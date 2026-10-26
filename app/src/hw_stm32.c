/* SPDX-License-Identifier: Apache-2.0 */
/*
 * TIM1-PWM (CP), ADC1 mit DMA2 synchron zu den CP-Plateaus, MCO1 fuer den
 * PHY, PHY-Reset, sicherer Zustand per Register.
 *
 * Nachbildung des Originalablaufs (wallbe2 docs/takt-adc-gpio.md):
 *  - TIM1: Timertakt 100 MHz, PSC 49, center-aligned Modus 1, ARR 1000
 *    -> 1 kHz, PWM1, CCR = Tastgrad in Promille, PE9 AF1.
 *  - Update-Interrupt (Unter- und Ueberlauf, 2 kHz): bei CNT < 50 (Mitte des
 *    positiven Plateaus) Gruppe 1, bei CNT > 950 Gruppe 2 per Software
 *    starten; ADC diskontinuierlich mit je 5 Raengen.
 *  - DMA2 Stream0 zirkular, 80 Worte = 8 Durchlaeufe, Summe je Rang.
 *
 * Nur gebaut, nicht auf Hardware ausgefuehrt.
 */
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_dma.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_iwdg.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_rtc.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_tim.h>
#include "hw.h"

LOG_MODULE_REGISTER(hw_stm32, LOG_LEVEL_INF);

#define CP_ARR       1000
#define CP_PSC       49    /* 100 MHz / 50 = 2 MHz, center-aligned -> 1 kHz */
#define RANKS        10
#define PASSES       8
#define DMA_WORDS    (RANKS * PASSES)

#define IRQ_PRIO_TIM 1
#define IRQ_PRIO_DMA 2

K_SEM_DEFINE(hw_block_sem, 0, 1);

static volatile uint32_t dma_buf[DMA_WORDS];
static volatile uint32_t group_toggle;
static struct hw_block block;
static volatile bool block_valid;

/* ------------------------------------------------ sicherer Zustand (Register) */

void hw_contactor_off_regs(void)
{
	GPIOE->BSRR = (1u << (2 + 16)); /* PE2 Low */
}

void hw_safe_state_regs(void)
{
	hw_contactor_off_regs();
	TIM1->CCR1 = 0;                  /* CP -12 V (F5) */
	GPIOA->BSRR = (1u << 9) | (1u << 10); /* PA9/PA10 High: kein Aktorimpuls */
	GPIOE->BSRR = (1u << 5);         /* PE5 High: kein RCM-Test */
}

/* ------------------------------------------------ frueheste Pinpegel (SOLL-IO-02) */

static int hw_early_pins(void)
{
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA | LL_AHB1_GRP1_PERIPH_GPIOB |
				 LL_AHB1_GRP1_PERIPH_GPIOC | LL_AHB1_GRP1_PERIPH_GPIOD |
				 LL_AHB1_GRP1_PERIPH_GPIOE);

	/* Pegel vor der Umschaltung auf Ausgang setzen */
	GPIOE->BSRR = (1u << (2 + 16)) | (1u << (1 + 16)) | (1u << 5);
	GPIOA->BSRR = (1u << 9) | (1u << 10);
	GPIOB->BSRR = (1u << (2 + 16)) | (0xf0u << 16); /* PB2, PB4..PB7 Low */

	LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_2, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_1, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_10, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_2, LL_GPIO_MODE_OUTPUT);
	for (uint32_t pin = LL_GPIO_PIN_4; pin <= LL_GPIO_PIN_7; pin <<= 1) {
		LL_GPIO_SetPinMode(GPIOB, pin, LL_GPIO_MODE_OUTPUT);
	}
	return 0;
}

SYS_INIT(hw_early_pins, PRE_KERNEL_1, 0);

/* ------------------------------------------------ MCO1 und PHY-Reset */

static int hw_phy_clock_reset(void)
{
	/* MCO1 = HSE ohne Teiler auf PA8 (25 MHz fuer PHY-XI, Hypothese) */
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_8, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_8, LL_GPIO_AF_0);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_8, LL_GPIO_SPEED_FREQ_VERY_HIGH);
	LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_8, LL_GPIO_OUTPUT_PUSHPULL);
	LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_HSE, LL_RCC_MCO1_DIV_1);

	/* PA15 >= 10 ms Low, dann High (Kandidat PHY RST#, SOLL-BOOT-09) */
	GPIOA->BSRR = (1u << (15 + 16));
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_15, LL_GPIO_MODE_OUTPUT);
	k_busy_wait(11000);
	GPIOA->BSRR = (1u << 15);
	k_busy_wait(1000);
	return 0;
}

