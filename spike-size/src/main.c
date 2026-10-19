/*
 * Groessentest fuer die Ersatzfirmware (nur Build). Bindet die geplanten
 * Grundbausteine ein, damit Flash/RAM realistisch gemessen werden:
 * Netzwerk + DHCP, HTTP-Server mit JSON-Ressource, Telnet-Shell, Settings/ZMS,
 * ADC, PWM, Watchdog. Keine Ladelogik, keine Hardwarefreigabe.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/data/json.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>

static const struct device *const pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm1));
static const struct device *const adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc1));
static const struct device *const wdt_dev = DEVICE_DT_GET(DT_NODELABEL(iwdg));

static uint16_t current_dA = 160; /* Sollstrom in 0,1 A (Entscheidung 21) */
static int16_t cp_mv;

struct state_json {
	int current_dA;
	int cp_mv;
	int64_t uptime_ms;
};

static const struct json_obj_descr state_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct state_json, current_dA, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct state_json, cp_mv, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct state_json, uptime_ms, JSON_TOK_NUMBER),
};

static int state_handler(struct http_client_ctx *client, enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static char buf[128];

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		struct state_json s = {current_dA, cp_mv, k_uptime_get()};
		int ret = json_obj_encode_buf(state_descr, ARRAY_SIZE(state_descr), &s, buf,
					      sizeof(buf));
		if (ret < 0) {
			return ret;
		}
		response_ctx->body = buf;
		response_ctx->body_len = strlen(buf);
		response_ctx->final_chunk = true;
	}
	return 0;
}

static struct http_resource_detail_dynamic state_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = state_handler,
};

static uint16_t api_port = 80;
HTTP_SERVICE_DEFINE(api, NULL, &api_port, 3, 5, NULL, NULL, NULL);
HTTP_RESOURCE_DEFINE(state_resource, api, "/evse/state", &state_detail);

static int set_current(const char *name, size_t len, settings_read_cb read_cb, void *arg)
{
	if (!strcmp(name, "current") && len == sizeof(current_dA)) {
		return read_cb(arg, &current_dA, len) == len ? 0 : -EINVAL;
	}
	return -ENOENT;
}
SETTINGS_STATIC_HANDLER_DEFINE(evse, "evse", NULL, set_current, NULL, NULL);

static int cmd_current(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		current_dA = (uint16_t)strtoul(argv[1], NULL, 10);
		settings_save_one("evse/current", &current_dA, sizeof(current_dA));
	}
	shell_print(sh, "current %u (0,1 A)", current_dA);
	return 0;
}
SHELL_CMD_REGISTER(current, NULL, "Sollstrom in 0,1 A lesen/setzen", cmd_current);

int main(void)
{
	struct adc_channel_cfg ch = {
		.gain = ADC_GAIN_1, .reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT, .channel_id = 0,
	};
	struct wdt_timeout_cfg wcfg = {.window.max = 1000, .flags = WDT_FLAG_RESET_SOC};
	int16_t sample;
	struct adc_sequence seq = {
		.channels = BIT(0), .buffer = &sample, .buffer_size = sizeof(sample),
		.resolution = 12,
	};
	int wdt_ch;

	settings_subsys_init();
	settings_load();
	adc_channel_setup(adc_dev, &ch);
	wdt_ch = wdt_install_timeout(wdt_dev, &wcfg);
	wdt_setup(wdt_dev, 0);
	http_server_start();

	while (1) {
		if (adc_read(adc_dev, &seq) == 0) {
			cp_mv = sample;
		}
		/* 1 kHz, Tastgrad aus Strom (Platzhalter fuer die Duty-Formel) */
		pwm_set(pwm_dev, 1, PWM_USEC(1000), PWM_USEC(current_dA * 1000 / 600), 0);
		wdt_feed(wdt_dev, wdt_ch);
		k_msleep(100);
	}
	return 0;
}
