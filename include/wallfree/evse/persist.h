/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Persistenz der Ladelogik ueber den Parameterspeicher (Abschnitt 13,
 * SOLL-NVM-03/04/10, SOLL-BOOT-04, SOLL-PAR-03/40).
 *
 * Kennungen im Speicher = Kennungen der Parametertabelle (Feldnummer des
 * Originals bzw. 1001..); dazu 465 fuer den zuletzt erreichten
 * Verriegelungs-Endzustand. Werte werden als int32 (4 Byte, little-endian)
 * gespeichert. Kennungen ab 3000 sind fuer Anwendungsparameter (Netz,
 * Zugang) reserviert.
 */
#ifndef WALLFREE_EVSE_PERSIST_H_
#define WALLFREE_EVSE_PERSIST_H_

#include <wallfree/evse/charger.h>
#include <wallfree/evse/param.h>
#include <wallfree/nvm/nvm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVSE_NVM_ID_LOCK  465   /* Original Feld 465 */
#define EVSE_NVM_ID_APP   3000  /* erste Kennung fuer Anwendungsparameter */

struct evse_load_result {
	uint16_t restored;   /**< uebernommen */
	uint16_t rejected;   /**< ungueltiger Wert, Default bleibt */
	uint16_t unknown;    /**< Kennung ohne Parameter der Ladelogik */
};

/**
 * Gespeicherte Konfiguration und Verriegelungszustand laden (SOLL-BOOT-04).
 * @p cfg enthaelt vorher die Werkseinstellungen; @p lock wird nur bei einem
 * gespeicherten Endzustand geaendert.
 */
int evse_persist_load(struct nvm *n, struct evse_config *cfg, enum evse_lock_state *lock,
		      struct evse_load_result *res);

/** Wert eines persistenten Parameters speichern (nach erfolgreichem evse_param_set). */
int evse_persist_param(struct nvm *n, const struct evse_param *p, int32_t v);

/** Verriegelungs-Endzustand speichern; andere Zustaende werden ignoriert. */
int evse_persist_lock(struct nvm *n, enum evse_lock_state st);

/**
 * Flash-Loeschen erlaubt (SOLL-NVM-04)? S1, CP A, Schuetz aus (Soll und
 * Ausgabe), kein Verriegelungszyklus und kein RCM-Selbsttest aktiv. S0 nur
 * vor dem Start der Ladelogik (Aufgabe der Anwendung): in S0 laufen
 * Selbsttest und Start-Entriegelung, deren Zeitfolge ein Loeschen stoeren
 * wuerde.
 */
bool evse_flash_erase_allowed(const struct evse *e);

/* ------------------------------------------------ Import aus Sektor 3 */

enum evse_import_result {
	EVSE_IMP_DONE = 0,     /**< uebernommen und gespeichert */
	EVSE_IMP_ZERO,         /**< im Original nicht gespeichert = 0, so uebernommen und gespeichert */
	EVSE_IMP_REJECTED,     /**< Wert nicht zulaessig, @p err sagt warum */
	EVSE_IMP_NOT_SAVED,    /**< uebernommen, aber Speichern fehlgeschlagen */
};

#define EVSE_IMP_E_UNSAFE (-100) /**< nicht im sicheren Zustand */
#define EVSE_IMP_E_ORIG   (-101) /**< Originalbereich ungueltig */

typedef void (*evse_import_report_t)(void *ctx, uint16_t field, const char *name, int32_t value,
				     enum evse_import_result res, int err);

/**
 * Einmalige Uebernahme aus dem Originalbereich (SOLL-NVM-10, SOLL-SH-09):
 * Felder 327..330, 390, 409, 412, 519..527 ueber evse_param_set (Grenz- und
 * Zustandspruefung) und speichern. Ein fehlender Eintrag gilt als 0: Das Original
 * speichert nur Abweichungen vom RAM-Startwert, und der ist 0 (wallbe2
 * parameterspeicher.md, Firmware 0.81). Netzwerkfelder uebernimmt die Anwendung
 * (wf_sys_import_orig).
 * Nur wenn evse_flash_erase_allowed().
 * @return Anzahl uebernommener Werte, EVSE_IMP_E_UNSAFE, EVSE_IMP_E_ORIG
 *         (Originalbereich ungueltig) oder NVM_E_IO
 */
int evse_import_orig(struct evse *e, const struct nvm_flash *orig, struct nvm *n, evse_ms_t now,
		     evse_import_report_t report, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_PERSIST_H_ */
