/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_HTTP_API_H_
#define WALLFREE_APP_HTTP_API_H_

/** Zugangsdaten laden, WebSocket-Thread und HTTP-Server (Port 80) starten. */
int http_api_start(void);

/** WebSocket-Thread wecken (neue Ereignisse oder geaenderte Werte). */
void http_api_notify(void);

/** Uhrzeit fuer das Protokoll: ms seit 1970 bei Laufzeit 0, 0 = unbekannt (SNTP) */
void http_api_set_wall_base(int64_t wall_base);

#endif
