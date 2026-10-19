/* SPDX-License-Identifier: Apache-2.0 */
/* Leser fuer den Parameterbereich der Originalfirmware (SOLL-NVM-10) */
#include <wallfree/nvm/orig.h>

#define WORDS_PER_READ 16u

int nvm_orig_read(const struct nvm_flash *fl, uint16_t key, uint16_t *value)
{
	uint8_t b[4 * WORDS_PER_READ];
	int found = NVM_E_NOENT;

	if (fl->size < 8u || fl->read(fl->ctx, 0, b, 2) != 0) {
		return NVM_E_IO;
	}
	if (b[0] != 0 || b[1] != 0) {
		return NVM_E_STATE;
	}
	/* vorwaerts bis zum Ende: der letzte Treffer ist der juengste */
	for (uint32_t off = 4; off + 4u <= fl->size;) {
		uint32_t n = (fl->size - off) / 4u;

		if (n > WORDS_PER_READ) {
			n = WORDS_PER_READ;
		}
		if (fl->read(fl->ctx, off, b, n * 4u) != 0) {
			return NVM_E_IO;
		}
		for (uint32_t i = 0; i < n; i++) {
			const uint8_t *w = b + 4u * i;
			uint16_t k = (uint16_t)(w[2] | (w[3] << 8));

			if (k == key) {
				*value = (uint16_t)(w[0] | (w[1] << 8));
				found = NVM_OK;
			}
		}
		off += n * 4u;
	}
	return found;
}
