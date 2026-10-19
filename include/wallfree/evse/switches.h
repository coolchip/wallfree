/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DIP-Schalter D1..D10 und Drehkodierschalter (Abschnitt 2.2, Ist-Spez. §10).
 */
#ifndef WALLFREE_EVSE_SWITCHES_H_
#define WALLFREE_EVSE_SWITCHES_H_

#include <wallfree/evse/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** DIP-Bitmaske: Bit 0 = D1 ... Bit 9 = D10, 1 = ON. */
typedef uint16_t evse_dip_t;

#define EVSE_DIP_MASK 0x3ffu

static inline bool evse_dip(evse_dip_t dip, int n)
{
	return n >= 1 && n <= 10 && (dip & (1u << (n - 1))) != 0;
}

/** Ergebnis der Drehschalterdekodierung. */
struct evse_rotary {
	uint8_t position;  /**< 0..15 */
	bool valid;        /**< false bei A..F (ERR-SWITCH, E11) */
	bool pwm5;         /**< Position 0: 5-%-Modus */
	int32_t limit_ma;  /**< Positionen 1..9, sonst 0 */
};

/** Position 0..15 dekodieren (HB S. 16 Tab. 3-4). */
struct evse_rotary evse_rotary_decode(uint8_t position);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_SWITCHES_H_ */
