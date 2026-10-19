/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_HTTP_API_H_
#define WALLFREE_APP_HTTP_API_H_

/** Zugangsdaten laden, WebSocket-Thread und HTTP-Server (Port 80) starten. */
int http_api_start(void);

/** WebSocket-Thread wecken (neue Ereignisse oder geaenderte Werte). */
void http_api_notify(void);

#endif