/* vor dem Ethernet-Treiber (CONFIG_ETH_INIT_PRIORITY) */
SYS_INIT(hw_phy_clock_reset, POST_KERNEL, 40);

/* ------------------------------------------------ Interrupts */

static void tim1_up_isr(const void *arg)
{
	ARG_UNUSED(arg);
	if (!LL_TIM_IsActiveFlag_UPDATE(TIM1)) {
		return;
	}
	LL_TIM_ClearFlag_UPDATE(TIM1);

	uint32_t cnt = LL_TIM_GetCounter(TIM1);

	if ((group_toggle & 1u) == 0 && cnt < 50) {
		LL_ADC_REG_StartConversionSWStart(ADC1); /* Gruppe 1: CP+ usw. */
		group_toggle++;
	} else if ((group_toggle & 1u) == 1 && cnt > 950) {
		LL_ADC_REG_StartConversionSWStart(ADC1); /* Gruppe 2: CP- usw. */
		group_toggle++;
	}
}

static void dma2_s0_isr(const void *arg)
{
	ARG_UNUSED(arg);
	if (!LL_DMA_IsActiveFlag_TC0(DMA2)) {
		LL_DMA_ClearFlag_HT0(DMA2);
		LL_DMA_ClearFlag_TE0(DMA2);
		LL_DMA_ClearFlag_FE0(DMA2);
		return;
	}
	LL_DMA_ClearFlag_TC0(DMA2);
	LL_DMA_ClearFlag_HT0(DMA2);

	uint32_t sum[RANKS] = {0};

	for (int p = 0; p < PASSES; p++) {
		for (int r = 0; r < RANKS; r++) {
			sum[r] += dma_buf[p * RANKS + r] & 0xfffu;
		}
	}

	/* Skalierung des Originals (takt-adc-gpio.md, Tabelle ADC1-Sequenz) */
	block.cp_pos = (int32_t)(sum[0] * 39u / 100u + 300u);
	block.din[EVSE_DIN_LD] = (int32_t)(sum[1] * 1066u / 1000u);
	block.din[EVSE_DIN_ML] = (int32_t)(sum[2] * 1066u / 1000u);
	block.pp = (int32_t)(sum[3] * 304u / 1000u);
	block.din[EVSE_DIN_IN] = (int32_t)(sum[4] * 1066u / 1000u);
	block.cp_neg = 14050 - (int32_t)(sum[5] * 397u / 1000u);
	block.din[EVSE_DIN_EN] = (int32_t)(sum[6] * 1066u / 1000u);
	block.din[EVSE_DIN_XR] = (int32_t)(sum[7] * 1066u / 1000u);
	block.supply = (int32_t)(sum[8] * 457u / 1000u);
	block.temp_raw = (int32_t)sum[9];
	block.seq++;
	block.at_ms = k_uptime_get();
	block_valid = true;
	k_sem_give(&hw_block_sem);
}

bool hw_get_block(struct hw_block *out)
{
	unsigned int key = irq_lock();
	bool v = block_valid;

	*out = block;
	irq_unlock(key);
	return v;
}

void hw_cp_set_duty(uint16_t permille)
{
	if (permille > CP_ARR) {
		permille = CP_ARR;
	}
	LL_TIM_OC_SetCompareCH1(TIM1, permille); /* Preload: wirkt zur naechsten Periode */
}

/* ------------------------------------------------ Initialisierung */

static void gpio_analog(GPIO_TypeDef *port, uint32_t pin)
{
	LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_ANALOG);
	LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_NO);
}

