/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Parameterspeicher der Anwendung (Soll-Spezifikation 13.1): eigener
 * Bereich in Sektor 7, Originalbereich in Sektor 3 nur lesen.
 */
#ifndef WALLFREE_APP_STORAGE_H_
#define WALLFREE_APP_STORAGE_H_

#include <wallfree/evse/persist.h>

struct storage_status {
	enum nvm_state state;
	uint32_t used;
	uint32_t free;
	uint8_t pending;       /**< Werte nur im RAM ("Speichern ausstehend") */
	bool damaged;
	bool invalid_at_boot;  /**< SOLL-NVM-05: Bereich war ungueltig -> Werkseinstellungen */
	uint32_t compactions;
	int last_error;        /**< letzter Fehler von Speichern/Verdichten, 0 = keiner */
};

/** Beim Start, vor der Ladelogik: einhaengen; ungueltig -> Meldung und neu anlegen. */
int storage_init(void);

/** Werksreset beim Start (SOLL-BOOT-20): Bereich loeschen und neu anlegen. */
int storage_factory_reset_boot(void);

/** Gespeicherte Werte in @p cfg / @p lock uebernehmen (SOLL-BOOT-04). */
int storage_load(struct evse_config *cfg, enum evse_lock_state *lock);

/** Speicher-Thread starten (Verriegelungszustand, Verdichten). */
void storage_start(void);

/** Persistenten Parameter speichern. @return NVM_OK, NVM_PENDING oder Fehler */
int storage_save_param(const struct evse_param *p, int32_t v);

/** Aus dem Automatenthread: Verriegelungszustand hat sich geaendert (nicht blockierend). */
void storage_lock_changed(enum evse_lock_state st);

/**
 * Werksreset im Betrieb (SOLL-SH-08): nur im sicheren Zustand; danach
 * Neustart durch den Aufrufer. @return 0, -EBUSY (nicht sicher) oder Fehler
 */
int storage_factory_reset(void);

/** Lesezugriff auf Sektor 3 je Feld (wf_orig_lookup_t). @return 0 gefunden */
int storage_orig_lookup(void *ctx, uint16_t field, uint16_t *value);

/** Import aus Sektor 3 (SOLL-SH-09). Rueckgabe wie evse_import_orig(). */
int storage_import_orig(evse_import_report_t report, void *ctx);

void storage_get_status(struct storage_status *st);

/* Anwendungsparameter (Kennungen ab EVSE_NVM_ID_APP) */
/* Zugangsdaten in einem Eintrag (atomar): 16 Byte HA1 = MD5(Benutzer:Realm:Passwort),
 * danach der Benutzername (1..32 Byte, ohne NUL) */
#define STORAGE_ID_WEB_CRED 3001

/** Gespeicherten Wert lesen. @return Laenge oder NVM-Fehler (NVM_E_NOENT) */
int storage_read_raw(uint16_t id, void *buf, size_t max);
/** Wert speichern. @return NVM_OK, NVM_PENDING oder Fehler */
int storage_save_raw(uint16_t id, const void *data, size_t len);

#endif
