/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Simulationsmodell fuer Szenariotests des Ladeautomaten: Fahrzeug am CP,
 * Kabel am PP, Bender-Sensor mit Trip-Test, Verriegelungsaktor mit
 * LD-Rueckmeldung, Schuetz mit Sense-Signal. Rein fuer Tests, keine
 * Aussage ueber reale Hardware.
 */
#ifndef SIM_H_
#define SIM_H_

#include <wallfree/evse/charger.h>

enum ld_model {
	LD_MODEL_NONE = 0,
	LD_MODEL_PERMANENT, /* Kontakt geschlossen, solange verriegelt */
	LD_MODEL_KUESTER,   /* 12 V nur waehrend der Fahrt (Fremdquelle) */
};

struct sim {
	struct evse e;
	struct evse_config cfg;
	evse_ms_t now;

	/* Fahrzeug */
	enum evse_cp_state car; /* A..D, E = Kurzschluss */
	bool diode;
	int32_t pp_mv;

	/* Eingaenge */
	int32_t din_mv[EVSE_DIN_COUNT];

	/* Bender */
	bool rcm_healthy;
	bool rcm_fault_current; /* echter Fehlerstrom */
	evse_ms_t test_pulse_at;

	/* Aktor */
	enum ld_model ld_model;
	bool actuator_locked;
	bool actuator_stuck;
	evse_ms_t pulse_start;
	bool pulse_was;
	bool ld_force_off;      /* LD-Kontakt faellt weg */

	/* Schuetz */
	bool welded;
	bool no_pull;           /* zieht nicht an: keine Rueckmeldung trotz Soll ein */
	bool protect_tripped;   /* Schutzmerker der Hardwareschicht (SOLL-PROT-14) */
	evse_ms_t last_edge;

	/* Versorgung, Messquelle */
	int32_t supply_mv;
	bool meter_valid;
	int32_t phase_ma[3];

	/* Beobachtung */
	int contactor_on_count;
	bool contactor_prev;
};

void sim_init(struct sim *s, const struct evse_config *cfg);
void sim_start(struct sim *s); /* evse_init */
void sim_run(struct sim *s, int ms);
/* laufen lassen, bis Zustand erreicht ist; @return benoetigte ms oder -1 */
int sim_until_state(struct sim *s, enum evse_state st, int max_ms);
/* bis nach Anlauf in S1 */
void sim_boot(struct sim *s);

#endif
