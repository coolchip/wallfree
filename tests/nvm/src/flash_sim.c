/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "flash_sim.h"

static int sim_read(void *ctx, uint32_t off, void *buf, size_t len)
{
	struct fsim *s = ctx;

	if (s->dead || off + len > s->fl.size) {
		return -1;
	}
	memcpy(buf, s->mem + off, len);
	return 0;
}

static int sim_write(void *ctx, uint32_t off, const void *buf, size_t len)
{
	struct fsim *s = ctx;
	const uint8_t *b = buf;

	if (s->dead || off + len > s->fl.size) {
		return -1;
	}
	s->writes++;
	for (size_t i = 0; i < len; i++) {
		if (s->budget == 0) {
			s->dead = true;
			return -1;
		}
		if (s->budget > 0) {
			s->budget--;
		}
		if (s->mem[off + i] != 0xFF) {
			s->overprogram = true;
		}
		s->mem[off + i] &= b[i];
		s->bytes++;
	}
	return 0;
}

static int sim_erase(void *ctx)
{
	struct fsim *s = ctx;

	if (s->dead) {
		return -1;
	}
	s->erases++;
	if (s->cut_in_erase) {
		/* unterbrochenes Loeschen: Inhalt teilweise geloescht, Rest verfaelscht */
		for (uint32_t i = 0; i < s->fl.size; i++) {
			s->mem[i] = i < s->fl.size / 3 ? 0xFF : (uint8_t)(s->mem[i] ^ (i * 37u));
		}
		s->dead = true;
		return -1;
	}
	memset(s->mem, 0xFF, s->fl.size);
	return 0;
}

void fsim_init(struct fsim *s, uint32_t size)
{
	memset(s, 0, sizeof(*s));
	memset(s->mem, 0xFF, sizeof(s->mem));
	s->fl.read = sim_read;
	s->fl.write = sim_write;
	s->fl.erase = sim_erase;
	s->fl.ctx = s;
	s->fl.size = size;
	s->budget = -1;
}

void fsim_power_on(struct fsim *s)
{
	s->dead = false;
	s->budget = -1;
	s->cut_in_erase = false;
	s->writes = 0;
	s->bytes = 0;
	s->erases = 0;
	s->overprogram = false;
}
