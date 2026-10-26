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
