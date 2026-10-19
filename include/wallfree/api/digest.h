/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP-Digest-Authentifizierung (RFC 7616) mit MD5 und qop=auth, wie sie der
 * evcc-Treiber warp-ws ueber icholy/digest verwendet (SOLL-API-07).
 *
 * Gespeichert wird nur HA1 = MD5(Benutzer ":" Realm ":" Passwort).
 * Nonce = 8 Hex-Zeichen Zeitstempel + 16 Hex-Zeichen MD5(Geheimnis, Zeitstempel);
 * das Geheimnis wird bei jedem Start neu gewuerfelt. Nonces aus einem
 * frueheren Start gelten als veraltet (stale=true: der Client meldet sich
 * ohne Rueckfrage neu an). Innerhalb eines Starts gibt es keine Ablaufzeit
 * und keine Pruefung des Zaehlers nc (Festlegung, siehe docs/implementierung.md).
 */
#ifndef WALLFREE_API_DIGEST_H_
#define WALLFREE_API_DIGEST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- MD5 (RFC 1321) ---- */

struct wf_md5 {
	uint32_t s[4];
	uint64_t n;       /**< Bytes gesamt */
	uint8_t buf[64];
};

void wf_md5_init(struct wf_md5 *c);
void wf_md5_update(struct wf_md5 *c, const void *data, size_t len);
void wf_md5_final(struct wf_md5 *c, uint8_t out[16]);
/** Hex-Darstellung (32 Zeichen + NUL) */
void wf_hex(const uint8_t *in, size_t len, char *out);

/* ---- Digest ---- */

#define WF_DIGEST_REALM   "wallfree"
#define WF_DIGEST_SECRET  16
#define WF_DIGEST_USER_MAX 32

struct wf_digest_cfg {
	const char *realm;
	const char *user;
	uint8_t ha1[16];
	uint8_t secret[WF_DIGEST_SECRET];
};

enum wf_auth {
	WF_AUTH_OK = 0,
	WF_AUTH_MISSING,  /**< keine Anmeldung -> 401 mit Challenge */
	WF_AUTH_BAD,      /**< falsch -> 401 mit Challenge */
	WF_AUTH_STALE,    /**< Nonce nicht von diesem Start -> 401 mit stale=true */
};

/** HA1 aus Benutzer, Realm und Passwort. */
void wf_digest_ha1(const char *user, const char *realm, const char *password, uint8_t ha1[16]);

/**
 * WWW-Authenticate-Wert erzeugen.
 * @param t Zeitstempel (z. B. Laufzeit in ms) fuer die Nonce
 * @return Laenge oder -1, wenn @p out zu klein ist
 */
int wf_digest_challenge(const struct wf_digest_cfg *cfg, uint32_t t, bool stale, char *out,
			size_t size);

/**
 * Authorization-Kopfzeile pruefen.
 * @param hdr Wert der Kopfzeile oder NULL
 * @param method z. B. "GET"
 * @param uri angefragte URI (muss dem Parameter uri entsprechen)
 */
enum wf_auth wf_digest_check(const struct wf_digest_cfg *cfg, const char *hdr, const char *method,
			     const char *uri);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_DIGEST_H_ */