static void adc_init(void)
{
	static const uint32_t ranks[RANKS] = {
		LL_ADC_REG_RANK_1, LL_ADC_REG_RANK_2, LL_ADC_REG_RANK_3, LL_ADC_REG_RANK_4,
		LL_ADC_REG_RANK_5, LL_ADC_REG_RANK_6, LL_ADC_REG_RANK_7, LL_ADC_REG_RANK_8,
		LL_ADC_REG_RANK_9, LL_ADC_REG_RANK_10,
	};
	/* Gruppe 1 (CNT < 50): CP+, LD, ML, PP, IN; Gruppe 2 (CNT > 950): CP-, EN, XR,
	 * Versorgung, Temperatur */
	static const uint32_t channels[RANKS] = {
		LL_ADC_CHANNEL_0, LL_ADC_CHANNEL_3, LL_ADC_CHANNEL_5, LL_ADC_CHANNEL_8,
		LL_ADC_CHANNEL_13, LL_ADC_CHANNEL_9, LL_ADC_CHANNEL_4, LL_ADC_CHANNEL_6,
		LL_ADC_CHANNEL_10, LL_ADC_CHANNEL_TEMPSENSOR,
	};

	gpio_analog(GPIOA, LL_GPIO_PIN_0);
	gpio_analog(GPIOA, LL_GPIO_PIN_3);
	gpio_analog(GPIOA, LL_GPIO_PIN_4);
	gpio_analog(GPIOA, LL_GPIO_PIN_5);
	gpio_analog(GPIOA, LL_GPIO_PIN_6);
	gpio_analog(GPIOB, LL_GPIO_PIN_0);
	gpio_analog(GPIOB, LL_GPIO_PIN_1);
	gpio_analog(GPIOC, LL_GPIO_PIN_0);
	gpio_analog(GPIOC, LL_GPIO_PIN_3);

	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC1);
	LL_ADC_SetCommonClock(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_CLOCK_SYNC_PCLK_DIV2);
	LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1),
				       LL_ADC_PATH_INTERNAL_TEMPSENSOR);
	LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_12B);
	LL_ADC_SetDataAlignment(ADC1, LL_ADC_DATA_ALIGN_RIGHT);
	LL_ADC_SetSequencersScanMode(ADC1, LL_ADC_SEQ_SCAN_ENABLE);
	LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_SOFTWARE);
	LL_ADC_REG_SetContinuousMode(ADC1, LL_ADC_REG_CONV_SINGLE);
	LL_ADC_REG_SetSequencerLength(ADC1, LL_ADC_REG_SEQ_SCAN_ENABLE_10RANKS);
	LL_ADC_REG_SetSequencerDiscont(ADC1, LL_ADC_REG_SEQ_DISCONT_5RANKS);
	LL_ADC_REG_SetFlagEndOfConversion(ADC1, LL_ADC_REG_FLAG_EOC_UNITARY_CONV);
	LL_ADC_REG_SetDMATransfer(ADC1, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
	for (int r = 0; r < RANKS; r++) {
		LL_ADC_REG_SetSequencerRanks(ADC1, ranks[r], channels[r]);
		LL_ADC_SetChannelSamplingTime(ADC1, channels[r], LL_ADC_SAMPLINGTIME_28CYCLES);
	}

	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
	LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_0);
	LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_0, LL_DMA_CHANNEL_0);
	LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_0, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
	LL_DMA_SetMode(DMA2, LL_DMA_STREAM_0, LL_DMA_MODE_CIRCULAR);
	LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_0, LL_DMA_PERIPH_NOINCREMENT);
	LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_0, LL_DMA_MEMORY_INCREMENT);
	LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_0, LL_DMA_PDATAALIGN_WORD);
	LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_0, LL_DMA_MDATAALIGN_WORD);
	LL_DMA_SetStreamPriorityLevel(DMA2, LL_DMA_STREAM_0, LL_DMA_PRIORITY_HIGH);
	LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_0,
			       LL_ADC_DMA_GetRegAddr(ADC1, LL_ADC_DMA_REG_REGULAR_DATA),
			       (uint32_t)dma_buf, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
	LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_0, DMA_WORDS);
	LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_0);
	LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_0);

	LL_ADC_Enable(ADC1);
}

