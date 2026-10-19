/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zugangsdaten fuer Web/API (SOLL-API-07, F20): HTTP-Digest MD5, gespeichert
 * wird nur HA1. Werkszustand: Benutzer "admin", Passwort "wallfree"; bis zur
 * Aenderung beantwortet die API nur POST /credentials.
 * Alle Funktionen laufen im Thread des HTTP-Servers.
 */
#ifndef WALLFREE_APP_AUTH_H_
#define WALLFREE_APP_AUTH_H_

#include <stdbool.h>
#include <stddef.h>
#include <wallfree/api/digest.h>

#define AUTH_DEFAULT_USER     "admin"
#define AUTH_DEFAULT_PASSWORD "wallfree"
#define AUTH_PW_MIN 8
#define AUTH_PW_MAX 64

/** Gespeicherte Zugangsdaten laden, Nonce-Geheimnis wuerfeln. */
void auth_init(void);

enum wf_auth auth_check(const char *hdr, const char *method, const char *uri);

/** WWW-Authenticate-Wert. @return Laenge oder -1 */
int auth_challenge(bool stale, char *out, size_t size);

/** Werkszugang aktiv: Passwortaenderung erzwingen. */
bool auth_must_change(void);

/**
 * Zugangsdaten aendern (Benutzer 1..32 druckbare Zeichen ohne '"', ':', '\\';
 * Passwort 8..64 Zeichen, nicht das Werkspasswort).
 * @return NULL bei Erfolg, sonst Klartext des Fehlers
 */
const char *auth_change(const char *user, const char *password);

#endif
