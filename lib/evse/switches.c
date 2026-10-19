/* SPDX-License-Identifier: Apache-2.0 */
#include <wallfree/evse/switches.h>

struct evse_rotary evse_rotary_decode(uint8_t position)
{
	static const int32_t ma[] = {6000, 10000, 13000, 16000, 20000,
				     32000, 63000, 70000, 80000};
	struct evse_rotary r = {.position = (uint8_t)(position & 0x0f)};

	if (r.position == 0) {
		r.valid = true;
		r.pwm5 = true;
		r.limit_ma = 0;
	} else if (r.position <= 9) {
		r.valid = true;
		r.pwm5 = false;
		r.limit_ma = ma[r.position - 1];
	} else {
		r.valid = false; /* E11: Fehlerzustand statt stillem Nichtladen */
		r.pwm5 = false;
		r.limit_ma = 0;
	}
	return r;
}