static void tim1_init(void)
{
	/* PE9 = TIM1_CH1, AF1 */
	LL_GPIO_SetPinMode(GPIOE, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_8_15(GPIOE, LL_GPIO_PIN_9, LL_GPIO_AF_1);
	LL_GPIO_SetPinOutputType(GPIOE, LL_GPIO_PIN_9, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinSpeed(GPIOE, LL_GPIO_PIN_9, LL_GPIO_SPEED_FREQ_LOW);

	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);
	LL_TIM_SetPrescaler(TIM1, CP_PSC);
	LL_TIM_SetCounterMode(TIM1, LL_TIM_COUNTERMODE_CENTER_DOWN); /* CMS = 01 */
	LL_TIM_SetAutoReload(TIM1, CP_ARR);
	LL_TIM_SetRepetitionCounter(TIM1, 0);  /* Update bei Unter- und Ueberlauf */
	LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
	LL_TIM_OC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_OCPOLARITY_HIGH);
	LL_TIM_OC_SetCompareCH1(TIM1, CP_ARR); /* Start +12 V DC */
	LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH1);
	LL_TIM_EnableARRPreload(TIM1);
	LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH1);
	LL_TIM_GenerateEvent_UPDATE(TIM1);
	LL_TIM_ClearFlag_UPDATE(TIM1);
	LL_TIM_EnableAllOutputs(TIM1);         /* MOE */
	LL_TIM_EnableIT_UPDATE(TIM1);
}

int hw_analog_init(void)
{
	adc_init();
	tim1_init();

	IRQ_CONNECT(TIM1_UP_TIM10_IRQn, IRQ_PRIO_TIM, tim1_up_isr, NULL, 0);
	IRQ_CONNECT(DMA2_Stream0_IRQn, IRQ_PRIO_DMA, dma2_s0_isr, NULL, 0);
	irq_enable(TIM1_UP_TIM10_IRQn);
	irq_enable(DMA2_Stream0_IRQn);

	LL_TIM_EnableCounter(TIM1);
	LOG_INF("CP-PWM 1 kHz und ADC-Abtastung gestartet");
	return 0;
}

/* ------------------------------------------------ IWDG (SOLL-BOOT-03, SOLL-RT-12) */

static const struct device *const wdt_dev = DEVICE_DT_GET(DT_NODELABEL(iwdg));
static int wdt_ch = -1;

int hw_wdt_start(uint32_t timeout_ms)
{
	struct wdt_timeout_cfg wcfg = {
		.window = {.min = 0, .max = timeout_ms},
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(wdt_dev)) {
		return -ENODEV;
	}
	wdt_ch = wdt_install_timeout(wdt_dev, &wcfg);
	if (wdt_ch < 0 || wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
		wdt_ch = -1;
		return -EIO;
	}
	return 0;
}

void hw_wdt_feed(void)
{
	if (wdt_ch >= 0) {
		(void)wdt_feed(wdt_dev, wdt_ch);
	}
}

/* ------------------------------------------------ IWDG fuer Flash-Loeschen */

/*
 * SOLL-RT-13: Der Zephyr-Treiber kann das Limit nach dem Start nicht mehr
 * aendern, deshalb direkt ueber die Register (RM0033 Abschnitt IWDG). Das
 * Loeschen eines 128-KB-Sektors dauert laut Zephyr-Devicetree hoechstens
 * 4000 ms (stm32f2.dtsi, max-erase-time); Teiler 256 und Reload 4095 ergeben
 * bei LSI 32 kHz rund 32 s (bei 47 kHz rund 22 s).
 */
static uint32_t iwdg_saved_pr;
static uint32_t iwdg_saved_rlr;

void hw_iwdg_long(bool on)
{
	if (on) {
		iwdg_saved_pr = LL_IWDG_GetPrescaler(IWDG);
		iwdg_saved_rlr = LL_IWDG_GetReloadCounter(IWDG);
	}
	/* Schreibfreigabe und Werte ohne Unterbrechung: ein Bedienen (0xAAAA)
	 * aus dem Schutzthread wuerde die Freigabe sonst aufheben */
	unsigned int key = irq_lock();

	LL_IWDG_ReloadCounter(IWDG);
	LL_IWDG_EnableWriteAccess(IWDG);
	LL_IWDG_SetPrescaler(IWDG, on ? LL_IWDG_PRESCALER_256 : iwdg_saved_pr);
	LL_IWDG_SetReloadCounter(IWDG, on ? IWDG_RLR_RL : iwdg_saved_rlr);
	irq_unlock(key);

	int64_t t0 = k_uptime_get();

	while (!LL_IWDG_IsReady(IWDG) && k_uptime_get() - t0 < 100) {
	}
	LL_IWDG_ReloadCounter(IWDG);
}

