/* SPDX-License-Identifier: Apache-2.0 */
/*
 * JSON der Zaehlerwerte (SOLL-MTR-30/31): WARP-API meters/0/value_ids,
 * meters/0/values und meters/0/state. Reines C.
 */
#ifndef WALLFREE_API_METER_JSON_H_
#define WALLFREE_API_METER_JSON_H_

#include <wallfree/api/json.h>
#include <wallfree/meter/meter.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Kennungen der Messgroessen des Modells, z. B. [1,2,3,13,...] */
void wf_meter_value_ids_json(uint8_t type, struct wf_jbuf *b);

/** Werte in derselben Reihenfolge, 3 Nachkommastellen, ungueltig = null */
void wf_meter_values_json(const struct meter_values *mv, struct wf_jbuf *b);

/** {"state":2,"type":1,"status":"ok","age_ms":..., Fehlerzaehler} */
void wf_meter_state_json(const struct meter_values *mv, struct wf_jbuf *b);

/** Tausendstel als Dezimalzahl, z. B. -1234 -> -1.234 */
void wf_jb_milli(struct wf_jbuf *b, int64_t v);

#ifdef __cplusplus
}
#endif

#endif
