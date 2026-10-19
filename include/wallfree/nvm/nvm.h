/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Parameterspeicher in einer einzigen Loescheinheit (Soll-Spezifikation 13.1,
 * SOLL-NVM-01..05). Reines C99 ohne Zephyr-Abhaengigkeit, auf dem PC testbar.
 *
 * Format (Version 1, alle Zahlen little-endian):
 *
 *   Offset 0: Bereichskopf, 8 Byte: Kennung "WFPS" (0x53504657), dann der
 *             Abschluss Version (u16), ~Version (u16). Nach dem Loeschen wird
 *             zuerst die Kennung, zuletzt der Abschluss geschrieben. Kennung
 *             ohne Abschluss oder ein nicht leerer Bereich ohne Kennung heisst:
 *             Verdichten unterbrochen (SOLL-NVM-05). Nur ein Ausfall genau
 *             zwischen Loeschende und erstem Byte sieht aus wie ein Erststart.
 *   Offset 8: Eintraege, je auf 4 Byte ausgerichtet:
 *             Kopf u32 = Kennung (Bit 0..15) | Laenge (Bit 16..23) |
 *                        CRC-8 ueber Kennung und Laenge (Bit 24..31)
 *             Daten (Laenge Byte, mit 0xFF auf 4 Byte aufgefuellt)
 *             CRC-32 (IEEE) ueber Kopf und Daten (ohne Fuellbytes)
 *
 * Ein Eintrag wird mit einem Schreibvorgang in aufsteigender Adressfolge
 * angehaengt (SOLL-NVM-03). Der juengste gueltige Eintrag einer Kennung
 * gilt; Eintraege mit falscher CRC-32 werden uebersprungen. Ein Kopf mit
 * falscher CRC-8 (Stromausfall waehrend des Kopfschreibens) beendet den
 * lesbaren Teil; der Rest gilt als verbraucht, bis verdichtet wird.
 *
 * Loeschen (Verdichten, Formatieren) blockiert den Flash-Bus fuer Sekunden
 * und ist nur im sicheren Zustand erlaubt (SOLL-NVM-04); diese Bedingung
 * prueft der Aufrufer.
 */
#ifndef WALLFREE_NVM_NVM_H_
#define WALLFREE_NVM_NVM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NVM_MAGIC        0x53504657u /* "WFPS" */
#define NVM_VERSION      1u
#define NVM_HDR_SIZE     8u
#define NVM_MAX_LEN      64u   /**< groesster Wert in Byte */
#define NVM_PENDING_MAX  16u   /**< Werte, die bei vollem Bereich im RAM warten */
#define NVM_ID_INVALID   0xFFFFu

/** Zugriff auf den Flash-Bereich (Offsets relativ zum Bereichsanfang). */
struct nvm_flash {
	int (*read)(void *ctx, uint32_t off, void *buf, size_t len);
	int (*write)(void *ctx, uint32_t off, const void *buf, size_t len);
	int (*erase)(void *ctx); /**< ganzer Bereich */
	void *ctx;
	uint32_t size;
};

enum nvm_state {
	NVM_ST_UNMOUNTED = 0,
	NVM_ST_OK,       /**< Bereichskopf gueltig */
	NVM_ST_EMPTY,    /**< Bereich geloescht (erster Start); Kopf folgt beim ersten Schreiben */
	NVM_ST_INVALID,  /**< weder leer noch gueltig: Verdichten unterbrochen oder fremdes Format */
};

enum nvm_err {
	NVM_OK = 0,
	NVM_PENDING = 1,      /**< kein Fehler: Wert vorerst nur im RAM (Speichern ausstehend) */
	NVM_E_ARG = -1,       /**< Kennung oder Laenge unzulaessig */
	NVM_E_IO = -2,        /**< Flash-Zugriff fehlgeschlagen */
	NVM_E_FULL = -3,      /**< auch der RAM-Puffer ist voll */
	NVM_E_NOSPACE = -4,   /**< Verdichten: Daten passen nicht in den Arbeitspuffer/Bereich */
	NVM_E_NOENT = -5,     /**< Kennung nicht gespeichert */
	NVM_E_STATE = -6,     /**< nicht eingehaengt */
};

struct nvm_pending {
	uint16_t id;
	uint8_t len;
	uint8_t data[NVM_MAX_LEN];
};

struct nvm {
	const struct nvm_flash *fl;
	enum nvm_state state;
	uint32_t end;          /**< Ende der lesbaren Eintraege = naechste freie Stelle */
	bool damaged;          /**< unbrauchbarer Rest ab end (unterbrochenes Schreiben) */
	uint8_t npend;
	struct nvm_pending pend[NVM_PENDING_MAX];
};

/**
 * Bereich einhaengen: Kopf pruefen, Schreibposition suchen.
 * @return NVM_OK (auch bei EMPTY/INVALID, siehe n->state) oder NVM_E_IO.
 */
int nvm_mount(struct nvm *n, const struct nvm_flash *fl);

/** Juengsten Wert lesen (RAM-Puffer zuerst). @return Laenge oder Fehlercode. */
int nvm_read(struct nvm *n, uint16_t id, void *buf, size_t maxlen);

/**
 * Wert speichern. Unveraenderte Werte erzeugen keinen Schreibvorgang.
 * Passt der Eintrag nicht mehr (oder ist der Bereich ungueltig), wird er im
 * RAM vorgehalten. Es wird nie geloescht.
 * @return NVM_OK, NVM_PENDING oder Fehlercode.
 */
int nvm_write(struct nvm *n, uint16_t id, const void *data, size_t len);

/** Rueckruf je gueltigem Eintrag in Speicherreihenfolge (aeltester zuerst). */
typedef void (*nvm_visit_t)(void *ctx, uint16_t id, const void *data, size_t len);

/**
 * Alle gueltigen Eintraege aufsuchen, danach die im RAM wartenden. Wer die
 * Werte der Reihe nach uebernimmt, hat am Ende jeweils den juengsten.
 */
int nvm_foreach(struct nvm *n, nvm_visit_t fn, void *ctx);

/** Verdichten noetig (RAM-Werte, beschaedigter Rest, Platz unter @p reserve)? */
bool nvm_needs_compact(const struct nvm *n, uint32_t reserve);

/**
 * Verdichten: juengste Werte und RAM-Werte in @p scratch sammeln, Bereich
 * loeschen, Eintraege schreiben, Kopf zuletzt. Passen die Daten nicht in
 * @p scratch, wird nichts geloescht. Nur im sicheren Zustand aufrufen.
 */
int nvm_compact(struct nvm *n, void *scratch, size_t scratch_size);

/** Loeschen und leeren Bereich mit Kopf anlegen (Werkseinstellungen). */
int nvm_format(struct nvm *n);

/** Belegte und freie Bytes. */
uint32_t nvm_used(const struct nvm *n);
uint32_t nvm_free(const struct nvm *n);

/* Pruefsummen (auch fuer Tests) */
uint8_t nvm_crc8(const uint8_t *p, size_t len);
uint32_t nvm_crc32(uint32_t crc, const uint8_t *p, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_NVM_NVM_H_ */