/* ------------------------------------------------ Flash */

#define FLASH_SR_ERRBITS (FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | \
			  FLASH_SR_PGSERR)

static uint32_t flash_sr_early;

/* Diagnose: Stand direkt nach dem Sprung aus dem Bootloader, vor jedem Treiber */
static int hw_flash_sr_early(void)
{
	flash_sr_early = FLASH->SR & FLASH_SR_ERRBITS;
	return 0;
}
SYS_INIT(hw_flash_sr_early, EARLY, 0);

uint32_t hw_flash_errors_early(void)
{
	return flash_sr_early;
}

uint32_t hw_flash_take_errors(void)
{
	uint32_t sr = FLASH->SR & FLASH_SR_ERRBITS;

	FLASH->SR = sr; /* rc_w1 */
	return sr;
}

/* ------------------------------------------------ Resetgrund (SOLL-RT-14) */

/* BKP10..16 nutzt der Bootloader (Uebergabe SOLL-BOOT-30, Ergebnis, IWDG-Zaehler);
 * BKP17/18 Fehlerort (hw_fault_note), BKP19 Resetgrund */
#define RESET_BKP LL_RTC_BKP_DR19

static void bkp_access(void)
{
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
	LL_PWR_EnableBkUpAccess();
}

void hw_reset_take(uint32_t *flags, uint32_t *bkp)
{
	uint32_t f = 0;

	f |= LL_RCC_IsActiveFlag_LPWRRST() ? WF_RSTF_LPWR : 0;
	f |= LL_RCC_IsActiveFlag_WWDGRST() ? WF_RSTF_WWDG : 0;
	f |= LL_RCC_IsActiveFlag_IWDGRST() ? WF_RSTF_IWDG : 0;
	f |= LL_RCC_IsActiveFlag_SFTRST() ? WF_RSTF_SFT : 0;
	f |= LL_RCC_IsActiveFlag_PORRST() ? WF_RSTF_POR : 0;
	f |= LL_RCC_IsActiveFlag_PINRST() ? WF_RSTF_PIN : 0;
	f |= LL_RCC_IsActiveFlag_BORRST() ? WF_RSTF_BOR : 0;
	LL_RCC_ClearResetFlags();
	*flags = f;

	bkp_access();
	*bkp = LL_RTC_BAK_GetRegister(RTC, RESET_BKP);
	LL_RTC_BAK_SetRegister(RTC, RESET_BKP, 0);
}

void hw_fault_note(unsigned int reason, uint32_t pc)
{
	bkp_access();
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR17, pc);
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR18, 0xFA000000u | (reason & 0xFFFFu));
}

FUNC_NORETURN void hw_reset_with(enum wf_reset code)
{
	hw_safe_state_regs();
	bkp_access();
	LL_RTC_BAK_SetRegister(RTC, RESET_BKP, wf_reset_bkp_value(code));
	NVIC_SystemReset();
	CODE_UNREACHABLE;
}

/* ------------------------------------------------ Bootloader (SOLL-BOOT-30..33) */

/* Bedeutung: wallbe2 docs/bootloader.md, Abschnitt Update-Betrieb */
#define BL_MODE_WEB  2
#define BL_ATTEMPTS  3

FUNC_NORETURN void hw_bootloader_handover(uint32_t ip, uint32_t mask)
{
	hw_safe_state_regs();
	bkp_access();
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR10, BL_MODE_WEB);
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR11, BL_ATTEMPTS);
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR15, 0);
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR13, ip);
	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR14, mask);
	LL_RTC_BAK_SetRegister(RTC, RESET_BKP, wf_reset_bkp_value(WF_RST_UPDATE));
	__disable_irq();
	NVIC_SystemReset();
	CODE_UNREACHABLE;
}

void hw_boot_regs(uint32_t out[HW_BOOT_REGS])
{
	out[0] = RCC->CSR;
	bkp_access();
	for (int i = 0; i < HW_BOOT_REGS - 1; i++) {
		out[1 + i] = LL_RTC_BAK_GetRegister(RTC, LL_RTC_BKP_DR10 + i);
	}
}

uint32_t hw_update_result_take(void)
{
	bkp_access();
	uint32_t v = LL_RTC_BAK_GetRegister(RTC, LL_RTC_BKP_DR12);

	LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR12, 0);
	return v;
}
