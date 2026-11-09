/* SPDX-License-Identifier: Apache-2.0 */
/* Lesen von Flash, OTP und Optionsbytes (SOLL-SH-24), siehe include/wallfree/api/flashread.h */
#include <wallfree/api/flashread.h>

/* Erlaubte Bereiche, jeweils erstes und letztes Byte (RM0033: Flash, OTP, Optionsbytes) */
static const struct {
	uint32_t first, last;
} allowed[] = {
	{0x08000000u, 0x0805FFFFu}, /* Sektor 0..6; Sektor 7 ab 0x08060000 ausgeschlossen */
	{0x1FFF7800u, 0x1FFF7A0Fu}, /* OTP 512 Byte und 16 Sperrbytes */
	{0x1FFFC000u, 0x1FFFC00Fu}, /* Optionsbytes */
};

int wf_flashread_check(uint32_t addr, uint32_t len)
{
	if (len == 0 || len > WF_FLASHREAD_MAX) {
		return -1;
	}
	for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
		/* ohne Ueberlauf: addr im Bereich, dann passt len in den Rest */
		if (addr >= allowed[i].first && addr <= allowed[i].last &&
		    len - 1 <= allowed[i].last - addr) {
			return 0;
		}
	}
	return -2;
}

size_t wf_base64(const uint8_t *in, size_t n, char *out, size_t out_size)
{
	static const char tab[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t need = 4 * ((n + 2) / 3);
	size_t o = 0;

	if (out == NULL || out_size < need + 1) {
		return 0;
	}
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)in[i] << 16;

		if (i + 1 < n) {
			v |= (uint32_t)in[i + 1] << 8;
		}
		if (i + 2 < n) {
			v |= in[i + 2];
		}
		out[o++] = tab[(v >> 18) & 0x3F];
		out[o++] = tab[(v >> 12) & 0x3F];
		out[o++] = i + 1 < n ? tab[(v >> 6) & 0x3F] : '=';
		out[o++] = i + 2 < n ? tab[v & 0x3F] : '=';
	}
	out[o] = '\0';
	return o;
}
