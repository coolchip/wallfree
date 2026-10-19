/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Digitale Ein-/Ausgaenge der Steuerung und Schutz-Interrupts (SOLL-PROT-05).
 * Pins aus dem Devicetree-Knoten zephyr,user (boards/phoenix/ev_cc_ac1).
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include "hw.h"

LOG_MODULE_REGISTER(hw_gpio, LOG_LEVEL_INF);

#define USER DT_PATH(zephyr_user)

static const struct gpio_dt_spec contactor = GPIO_DT_SPEC_GET(USER, contactor_gpios);
static const struct gpio_dt_spec d5_out = GPIO_DT_SPEC_GET(USER, d5_out_gpios);
static const struct gpio_dt_spec ct_sense = GPIO_DT_SPEC_GET(USER, ct_sense_gpios);
static const struct gpio_dt_spec rcm_sw1 = GPIO_DT_SPEC_GET(USER, rcm_sw1_gpios);
static const struct gpio_dt_spec rcm_sw2 = GPIO_DT_SPEC_GET(USER, rcm_sw2_gpios);
static const struct gpio_dt_spec rcm_err = GPIO_DT_SPEC_GET(USER, rcm_err_gpios);
static const struct gpio_dt_spec rcm_test = GPIO_DT_SPEC_GET(USER, rcm_test_gpios);
static const struct gpio_dt_spec lock_pin = GPIO_DT_SPEC_GET(USER, lock_gpios);
static const struct gpio_dt_spec unlock_pin = GPIO_DT_SPEC_GET(USER, unlock_gpios);
static const struct gpio_dt_spec dout_en = GPIO_DT_SPEC_GET(USER, dout_enable_gpios);
static const struct gpio_dt_spec reset_btn = GPIO_DT_SPEC_GET(USER, reset_button_gpios);

#define SPEC_BY_IDX(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx)

static const struct gpio_dt_spec dout[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER, dout_gpios, SPEC_BY_IDX, (,))};
static const struct gpio_dt_spec led[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER, led_gpios, SPEC_BY_IDX, (,))};
static const struct gpio_dt_spec led_aux[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER, led_aux_gpios, SPEC_BY_IDX, (,))};
static const struct gpio_dt_spec dip[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER, dip_gpios, SPEC_BY_IDX, (,))};
static const struct gpio_dt_spec rotary[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER, rotary_gpios, SPEC_BY_IDX, (,))};
static const struct gpio_dt_spec rs485_de[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER, rs485_de_gpios, SPEC_BY_IDX, (,))};

BUILD_ASSERT(ARRAY_SIZE(dout) == EVSE_DOUT_COUNT);
BUILD_ASSERT(ARRAY_SIZE(led) == EVSE_LED_COUNT);
BUILD_ASSERT(ARRAY_SIZE(dip) == 10);
BUILD_ASSERT(ARRAY_SIZE(led_aux) == 3); /* Modem 1/2, Netzwerk (PC2) */
#define LED_AUX_NET 2
BUILD_ASSERT(ARRAY_SIZE(rotary) == 4);

static atomic_t protect_latch;
static atomic_t dout_enabled;
static volatile int64_t last_sense_edge = INT64_MIN;
static struct gpio_callback rcm_cb;
static struct gpio_callback sense_cb;

/* ------------------------------------------------ Schutzpfad */

bool hw_protect_latched(void)
{
	return atomic_get(&protect_latch) != 0;
}

void hw_protect_trip(void)
{
	hw_contactor_off_regs();
	atomic_set(&protect_latch, 1);
}

void hw_protect_clear(void)
{
	atomic_set(&protect_latch, 0);
}

static void rcm_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* SOLL-PROT-05: Schuetz sofort aus, unabhaengig vom Automaten */
	hw_protect_trip();
}

static void sense_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	last_sense_edge = k_uptime_get();
}

int64_t hw_gpio_last_sense_edge(void)
{
	return last_sense_edge;
}

/* ------------------------------------------------ Lesen */

void hw_gpio_read_rcm(struct evse_rcm_levels *lv)
{
	lv->sw1 = gpio_pin_get_dt(&rcm_sw1) == 1;
	lv->sw2 = gpio_pin_get_dt(&rcm_sw2) == 1;
	lv->err = gpio_pin_get_dt(&rcm_err) == 1;
}

