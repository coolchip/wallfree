/* SPDX-License-Identifier: Apache-2.0 */
/* Zugangsdaten fuer Web/API, siehe auth.h. Nur gebaut. */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include "auth.h"
#include "storage.h"

LOG_MODULE_REGISTER(auth, LOG_LEVEL_INF);

static char user[WF_DIGEST_USER_MAX + 1];
static struct wf_digest_cfg cfg;
static bool factory_credentials;

void auth_init(void)
{
	uint8_t rec[16 + WF_DIGEST_USER_MAX];
	int n = storage_read_raw(STORAGE_ID_WEB_CRED, rec, sizeof(rec));

	cfg.realm = WF_DIGEST_REALM;
	cfg.user = user;
	if (n > 16) {
		memcpy(cfg.ha1, rec, 16);
		memcpy(user, rec + 16, (size_t)n - 16);
		user[n - 16] = '\0';
		factory_credentials = false;
	} else {
		strcpy(user, AUTH_DEFAULT_USER);
		wf_digest_ha1(AUTH_DEFAULT_USER, WF_DIGEST_REALM, AUTH_DEFAULT_PASSWORD, cfg.ha1);
		factory_credentials = true;
		LOG_WRN("Werkszugang aktiv: Passwortaenderung erforderlich");
	}
	/* Nonces gelten nur bis zum naechsten Start */
	if (sys_csrand_get(cfg.secret, sizeof(cfg.secret)) != 0) {
		sys_rand_get(cfg.secret, sizeof(cfg.secret));
	}
}

enum wf_auth auth_check(const char *hdr, const char *method, const char *uri)
{
	return wf_digest_check(&cfg, hdr, method, uri);
}

int auth_challenge(bool stale, char *out, size_t size)
{
	return wf_digest_challenge(&cfg, k_uptime_get_32(), stale, out, size);
}

bool auth_must_change(void)
{
	return factory_credentials;
}

const char *auth_change(const char *new_user, const char *password)
{
	size_t ul = strlen(new_user);
	size_t pl = strlen(password);

	if (ul == 0 || ul > WF_DIGEST_USER_MAX) {
		return "Benutzername: 1..32 Zeichen";
	}
	for (size_t i = 0; i < ul; i++) {
		char c = new_user[i];

		if (c < 0x21 || c > 0x7E || c == '"' || c == ':' || c == '\\') {
			return "Benutzername: nur druckbare Zeichen ohne Leerzeichen, '\"', ':', '\\'";
		}
	}
	if (pl < AUTH_PW_MIN || pl > AUTH_PW_MAX) {
		return "Passwort: 8..64 Zeichen";
	}
	if (strcmp(password, AUTH_DEFAULT_PASSWORD) == 0) {
		return "Passwort darf nicht das Werkspasswort sein";
	}
	uint8_t rec[16 + WF_DIGEST_USER_MAX];

	wf_digest_ha1(new_user, WF_DIGEST_REALM, password, rec);
	memcpy(rec + 16, new_user, ul);
	/* ein Eintrag: Benutzer und HA1 aendern sich nur gemeinsam */
	if (storage_save_raw(STORAGE_ID_WEB_CRED, rec, 16 + ul) < 0) {
		return "Speichern fehlgeschlagen";
	}
	memcpy(user, new_user, ul + 1);
	memcpy(cfg.ha1, rec, 16);
	factory_credentials = false;
	LOG_INF("Zugangsdaten geaendert");
	return NULL;
}
