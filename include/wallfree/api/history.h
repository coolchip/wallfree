/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Verlauf (SOLL-API-33): Mittelwerte je WF_HIST_INTERVAL_MS in einem
 * Ringpuffer im RAM, JSON fuer GET /evse/history. Abtastung etwa
 * sekuendlich; Intervalle an der Laufzeit ausgerichtet. Nach einem Neustart
 * leer. Reines C, nicht threadsicher (Aufrufer sperrt).
 */
#ifndef WALLFREE_API_HISTORY_H_
#define WALLFREE_API_HISTORY_H_

#include <stdbool.h>
#include <stdint.h>
#include <wallfree/api/json.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_HIST_INTERVAL_MS (10 * 60 * 1000)
#define WF_HIST_SIZE        288 /* 48 h */

/* Zustand im Intervall (ODER ueber alle Abtastungen) */
#define WF_HIST_CONNECTED 0x01 /**< Fahrzeug angesteckt (B, C, D) */
#define WF_HIST_CHARGING  0x02 /**< charger_state 3 */
#define WF_HIST_ERROR     0x04 /**< charger_state 4 oder error_state != 0 */

#define WF_HIST_NO_POWER UINT16_MAX

struct wf_hist_row {
	uint16_t power_w; /**< mittlere Wirkleistung, WF_HIST_NO_POWER = kein gueltiger Messwert */
	uint8_t allowed;  /**< mittlerer erlaubter Strom in 0,25 A */
	uint8_t flags;    /**< WF_HIST_* */
};

struct wf_hist_acc {
	int64_t slot; /**< Intervallnummer (Laufzeit / Intervall), -1 = noch keine Abtastung */
	uint32_t n;   /**< Abtastungen */
	uint32_t p_n; /**< davon mit gueltiger Leistung */
	uint64_t p_sum;
	uint64_t a_sum;
	uint8_t flags;
};

struct wf_hist {
	struct wf_hist_row r[WF_HIST_SIZE];
	uint32_t count; /**< abgeschlossene Intervalle seit Start */
	int64_t end_ms; /**< Ende des juengsten abgeschlossenen Intervalls */
	struct wf_hist_acc acc;
};

void wf_hist_init(struct wf_hist *h);

/**
 * Eine Abtastung zur Zeit @p t_ms (ms seit Start). @p power_w < 0 = kein
 * gueltiger Messwert (Einspeisung zaehlt als 0, siehe Aufrufer). Beginnt mit
 * @p t_ms ein neues Intervall, wird das laufende abgeschlossen; Intervalle
 * ganz ohne Abtastung (Luecke) werden als leere Zeilen nachgetragen.
 */
void wf_hist_add(struct wf_hist *h, int64_t t_ms, int32_t power_w, int32_t allowed_ma,
		 uint8_t flags);

/** Zeile @p i, 0 = aelteste noch vorhandene; @return false, wenn i >= Anzahl */
bool wf_hist_get(const struct wf_hist *h, uint32_t i, struct wf_hist_row *out);

/** Anzahl vorhandener Zeilen (hoechstens WF_HIST_SIZE) */
uint32_t wf_hist_len(const struct wf_hist *h);

/** laufendes Intervall als Zeile; @return false ohne Abtastung */
bool wf_hist_current(const struct wf_hist *h, struct wf_hist_row *out);

#define WF_HIST_DONE UINT32_MAX

/**
 * In Teilen ausgeben, aelteste Zeile zuerst:
 * {"interval_ms":600000,"rows":[[1520,16000,3],[null,0,0],...],"age_ms":120000,
 *  "current":[...]|null}
 * Zeile = [Leistung W|null, erlaubter Strom mA, Zustand WF_HIST_*]; die
 * juengste Zeile endete age_ms vor @p now_ms. age_ms und current stehen am
 * Ende, damit sie zu den Zeilen passen, auch wenn zwischen zwei Teilen ein
 * Intervall endet. @p cursor 0 beginnt; @p b muss mindestens 128 Byte
 * fassen. @return naechster Cursor oder WF_HIST_DONE, wenn das Ende
 * geschrieben ist.
 */
uint32_t wf_hist_json(const struct wf_hist *h, int64_t now_ms, uint32_t cursor, struct wf_jbuf *b);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_HISTORY_H_ */