void hw_gpio_read_switches(evse_dip_t *d, uint8_t *rot)
{
	evse_dip_t v = 0;
	uint8_t r = 0;

	for (size_t i = 0; i < ARRAY_SIZE(dip); i++) {
		if (gpio_pin_get_dt(&dip[i]) == 1) {
			v |= (evse_dip_t)(1u << i);
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(rotary); i++) {
		if (gpio_pin_get_dt(&rotary[i]) == 1) {
			r |= (uint8_t)(1u << i);
		}
	}
	*d = v;
	*rot = r;
}

bool hw_gpio_reset_button(void)
{
	return gpio_pin_get_dt(&reset_btn) == 1;
}

/* ------------------------------------------------ Schreiben */

void hw_gpio_apply(const struct evse_outputs *o)
{
	/* Schuetz nur ein, wenn der Schutzpfad nicht verriegelt ist (SOLL-PROT-01) */
	bool on = o->contactor && !hw_protect_latched();

	gpio_pin_set_dt(&contactor, on);
	gpio_pin_set_dt(&d5_out, o->pe1);
	gpio_pin_set_dt(&lock_pin, o->lock_pulse && !o->unlock_pulse);
	gpio_pin_set_dt(&unlock_pin, o->unlock_pulse && !o->lock_pulse);
	gpio_pin_set_dt(&rcm_test, o->rcm_test);
	for (int i = 0; i < EVSE_DOUT_COUNT; i++) {
		gpio_pin_set_dt(&dout[i], o->dout[i]);
	}
	for (int i = 0; i < EVSE_LED_COUNT; i++) {
		gpio_pin_set_dt(&led[i], o->led[i]);
	}
	/* SOLL-IO-21: Treiberfreigabe erst nach der ersten Ausgangsberechnung */
	if (!atomic_get(&dout_enabled)) {
		gpio_pin_set_dt(&dout_en, 1);
		atomic_set(&dout_enabled, 1);
	}
}

/* ------------------------------------------------ Initialisierung */

static int cfg_out(const struct gpio_dt_spec *s)
{
	if (!gpio_is_ready_dt(s)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(s, GPIO_OUTPUT_INACTIVE);
}

static int cfg_in(const struct gpio_dt_spec *s)
{
	if (!gpio_is_ready_dt(s)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(s, GPIO_INPUT);
}

int hw_gpio_init(void)
{
	int err = 0;

	/* Ausgaenge inaktiv (SOLL-IO-02): Schuetz aus, Aktor in Ruhe, kein Test */
	err |= cfg_out(&contactor);
	err |= cfg_out(&d5_out);
	err |= cfg_out(&lock_pin);
	err |= cfg_out(&unlock_pin);
	err |= cfg_out(&rcm_test);
	err |= cfg_out(&dout_en);
	for (size_t i = 0; i < ARRAY_SIZE(dout); i++) {
		err |= cfg_out(&dout[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(led); i++) {
		err |= cfg_out(&led[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(led_aux); i++) {
		err |= cfg_out(&led_aux[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(rs485_de); i++) {
		err |= cfg_out(&rs485_de[i]); /* SOLL-IO-01 */
	}

	err |= cfg_in(&ct_sense);
	err |= cfg_in(&rcm_sw1);
	err |= cfg_in(&rcm_sw2);
	err |= cfg_in(&rcm_err);
	err |= cfg_in(&reset_btn);
	for (size_t i = 0; i < ARRAY_SIZE(dip); i++) {
		err |= cfg_in(&dip[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(rotary); i++) {
		err |= cfg_in(&rotary[i]);
	}
	if (err) {
		LOG_ERR("GPIO-Initialisierung fehlgeschlagen");
		return -EIO;
	}

	/* Schutz-Interrupts: Flanke nach "ausgeloest" (SOLL-PROT-05) */
	gpio_init_callback(&rcm_cb, rcm_isr,
			   BIT(rcm_sw1.pin) | BIT(rcm_sw2.pin) | BIT(rcm_err.pin));
	err |= gpio_add_callback_dt(&rcm_sw1, &rcm_cb);
	err |= gpio_pin_interrupt_configure_dt(&rcm_sw1, GPIO_INT_EDGE_TO_ACTIVE);
	err |= gpio_pin_interrupt_configure_dt(&rcm_sw2, GPIO_INT_EDGE_TO_ACTIVE);
	err |= gpio_pin_interrupt_configure_dt(&rcm_err, GPIO_INT_EDGE_TO_ACTIVE);

	/* Schuetzueberwachung "Via Sense": steigende Flanke an PE0 */
	gpio_init_callback(&sense_cb, sense_isr, BIT(ct_sense.pin));
	err |= gpio_add_callback_dt(&ct_sense, &sense_cb);
	err |= gpio_pin_interrupt_configure_dt(&ct_sense, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Interrupt-Konfiguration fehlgeschlagen");
		return -EIO;
	}
	return 0;
}

void hw_gpio_net_led(bool on)
{
	gpio_pin_set_dt(&led_aux[LED_AUX_NET], on);
}

void hw_gpio_led(enum evse_led idx, bool on)
{
	gpio_pin_set_dt(&led[idx], on);
}
