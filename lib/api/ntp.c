/* SPDX-License-Identifier: Apache-2.0 */
/* Uhrzeit per SNTP, Hilfen ohne Netz, siehe include/wallfree/api/ntp.h */
#include <string.h>
#include <wallfree/api/ntp.h>

static int is_name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
	       c == '-' || c == '.';
}

int wf_ntp_parse_server(const char *s, size_t n, char *host, uint16_t *port)
{
	size_t h = n;
	uint32_t p = WF_NTP_PORT;

	for (size_t i = 0; i < n; i++) {
		if (s[i] == ':') {
			h = i;
			break;
		}
	}
	if (h == 0 || s[0] == '.' || s[0] == '-' || s[h - 1] == '.') {
		return -1;
	}
	for (size_t i = 0; i < h; i++) {
		if (!is_name_char(s[i])) {
			return -1;
		}
	}
	if (h < n) {
		if (h + 1 == n || n - h - 1 > 5) {
			return -1;
		}
		p = 0;
		for (size_t i = h + 1; i < n; i++) {
			if (s[i] < '0' || s[i] > '9') {
				return -1;
			}
			p = p * 10u + (uint32_t)(s[i] - '0');
		}
		if (p == 0 || p > 65535u) {
			return -1;
		}
	}
	memcpy(host, s, h);
	host[h] = '\0';
	*port = (uint16_t)p;
	return 0;
}

uint32_t wf_ntp_next_s(unsigned int fails)
{
	uint32_t s = WF_NTP_RETRY_MIN_S;

	if (fails == 0) {
		return WF_NTP_RESYNC_S;
	}
	for (unsigned int i = 1; i < fails && s < WF_NTP_RETRY_MAX_S; i++) {
		s *= 2u;
	}
	return s < WF_NTP_RETRY_MAX_S ? s : WF_NTP_RETRY_MAX_S;
}

int64_t wf_ntp_epoch_ms(uint64_t seconds, uint32_t fraction)
{
	if (seconds < WF_NTP_MIN_EPOCH || seconds >= WF_NTP_MAX_EPOCH) {
		return 0;
	}
	return (int64_t)(seconds * 1000u + (((uint64_t)fraction * 1000u) >> 32));
}

/* Kalender nach Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms" */
void wf_rtc_from_unix(uint64_t s, struct wf_rtc_time *t)
{
	uint32_t days = (uint32_t)(s / 86400u);
	uint32_t rem = (uint32_t)(s % 86400u);
	uint32_t z = days + 719468u;
	uint32_t era = z / 146097u;
	uint32_t doe = z - era * 146097u;
	uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	uint32_t mp = (5u * doy + 2u) / 153u;
	uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
	uint32_t y = yoe + era * 400u + (m <= 2u ? 1u : 0u);

	t->year = (uint8_t)(y - 2000u);
	t->month = (uint8_t)m;
	t->day = (uint8_t)(doy - (153u * mp + 2u) / 5u + 1u);
	t->weekday = (uint8_t)((days + 3u) % 7u + 1u); /* 1970-01-01 war ein Donnerstag */
	t->hour = (uint8_t)(rem / 3600u);
	t->minute = (uint8_t)(rem / 60u % 60u);
	t->second = (uint8_t)(rem % 60u);
}

static uint8_t bcd(uint32_t v)
{
	return (uint8_t)((v >> 4) * 10u + (v & 0xFu));
}

void wf_rtc_from_regs(uint32_t tr, uint32_t dr, struct wf_rtc_time *t)
{
	t->year = bcd((dr >> 16) & 0xFFu);
	t->weekday = (uint8_t)((dr >> 13) & 0x7u);
	t->month = bcd((dr >> 8) & 0x1Fu);
	t->day = bcd(dr & 0x3Fu);
	t->hour = bcd((tr >> 16) & 0x3Fu);
	t->minute = bcd((tr >> 8) & 0x7Fu);
	t->second = bcd(tr & 0x7Fu);
}

uint64_t wf_rtc_to_unix(const struct wf_rtc_time *t)
{
	if (t->year > 99u || t->month < 1u || t->month > 12u || t->day < 1u || t->day > 31u ||
	    t->hour > 23u || t->minute > 59u || t->second > 59u) {
		return 0;
	}
	uint32_t m = t->month;
	uint32_t y = 2000u + t->year - (m <= 2u ? 1u : 0u);
	uint32_t era = y / 400u;
	uint32_t yoe = y - era * 400u;
	uint32_t doy = (153u * (m > 2u ? m - 3u : m + 9u) + 2u) / 5u + t->day - 1u;
	uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	uint64_t s = (uint64_t)(era * 146097u + doe - 719468u) * 86400u + t->hour * 3600u +
		     t->minute * 60u + t->second;

	return s >= WF_NTP_MIN_EPOCH && s < WF_NTP_MAX_EPOCH ? s : 0;
}
