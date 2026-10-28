// SPDX-License-Identifier: Apache-2.0
// Weboberflaeche Wallfree (SOLL-API-31): nutzt nur die HTTP-/WebSocket-API.
import { html, render, useState, useEffect, useRef } from './vendor.js';

// ------------------------------------------------------------ API

// Firmware-Update: Der Bootloader bricht einen Upload bei jeder anderen Anfrage ab (ein
// gemeinsamer Zustand fuer alle Verbindungen, wallbe2 docs/bootloader.md). Waehrend des
// Updates stellt die Seite deshalb keine Anfragen ausser denen des Update-Ablaufs.
let quiet = false;

// Anfragen nacheinander: Die Steuerung hat nur 4 HTTP-Verbindungsplaetze, und jede Verbindung
// haelt ihren nach der letzten Anfrage noch 5 s. Parallele Abfragen der Karten oeffnete der
// Browser auf mehreren Verbindungen; so braucht ein Tab einen Platz (dazu den WebSocket).
let queue = Promise.resolve();
function api(path, opts) {
	const run = queue.then(() => request(path, opts));
	queue = run.catch(() => {});
	return run;
}

async function request(path, opts) {
	if (quiet) throw new Error('Update läuft');
	const r = await fetch(path, Object.assign({ cache: 'no-store', signal: AbortSignal.timeout(10000) }, opts));
	let j = null;
	try { j = await r.json(); } catch (e) { /* leer */ }
	if (!r.ok) {
		const err = new Error((j && j.error) || r.status + ' ' + r.statusText);
		err.status = r.status;
		err.body = j;
		throw err;
	}
	return j;
}
const post = (path, body) => api(path, {
	method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
});
// Konsole (SOLL-SH-19): Kommandozeile hin, Ausgabe als Text zurueck
const shell = (line) => {
	const run = queue.then(async () => {
		if (quiet) throw new Error('Update läuft');
		const r = await fetch('/system/shell', { method: 'POST', body: line, cache: 'no-store',
			signal: AbortSignal.timeout(20000) });
		const t = await r.text();
		if (!r.ok) throw new Error(r.status === 413 ? 'Kommandozeile zu lang' : t || r.status + ' ' + r.statusText);
		return t;
	});
	queue = run.catch(() => {});
	return run;
};
// Periodisch laden, nur solange die Seite sichtbar ist, beim Zurueckkehren sofort. Gedrosselte
// Hintergrund-Tabs wachen im Handy-Browser alle zugleich im Minutentakt auf; ihre Abfragen
// belegten dann die wenigen Verbindungsplaetze der Steuerung (implementierung.md).
function useEvery(load, ms, deps = []) {
	useEffect(() => {
		const run = () => { if (!document.hidden) load(); };
		run();
		const t = setInterval(run, ms);
		document.addEventListener('visibilitychange', run);
		return () => { clearInterval(t); document.removeEventListener('visibilitychange', run); };
	}, deps);
}
// Abruf mit Browserzeit zur Mitte der Anfrage (F32: die Steuerung hat keine Uhr; ihre
// Zeiten zaehlen ab Start und werden bei jedem Abruf neu gegen die Browseruhr abgeglichen)
async function timed(path) {
	const t0 = Date.now(), j = await api(path);
	return [j, (t0 + Date.now()) / 2];
}

// ------------------------------------------------------------ Texte

const CHARGER = ['Kein Fahrzeug', 'Wartet auf Freigabe', 'Bereit', 'Lädt', 'Fehler'];
const IEC = ['A – kein Fahrzeug', 'B – verbunden', 'C – lädt', 'D – lädt (Lüftung)', 'E/F – Fehler'];
const ERRSTATE = ['Kein Fehler', 'Fehler (Details: Status)', 'Schalter-/Konfigurationsfehler',
	'Fehlerstrom (RCM)', 'Schützfehler', 'Kommunikationsfehler'];
const LOCK = ['Initialisierung', 'Entriegelt', 'Verriegelt …', 'Verriegelt', 'Entriegelt …', 'Fehler'];
const STATES = ['S0 Start', 'S1 Bereit', 'S2 Verriegeln', 'S3 Warten', 'S4 Selbsttest', 'S5 PWM an',
	'S6 Laden', 'S7 Beenden', 'S8 Entriegeln', 'S9 Fehler', 'S10 Nicht verfügbar'];
const ERRBITS = ['Kabel abgewiesen', 'PP ungültig', 'CP ungültig', 'Verriegeln', 'Entriegeln',
	'LD verloren', 'Überstrom', 'Zähler', 'Zustand D', 'Schütz', 'Diode', 'Eingang',
	'RCM-Selbsttest', 'RCM-Sensor', 'RCM 6 mA DC', 'RCM 30 mA AC', 'Drehschalter', 'Konfiguration',
	'Intern'];
const errText = (m) => ERRBITS.filter((_, i) => m & (1 << i)).join(', ') || 'keine';
const amps = (ma) => (ma / 1000).toLocaleString('de-DE', { maximumFractionDigits: 1 }) + ' A';
const num = (v, d) => v.toLocaleString('de-DE', { minimumFractionDigits: d, maximumFractionDigits: d });

// Zaehler (SOLL-MTR-30..32): WARP-Kennungen der Messwerte
const MV = { 1: ['Spannung L1', 'V', 1], 2: ['Spannung L2', 'V', 1], 3: ['Spannung L3', 'V', 1],
	13: ['Strom L1', 'A', 2], 17: ['Strom L2', 'A', 2], 21: ['Strom L3', 'A', 2],
	39: ['Leistung L1', 'W', 0], 48: ['Leistung L2', 'W', 0], 57: ['Leistung L3', 'W', 0],
	74: ['Leistung gesamt', 'W', 0], 364: ['Frequenz', 'Hz', 2], 209: ['Energie Bezug', 'kWh', 3],
	211: ['Energie Einspeisung', 'kWh', 3] };
const METER_STATUS = { off: 'kein Zähler', connecting: 'verbinde …', ok: 'verbunden', error: 'gestört' };
const METER_TYPES = ['kein Zähler', 'ABB B23', 'Eastron SDM630', 'Eastron SDM72D-M V2',
	'Eastron SDM72D-M (V1, nur Summen)'];
const FRAMING = ['8N1', '8E1', '8O1', '8N2'];
// Namen der Funktionscodes nur fuer die Anzeige; das Geraet speichert die Zahl
// (Soll-Spezifikation 9.1/9.3)
const OUT_CODES = ['nie', 'Status A ohne Fehler', 'Status B ohne Fehler', 'B mit PWM, ohne Fehler',
	'B ohne PWM, ohne Fehler', 'Status C', 'Status D', 'Status E oder Fehler (nicht F)', 'Status F',
	'A oder B ohne Fehler', 'A, oder B mit PWM, ohne Fehler', 'A, oder B ohne PWM, ohne Fehler',
	'A/B ohne Fehler oder C', 'A/B ohne Fehler oder D', 'A/B ohne Fehler, C oder D', 'E, F oder Fehler',
	'C oder D (lädt)', 'PWM an', 'gültiger Kabelwert', 'kein gültiger Kabelwert', 'Kabel 13 A', 'Kabel 20 A',
	'Kabel 32 A', 'Kabel 63 A', 'Kabel 13 oder 20 A', 'Kabel 13, 20 oder 32 A', 'Kabel abgewiesen',
	'Schütz ein', 'Belüftung (Laden mit Status D)', 'Verriegelungsanforderung', 'Schnittstellen-Ausgang 1',
	'Schnittstellen-Ausgang 2', 'Schnittstellen-Ausgang 3', 'Schnittstellen-Ausgang 4', 'Überstromfehler',
	'Schützfehler', 'Fehler Zustand D', 'B ohne Fehler, C oder D', 'A/B bei aktivem Eingangscode 19',
	'Freigabe und C/D (oder B nach Stromsperre)', 'wie 9, beim Start blinkend',
	'A/B ohne Fehler, PWM aus', 'Fahrzeug pausiert (B mit PWM beim Laden)'];
const IN_CODES = ['keine', 'EN Freigabe (Dauersignal)', 'XR Verfügbarkeit', 'LD Verriegelungsrückmeldung (Dauersignal)',
	'ML Verriegelungswunsch (Dauersignal)', 'Schützrückmeldung Schließer (NO)', 'Schützrückmeldung Öffner (NC)',
	'5-%-Modus', ...[6, 10, 13, 16, 20, 32, 63, 70].map((a) => 'Stromgrenze ' + a + ' A'),
	'EN Freigabe (Taster)', 'ML Verriegelungswunsch (Taster)', 'EN Freigabe (wie 1)',
	'Maximalstrom, Vorgabe der Schnittstelle ignorieren', 'Pause (Stromsperre)', 'Fehlereingang',
	'LD Verriegelungsrückmeldung (Impuls)'];
const PARAM_OPTS = {
	contactor_monitor: ['aus', 'Spannung am Ladepunkt (Sense)', 'Rückmeldekontakt Schließer (NO)',
		'Rückmeldekontakt Öffner (NC)', 'Energiezähler'],
	wakeup_mode: ['einmal je Steckvorgang', 'auch nach Stromsperre'],
};
const paramOpts = (name) => name.startsWith('out_function_') ? OUT_CODES
	: name.startsWith('in_function_') ? IN_CODES : PARAM_OPTS[name];

// Begriffe: Erklaerung beim Zeigen (abbr) und als aufklappbare Liste
const TERMS = [
	['LD', 'Digitaleingang, werkseitig Rückmeldung der Steckerverriegelung'],
	['EN', 'Digitaleingang, werkseitig Freigabe (z. B. Schlüsselschalter, mit D7)'],
	['ML', 'Digitaleingang, werkseitig Verriegelungswunsch (mit D9)'],
	['XR', 'Digitaleingang, werkseitig Verfügbarkeit (mit D8)'],
	['IN', 'Digitaleingang, werkseitig Stromgrenze 16 A'],
	['ER', 'Digitalausgang, werkseitig Fehler'],
	['LR', 'Digitalausgang, werkseitig Verriegelungsanforderung'],
	['VR', 'Digitalausgang, werkseitig Fahrzeug lädt (C oder D)'],
	['CR', 'Digitalausgang, werkseitig PWM an'],
	['CP', 'Control Pilot: Signalleitung zum Fahrzeug; Status A kein Fahrzeug, B angeschlossen, C lädt, D lädt mit Belüftung, E Fehler, F nicht verfügbar'],
	['PWM', 'Rechtecksignal auf CP; der Tastgrad gibt dem Fahrzeug den erlaubten Strom vor'],
	['PP', 'Proximity Pilot: Widerstand im Stecker, kodiert die Belastbarkeit des Ladekabels'],
	['RCM', 'Fehlerstromüberwachung'],
	['NO', 'Schließer (normally open)'],
	['NC', 'Öffner (normally closed)'],
	['D1..D10', 'DIP-Schalter der Steuerung (Betriebsart), z. B. D7 Freigabe über EN, D8 Verfügbarkeit über XR, D9 Verriegelung über ML, D10 Steuerung über die Schnittstelle'],
	['S0..S10', 'Zustände der Ladelogik, z. B. S1 Bereit, S6 Laden, S10 nicht verfügbar'],
];
const TERM_RE = /\b(LD|EN|ML|XR|IN|ER|LR|VR|CR|CP|PWM|PP|RCM|NO|NC|D(?:10|[1-9])|S(?:10|[0-9]))\b/g;
const termOf = (w) => (TERMS.find(([k]) => k === w) || TERMS.find(([k]) =>
	k.includes('..') && k[0] === w[0] && /^[DS]\d/.test(w)) || [])[1];
function Terms({ text }) {
	const out = [];
	let last = 0;
	for (const m of text.matchAll(TERM_RE)) {
		const t = termOf(m[1]);
		if (!t) continue;
		out.push(text.slice(last, m.index), html`<abbr title=${t}>${m[1]}</abbr>`);
		last = m.index + m[1].length;
	}
	out.push(text.slice(last));
	return out;
}
function Glossary() {
	return html`<details class="card terms"><summary>Begriffe</summary><table>
		${TERMS.map(([k, t]) => html`<tr><td><b>${k}</b></td><td>${t}</td></tr>`)}</table></details>`;
}

// Klemmen in Bitreihenfolge der Statuswerte inputs/outputs
const DINS = ['LD', 'EN', 'ML', 'XR', 'IN'];
const DOUTS = ['ER', 'LR', 'VR', 'CR'];
const MQTT_STATES = ['aus', 'nicht verbunden', 'verbunden', 'vom Broker abgelehnt'];
// Wert zur Kennung oder null (nicht vorhanden, ungueltig, veraltet)
const mval = (m, id) => {
	if (!m || !m.ids || !m.values) return null;
	const i = m.ids.indexOf(id);
	return i < 0 || m.values[i] === null || m.values[i] === undefined ? null : m.values[i];
};

// ------------------------------------------------------------ Live-Daten

// WebSocket /ws; schliesst der Server sofort (z. B. Browser sendet keine
// Anmeldung beim Aufbau), wird stattdessen alle 2 s abgefragt und alle 30 s
// erneut ein WebSocket versucht. Die Steuerung sendet evse/state mindestens alle
// 2 s; ohne Daten seit OFFLINE_MS gilt die Verbindung als getrennt.
const OFFLINE_MS = 5000;

function useLive() {
	const [state, setState] = useState(null);
	const [ext, setExt] = useState(null);
	const [limits, setLimits] = useState(null); // Topic evse/limits
	const [mode, setMode] = useState(null); // 'live' (WebSocket) oder 'poll'
	const [online, setOnline] = useState(null); // null = noch keine Antwort
	const [events, setEvents] = useState([]); // Topic evse/event (SOLL-ERR-10), juengste zuletzt
	const [meter, setMeter] = useState(null); // meters/0/*: {ids, values, state}; null = kein Zaehler

	useEffect(() => {
		let ws = null, timer = null, retry = null, stop = false, busy = false, lastOk = 0;
		const ok = () => { lastOk = Date.now(); setOnline(true); };
		const get = (path) => api(path, { signal: AbortSignal.timeout(OFFLINE_MS - 1000) });
		const poll = async () => {
			if (busy) return;
			busy = true;
			try {
				setState(await get('/evse/state'));
				ok();
				setExt(await get('/evse/external_current'));
				setLimits(await get('/evse/limits'));
			} catch (e) { /* naechster Versuch */ }
			try {
				const st = await get('/meters/0/state');
				const ids = await get('/meters/0/value_ids');
				setMeter({ ids, values: await get('/meters/0/values'), state: st });
			} catch (e) { if (e.status === 404) setMeter(null); }
			busy = false;
		};
		const startPoll = () => {
			if (timer) return;
			setMode('poll');
			poll();
			timer = setInterval(poll, 2000);
		};
		const later = (ms) => { clearTimeout(retry); retry = setTimeout(connect, ms); };
		// Verbindung verloren: kurzlebig oder ohne Daten -> Abfrage, sonst bald neu verbinden
		const lost = (opened, got) => {
			ws = null;
			if (stop) return;
			if (!got || Date.now() - opened < 3000) { startPoll(); later(30000); } else later(3000);
		};
		const connect = () => {
			if (ws || document.hidden) return;
			if (quiet) { later(3000); return; }
			const opened = Date.now();
			let got = false;
			const w = ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
			w.onmessage = (ev) => {
				if (!got && timer) { clearInterval(timer); timer = null; }
				got = true;
				ok();
				setMode('live');
				for (const line of ev.data.split('\n')) {
					if (!line.trim()) continue;
					const m = JSON.parse(line);
					if (m.topic === 'evse/state') setState(m.payload);
					if (m.topic === 'evse/external_current') setExt(m.payload);
					if (m.topic === 'evse/limits') setLimits(m.payload);
					if (m.topic === 'evse/event') setEvents((l) => l.concat([m.payload]).slice(-40));
					if (m.topic === 'meters/0/value_ids') setMeter((o) => ({ ...o, ids: m.payload }));
					if (m.topic === 'meters/0/values') setMeter((o) => ({ ...o, values: m.payload }));
					if (m.topic === 'meters/0/state') setMeter((o) => ({ ...o, state: m.payload }));
				}
			};
			w.onclose = () => { if (ws === w) lost(opened, got); };
			w.drop = () => { w.onclose = null; w.close(); lost(opened, got); };
		};
		// Im Hintergrund weder WebSocket noch Abfrage (useEvery); sichtbar wieder verbinden
		const vis = () => {
			if (document.hidden) {
				clearInterval(timer); timer = null; clearTimeout(retry);
				if (ws) { const w = ws; ws = null; w.onclose = null; w.close(); }
			} else if (!ws && !timer) {
				lastOk = Date.now();
				connect();
			}
		};
		document.addEventListener('visibilitychange', vis);
		// Ein haengender WebSocket (Kabel gezogen) meldet sich nicht von selbst
		const watch = setInterval(() => {
			if (document.hidden || !lastOk || Date.now() - lastOk < OFFLINE_MS) return;
			setOnline(false);
			if (ws && ws.readyState === WebSocket.OPEN) ws.drop();
		}, 1000);
		if (!document.hidden) connect();
		return () => {
			stop = true; if (ws) ws.close(); clearInterval(timer); clearInterval(watch); clearTimeout(retry);
			document.removeEventListener('visibilitychange', vis);
		};
	}, []);
	return { state, ext, mode, online, setExt, events, limits, meter };
}

// ------------------------------------------------------------ Bausteine

function Card({ title, children }) {
	return html`<section class="card"><h2>${title}</h2>${children}</section>`;
}

function Msg({ m }) {
	if (!m) return null;
	return html`<div class="msg ${m.ok ? 'ok' : 'bad'}">${m.text}</div>`;
}

// ------------------------------------------------------------ Ereignisse, Ereignisprotokoll

// Resetgrund und genauerer Ausloeser (SOLL-RT-14), Texte fuer Protokoll und System
const RESET = { power_on: 'Einschalten', user_restart: 'Neustart auf Anforderung', update: 'Firmware-Update',
	factory_reset: 'Werksreset', reset_pin: 'Reset-Taster', brownout: 'Spannungseinbruch',
	watchdog: 'Watchdog', window_watchdog: 'Watchdog', supervision_limit: 'Überwachung',
	interface_hang: 'Schnittstelle hing', fault: 'Programmfehler', software: 'Software', low_power: 'Energiesparen',
	unknown: 'unbekannt' };
const RESET_DETAIL = { charger_thread: 'Ladelogik reagierte nicht', measurement_blocks: 'Messung blieb aus',
	storage_thread: 'Speicher-Thread', websocket_thread: 'WebSocket-Thread', system_workqueue: 'System-Workqueue',
	http_server: 'HTTP-Server', fault: 'Prozessorausnahme' };
const resetText = (r, d) => (RESET[r] || r) + (d ? ' (' + (RESET_DETAIL[d] || d) + ')' : '');

function evText(e) {
	switch (e.type) {
	case 'state': return 'Zustand ' + (STATES[e.from] || e.from) + ' → ' + (STATES[e.to] || e.to);
	case 'error': return [e.set.length && 'Fehler: ' + e.set.join(', '),
		e.cleared.length && 'behoben: ' + e.cleared.join(', ')].filter(Boolean).join('; ');
	case 'lock': return 'Verriegelung: ' + (LOCK[e.lock_state] || e.lock_state);
	case 'contactor': return 'Schütz ' + (e.on ? 'ein' : 'aus');
	case 'meter': return 'Zähler ' + (METER_STATUS[e.status] || e.status) +
		(e.energy_decreased ? ', Zählerstand gesunken' : '');
	case 'update': return 'Firmware-Update ' + (e.ok ? 'erfolgreich' : 'fehlgeschlagen');
	case 'boot': return 'Start: ' + resetText(e.reason, e.detail) + (e.pc ? ', PC ' + e.pc : '');
	}
	return e.type;
}

// Vorgeschichte aus GET /system/log (SOLL-ERR-11/12, auch aus frueheren Starts), danach live
// ueber den WebSocket; ohne WebSocket alle 5 s neu abfragen. Zeiten: mit Uhrzeit der Steuerung
// (wall_ms) genau; sonst fuer den laufenden Start ms seit Start, umgerechnet ueber den
// Startzeitpunkt in Browserzeit (bei jedem Abruf und mindestens jede Minute ueber /system/info,
// ungefaehr); Eintraege frueherer Starts ohne Uhrzeit haben keine.
function EventLog({ live, all }) {
	const [log, setLog] = useState(null);
	const [off, setOff] = useState(null);
	const [boot, setBoot] = useState(0);
	const [synced, setSynced] = useState(false);
	const [err, setErr] = useState(null);
	const [msg, setMsg] = useState(null);
	const load = async () => {
		try {
			const l = await api('/system/log');
			const [i, at] = await timed('/system/info');
			// mit Zeitserver die Uhr der Steuerung, sonst Browserzeit minus Laufzeit (SOLL-TIME-03)
			setOff(i.time ? i.time.time_ms - i.uptime_ms : at - i.uptime_ms);
			setSynced(!!i.time);
			setBoot(l.boot);
			setLog(l.entries);
			setErr(null);
		} catch (e) { setErr(e.message); }
	};
	const polling = live.mode !== 'live';
	useEvery(load, polling ? 5000 : 60000, [polling]);
	if (!log) return err ? html`<${Card} title="Ereignisse"><p class="bad">${err}</p><//>` : null;
	// SOLL-ERR-13: was vor dem Loeschen live ankam, gehoert nicht mehr dazu
	const cut = log.filter((x) => x.kind === 'cleared' && x.boot === boot).map((x) => x.t_ms).pop() ?? -1;
	const known = Math.max(0, ...log.filter((x) => x.kind === 'event').map((x) => x.event.seq));
	let list = log.concat(live.events.filter((e) => e.seq > known && e.t_ms > cut)
		.map((e) => ({ kind: 'event', t_ms: e.t_ms, boot, event: e })));
	if (!all) list = list.filter((x) => x.kind === 'event').slice(-8);
	const today = new Date().toDateString();
	const fmtTime = (ms) => {
		const d = new Date(ms);
		return d.toDateString() === today ? d.toLocaleTimeString('de-DE') : d.toLocaleString('de-DE');
	};
	const time = (x) => (x.wall_ms ? fmtTime(x.wall_ms) : x.boot === boot ? fmtTime(off + x.t_ms) : 'unbekannte Zeit');
	const text = (x) => (x.kind === 'event' ? evText(x.event) : x.kind === 'cleared' ? 'Ereignisprotokoll gelöscht von ' + x.addr
		: 'Anmeldung fehlgeschlagen von ' + x.addr + (x.count > 1 ? ' (' + x.count + '×)' : ''));
	const clear = async () => {
		if (!confirm('Alle Einträge des Ereignisprotokolls löschen? Das Löschen selbst bleibt als Eintrag stehen.')) return;
		try {
			await post('/system/log/clear', {});
			setMsg(null);
			await load();
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	return html`<${Card} title=${all ? 'Ereignisprotokoll' : 'Letzte Ereignisse'}>
		${list.length === 0 ? html`<p class="muted">Keine Einträge.</p>` : html`<table>
			${list.slice().reverse().map((x) => html`<tr>
				<td class="num muted">${time(x)}</td>
				<td class=${x.kind === 'event' ? (x.event.type === 'error' && x.event.set.length ? 'bad' : '') : 'warn'}>${text(x)}</td>
			</tr>`)}
		</table>`}
		${all && html`<p class="muted msg">Letzte 48 Einträge, auch über Neustarts hinweg (nicht über einen Stromausfall). ${synced
			? 'Uhrzeiten vom Zeitserver.' : 'Ohne Zeitserver sind Uhrzeiten auf wenige Sekunden genau; Einträge früherer Starts haben keine (unbekannte Zeit).'}</p>`}
		${all && html`<div class="row"><button onClick=${clear}>Protokoll löschen</button></div>`}
		${msg && html`<${Msg} m=${msg} />`}
	<//>`;
}

// ------------------------------------------------------------ Uebersicht

// Ladestrom (SOLL-API-32): Vorgabe, Grenzen, erlaubter Strom (Tastgrad) und mit
// Zaehler der Messwert (hoechste Phase, nur Anzeige).
const LIMIT_TEXT = { external: 'Vorgabe', hardware: 'Maximum Drehschalter', cable: 'Kabel', input: 'Digitaleingang' };

function measured(meter) {
	const ph = [13, 17, 21].map((id) => mval(meter, id)).filter((v) => v !== null);
	return ph.length ? Math.max(...ph) * 1000 : null;
}

// Zeile unter dem Ring: was gerade begrenzt. @return [Text, CSS-Klasse]
function limitLine(l) {
	if (l.limited_by === 'pwm5') return ['5-%-Modus (digitale Kommunikation)', ''];
	if (l.limited_by === 'blocked') {
		return [l.external === 0 && !l.external_ignored ? 'Vorgabe: Laden aus' : 'Laden gesperrt', 'bad'];
	}
	const by = l.limited_by;
	const t = 'begrenzt durch ' + (LIMIT_TEXT[by] || by) + ' (' + amps(by === 'external' ? l.external : l[by]) + ')';
	return [t + (l.external_ignored ? ' · Vorgabe ignoriert (Digitaleingang)' : ''), by === 'external' ? '' : 'warn'];
}

// Ring ueber 270 Grad von 0 bis zum Maximum (Drehschalter bzw. abgesenkt)
const GR = 110, GX = 150, GY = 152;
const gxy = (a, r) => [GX + r * Math.cos(a * Math.PI / 180), GY + r * Math.sin(a * Math.PI / 180)];
const gpt = (a, r) => gxy(a, r).map((v) => v.toFixed(1)).join(' ');

function Gauge({ l, meter }) {
	const max = l.hardware / 1000 || 32;
	const ang = (a) => 135 + 270 * Math.min(Math.max(a, 0), max) / max;
	const arc = (a0, a1, r) => `M${gpt(ang(a0), r)} A${r} ${r} 0 ${ang(a1) - ang(a0) > 180 ? 1 : 0} 1 ${gpt(ang(a1), r)}`;
	const ticks = [0, 6, 10, 16, 20, 25, 32, 40, 63].filter((t) => t < max - 1.5).concat([max]);
	const ist = measured(meter), allowed = l.allowed / 1000;
	const p = mval(meter, 74);
	const marks = ['cable', 'input'].filter((k) => l[k] > 0 && l[k] < l.hardware);
	const set = l.external_ignored ? null : l.external / 1000;
	const [line, cls] = limitLine(l);
	return html`<div class="gauge">
		<svg viewBox="0 0 300 262" role="img" aria-label="Ladestrom">
			<path class="g-bg" d=${arc(0, max, GR)} />
			<path class="g-low" d=${arc(0, Math.min(6, max), GR)} />
			${allowed > 0 && html`<path class="g-on" d=${arc(0, allowed, GR)} />`}
			${ist > 0 && html`<path class="g-ist" d=${arc(0, ist / 1000, GR - 20)} />`}
			${ticks.map((t) => html`<g>
				<path class="g-tick" d=${'M' + gpt(ang(t), GR + 13) + ' L' + gpt(ang(t), GR + 18)} />
				<text class="g-tl" x=${gxy(ang(t), GR + 30)[0]} y=${gxy(ang(t), GR + 30)[1] + 4}>${t}</text></g>`)}
			${marks.map((k) => html`<path class=${'g-lim' + (l.limited_by === k ? ' act' : '')}
				d=${'M' + gpt(ang(l[k] / 1000), GR - 11) + ' L' + gpt(ang(l[k] / 1000), GR + 11)} />`)}
			${set !== null && html`<circle class=${'g-set' + (set === 0 ? ' off' : '')} r="10"
				cx=${gxy(ang(set), GR)[0]} cy=${gxy(ang(set), GR)[1]} />`}
			<text class="g-big" x=${GX} y=${GY - 8}>${amps(ist !== null ? ist : l.allowed)}</text>
			<text class="g-cap" x=${GX} y=${GY + 16}>${ist !== null ? 'Messwert' : 'erlaubt'}</text>
			<text class="g-sub" x=${GX} y=${GY + 38}>${ist !== null
				? (p !== null ? num(p / 1000, 1) + ' kW · ' : '') + 'erlaubt ' + amps(l.allowed)
				: meter ? 'kein gültiger Messwert' : ''}</text>
		</svg>
		<p class=${'gline ' + cls}>${line}</p>
	</div>`;
}

function CurrentControl({ ext, setExt, limits, meter }) {
	const [msg, setMsg] = useState(null);
	if (!limits) return null;
	const max = limits.hardware / 1000 || 32;
	const cur = Math.min(ext ? ext.current / 1000 : 0, max);
	const send = async (a) => {
		try {
			setExt(await post('/evse/external_current', { current: Math.round(a * 1000) }));
			setMsg(null);
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	const sel = (a) => (ext && ext.current === a * 1000 ? 'sel' : '');
	return html`<section class="card wide"><h2>Ladestrom</h2>
		<${Gauge} l=${limits} meter=${meter} />
		<div class="presets">
			<button onClick=${() => send(cur <= 6 ? 6 : Math.ceil(cur) - 1)}>−</button>
			${[6, 10, 16, 32].filter((a) => a <= max).map((a) =>
				html`<button class=${sel(a)} onClick=${() => send(a)}>${a} A</button>`)}
			<button onClick=${() => send(cur < 6 ? 6 : Math.min(Math.floor(cur) + 1, max))}>+</button>
			<button class=${ext && ext.current === 0 ? 'primary' : 'off'} onClick=${() => send(0)}>Laden aus</button>
		</div>
		<${Msg} m=${msg} />
		<p class="muted msg center">Eine externe Vorgabe (z. B. Energiemanagement) kann diesen Wert jederzeit überschreiben.</p>
		<p class="legend"><i class="sw on"></i>Erlaubt${meter ? html` <i class="sw ist"></i>Messwert` : ''}
			<i class="sw lim"></i>Grenze <i class="sw set"></i>Vorgabe</p>
	</section>`;
}

// Status: dieselben Werte als Zahlen
function CurrentTable({ l, meter }) {
	// welche Eingaenge eine Stromgrenze tragen (Eingangscodes 8..15), einmal beim Oeffnen
	const [ins, setIns] = useState('');
	useEffect(() => {
		api('/evse/params').then((v) => setIns(DINS.filter((n) => {
			const c = v['in_function_' + n.toLowerCase()];
			return c >= 8 && c <= 15;
		}).join(', '))).catch(() => {});
	}, []);
	if (!l) return null;
	const ist = measured(meter);
	const rows = [
		['external', 'Vorgabe', l.external_ignored ? 'ignoriert (Digitaleingang)' : l.external ? amps(l.external) : 'Laden aus'],
		['hardware', 'Maximum Drehschalter', amps(l.hardware)],
		['cable', 'Kabel', l.cable ? amps(l.cable) : '–'],
		['input', 'Stromgrenze über Digitaleingang' + (ins ? ' ' + ins : ''), l.input ? amps(l.input) : '–'],
	];
	return html`<${Card} title="Ladestrom"><table>
		${rows.map(([k, t, v]) => {
			const act = l.limited_by === k && k !== 'external';
			return html`<tr class=${act ? 'act' : ''}><td>${t}</td><td class="num">${v}${act ? ' · begrenzt' : ''}</td></tr>`;
		})}
		<tr><td><b>Erlaubt</b></td><td class="num"><b>${amps(l.allowed)}</b></td></tr>
		${meter && html`<tr><td>Messwert (höchste Phase)</td><td class="num">${ist !== null ? amps(ist) : 'ungültig'}</td></tr>`}
	</table><p class=${'msg ' + limitLine(l)[1]}>${limitLine(l)[0]}</p><//>`;
}

function Overview({ live }) {
	const s = live.state;
	if (!s) return html`<p class="muted">Warte auf Daten …</p>`;
	const cls = s.charger_state === 4 || s.error_state ? 'bad' : s.charger_state === 3 ? 'ok' : '';
	return html`<div class="cards">
		<${Card} title="Zustand">
			<div class="big ${cls}">${CHARGER[s.charger_state] || '?'}</div>
			<div class="muted">Fahrzeug: ${IEC[s.iec61851_state] || '?'}</div>
		<//>
		<${Card} title="Fahrzeug darf">
			<div class="big">${s.allowed_charging_current ? amps(s.allowed_charging_current) : '–'}</div>
			<div class="muted">Schütz ${s.contactor_state ? 'ein' : 'aus'}</div>
		<//>
		<${Card} title="Fehler">
			<div class="big ${s.error_state ? 'bad' : 'ok'}">${ERRSTATE[s.error_state] || s.error_state}</div>
			<div class="muted">Verriegelung: ${LOCK[s.lock_state] || '?'}</div>
		<//>
		<${MeterCard} m=${live.meter} />
		<${CurrentControl} ext=${live.ext} setExt=${live.setExt} limits=${live.limits} meter=${live.meter} />
	</div><${EventLog} live=${live} />`;
}

// Uebersicht: Leistung, Zaehlerstand, Zustand
function MeterCard({ m }) {
	if (!m) return html`<${Card} title="Zähler"><//>`; // ohne Zaehler leer, damit die Anordnung bleibt
	const p = mval(m, 74), e = mval(m, 209);
	const st = m.state ? m.state.status : 'connecting';
	return html`<${Card} title="Zähler">
		<div class="big">${p === null ? '–' : num(p / 1000, 2) + ' kW'}</div>
		<div class="muted">${e === null ? 'Zählerstand –' : 'Zählerstand ' + num(e, 1) + ' kWh'}</div>
		<div class=${st === 'ok' ? 'muted' : 'warn'}>${METER_STATUS[st] || st}</div>
	<//>`;
}

// Status: alle Messwerte und Fehlerzaehler (SOLL-MTR-15)
function Meter({ m }) {
	if (!m) return null;
	const s = m.state || {};
	const cnt = ['requests', 'responses', 'timeouts', 'frame_errors', 'exceptions', 'implausible', 'sets'];
	return html`<${Card} title="Zähler (nur Anzeige)">
		<p class=${s.status === 'ok' ? 'muted' : 'warn'}>${(METER_TYPES[s.type] || '?') + ', ' +
			(METER_STATUS[s.status] || s.status || '…') + (s.age_ms >= 0 ? ', letzter Satz vor ' +
			num(s.age_ms / 1000, 1) + ' s' : '')}</p>
		<table>
			${(m.ids || []).map((id) => {
				const v = mval(m, id), d = MV[id] || [String(id), '', 3];
				return html`<tr><td>${d[0]}</td><td class="num">${v === null ? 'ungültig' : num(v, d[2]) + ' ' + d[1]}</td></tr>`;
			})}
		</table>
		<p class="muted msg">${cnt.map((k) => k + ' ' + (s[k] || 0)).join(' · ') +
			(s.last_exception ? ' · letzte Ausnahme ' + s.last_exception : '')}</p>
	<//>`;
}

// ------------------------------------------------------------ Parameter, Fehler, System

function fmt(name, v) {
	if (name === 'state') return STATES[v] || v;
	if (name === 'cp_status' || name === 'cp_raw') return 'ABCDEF'[v] || v;
	if (name === 'errors' || name === 'errors_seen') return errText(v);
	if (name === 'dip_override') return dipText(v) + ' (0x' + v.toString(16) + ')';
	return v;
}

// eingeschaltete DIP-Schalter, z. B. "D1, D2, D4"
const dipText = (v) => [...Array(10).keys()].filter((i) => (v >> i) & 1).map((i) => 'D' + (i + 1)).join(', ') || 'keiner';

function ParamRow({ p, value, onSaved }) {
	const [edit, setEdit] = useState('');
	const [msg, setMsg] = useState(null);
	const writable = p.kind !== 'status';
	const opts = paramOpts(p.name);
	const bool = !opts && writable && p.min === 0 && p.max === 1;
	const save = async () => {
		if (edit === '') return;
		try {
			await post('/evse/params', { [p.name]: Number(edit) });
			setMsg({ ok: true, text: p.kind === 'config' ? 'gespeichert' : 'gesetzt' });
			setEdit('');
			onSaved();
		} catch (e) {
			const t = e.body && e.body.errors ? e.body.errors[p.name] : e.message;
			setMsg({ ok: false, text: t });
		}
	};
	return html`<tr>
		<td title=${p.name}><${Terms} text=${opts ? p.help.split(':')[0] : p.help} />${p.field ? html`<div class="muted msg">Feld ${p.field}</div>` : ''}</td>
		<td class="num">${value === undefined ? '–' : opts ? value
			: bool ? (value ? 'ja' : 'nein') : fmt(p.name, value) + ' ' + p.unit}</td>
		<td class="num hide-s muted">${writable && !bool && !opts ? p.min + '…' + p.max : ''}</td>
		<td>${writable && html`<div class="row edit">
			${opts ? html`<select value=${edit === '' ? value : edit} onChange=${(e) => setEdit(e.target.value)}>
					${opts.map((t, i) => html`<option value=${i}>${i} – ${t}${i === p.default ? ' (Werk)' : ''}</option>`)}</select>`
				: bool ? html`<input type="checkbox" checked=${edit === '' ? !!value : edit === '1'}
					onChange=${(e) => setEdit(e.target.checked ? '1' : '0')} />`
				: html`<input value=${edit} placeholder=${String(p.default)} onInput=${(e) => setEdit(e.target.value)}
					onKeyDown=${(e) => e.key === 'Enter' && save()} />`}
			<button onClick=${save}>OK</button></div><${Msg} m=${msg} />`}</td>
	</tr>`;
}

function Params({ kinds }) {
	const [info, setInfo] = useState(null);
	const [vals, setVals] = useState({});
	const [err, setErr] = useState(null);
	const load = async () => {
		try { setVals(await api('/evse/params')); } catch (e) { /* Verbindung zeigt der Kopf */ }
	};
	useEffect(() => { api('/evse/param_info').then(setInfo).catch((e) => setErr(e.message)); }, []);
	useEvery(load, 2000);
	if (err) return html`<p class="bad">${err}</p>`;
	if (!info) return html`<p class="muted">Lade …</p>`;
	const groups = [['config', 'Konfiguration (gespeichert)'], ['runtime', 'Laufzeitwerte (nicht gespeichert)'],
		['status', 'Ladelogik']];
	return groups.filter(([k]) => kinds.includes(k)).map(([k, t]) => html`<${Card} title=${t}><table>
		<tr><th>Parameter</th><th>Wert</th><th class="hide-s">${k !== 'status' ? 'Bereich' : ''}</th><th>${k !== 'status' ? 'Neu' : ''}</th></tr>
		${info.filter((p) => p.kind === k).map((p) => {
			// Bitmasken der Klemmen als eine Zeile je Ein-/Ausgang
			const bits = p.name === 'inputs' ? ['Eingang', DINS] : p.name === 'outputs' ? ['Ausgang', DOUTS] : null;
			// DIP-Schalter (wirksame Stellung aus dip_override) gleich unter dem Drehschalter
			if (p.name === 'rotary') {
				return [html`<${ParamRow} key=${p.name} p=${p} value=${vals[p.name]} onSaved=${load} />`,
					html`<tr key="dip"><td title="dip_override"><${Terms} text="DIP-Schalter an" /></td>
						<td class="num">${vals.dip_override === undefined ? '–' : dipText(vals.dip_override)}</td><td class="hide-s"></td><td></td></tr>`];
			}
			if (!bits) return html`<${ParamRow} key=${p.name} p=${p} value=${vals[p.name]} onSaved=${load} />`;
			const v = vals[p.name];
			return bits[1].map((n, i) => html`<tr key=${p.name + i}><td title=${p.name + ' Bit ' + i}><${Terms} text=${bits[0] + ' ' + n} /></td>
				<td class="num">${v === undefined ? '–' : (v >> i) & 1 ? 'an' : 'aus'}</td><td class="hide-s"></td><td></td></tr>`);
		})}
	</table><//>`).concat(html`<${Glossary} />`);
}

function Errors() {
	const [d, setD] = useState(null);
	const [msg, setMsg] = useState(null);
	const load = () => api('/evse/errors').then(setD).catch(() => { /* Verbindung zeigt der Kopf */ });
	useEvery(load, 3000);
	const ack = async () => {
		try {
			const r = await post('/evse/errors/ack', {});
			setMsg({ ok: true, text: r.acknowledged ? 'Schützfehler quittiert' : 'Nichts zu quittieren' });
			load();
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	if (!d) return null;
	const age = (ms) => (ms < 60000 ? Math.round(ms / 1000) + ' s' : Math.round(ms / 60000) + ' min');
	return html`<${Card} title="Fehler">
		<p>Aktiv: <b class=${d.active.length ? 'bad' : 'ok'}>${d.active.join(', ') || 'keine'}</b></p>
		<p class="muted msg">Seit Start: ${d.seen.join(', ') || 'keine'}. Fehler löschen sich nach ihrer
			Regel selbst; nur der Schützfehler wird quittiert.</p>
		<div class="row actions"><button onClick=${ack}>Schützfehler quittieren</button></div>
		<${Msg} m=${msg} />
		${d.history.length > 0 && html`<table>
			<tr><th>vor</th><th>Zustand</th><th>neu</th><th>gelöscht</th></tr>
			${d.history.map((h) => html`<tr><td class="num">${age(h.age_ms)}</td>
				<td>${STATES[h.state] || h.state}</td><td class="bad">${h.set.join(', ')}</td>
				<td class="ok">${h.cleared.join(', ')}</td></tr>`)}
		</table>`}
		${d.truncated && html`<p class="muted msg">Ältere Einträge: Shell "evse errors".</p>`}
	<//>`;
}

// Neustart (SOLL-API-31): ein laufender Ladevorgang endet geordnet
async function restart(setMsg) {
	if (!confirm('Steuerung neu starten? Ein laufender Ladevorgang wird beendet.')) return;
	try {
		await post('/system/restart', {});
		setMsg({ ok: true, text: 'Neustart (laufendes Laden endet geordnet) … Seite lädt in 25 s neu' });
		setTimeout(() => location.reload(), 25000);
	} catch (e) { setMsg({ ok: false, text: e.message }); }
}

// Einstellungen Netzwerk, MQTT, Zaehler: Teil von /system/config nach Namensanfang
const SYS_PARTS = {
	netzwerk: ['Netzwerk', ['net_', 'ntp_']],
	mqtt: ['MQTT', ['mqtt_']],
	zaehler: ['Zähler (nur Anzeige)', ['meter_']],
	temperatur: ['Prozessortemperatur', ['mcu_']],
};

function SysConfig({ part }) {
	const [title, prefixes] = SYS_PARTS[part];
	const [cfg, setCfg] = useState(null);
	const [info, setInfo] = useState([]);
	const [edit, setEdit] = useState({});
	const [msg, setMsg] = useState(null);
	const load = () => api('/system/config').then(setCfg).catch((e) => setMsg({ ok: false, text: e.message }));
	useEffect(() => {
		api('/system/config_info').then(setInfo).catch(() => {});
		load();
	}, []);
	if (!cfg) return null;
	const val = (n) => (n in edit ? edit[n] : cfg.saved[n]);
	const save = async (ev) => {
		ev.preventDefault();
		try {
			const r = await post('/system/config', edit);
			setMsg({ ok: true, text: r.restart_required ? 'Gespeichert – wirkt nach Neustart' : 'Gespeichert' });
			setEdit({});
			load();
		} catch (e) {
			const b = e.body && e.body.errors;
			setMsg({ ok: false, text: b ? Object.entries(b).map(([k, v]) => k + ': ' + v).join('; ') : e.message });
		}
	};
	return html`<${Card} title=${title}>
		<form onSubmit=${save}>
		${info.filter((p) => prefixes.some((x) => p.name.startsWith(x))).map((p) => {
			const set = (v) => setEdit({ ...edit, [p.name]: v });
			const opts = p.name === 'meter_type' ? METER_TYPES : p.name === 'meter_framing' ? FRAMING : null;
			if (p.type === 'bool') {
				return html`<label class="check" title=${p.name}><input type="checkbox" checked=${val(p.name)}
					onChange=${(e) => set(e.target.checked)} />${p.help}</label>`;
			}
			if (p.type === 'secret') {
				return html`<label title=${p.name}>${p.help}<input type="password" value=${edit[p.name] || ''}
					placeholder="unverändert" autocomplete="off" onInput=${(e) => set(e.target.value)} /></label>`;
			}
			if (opts) {
				// Hilfetext "Bezeichnung: Codes ..." -> nur die Bezeichnung, die Codes zeigt die Liste
				return html`<label title=${p.name}>${p.help.split(':')[0]}<select value=${val(p.name)} onChange=${(e) => set(+e.target.value)}>
					${opts.map((t, i) => html`<option value=${i}>${t}</option>`)}</select></label>`;
			}
			// mcu_temp_offset hat ein Vorzeichen, die Zifferntastatur am Handy oft keins
			const num = p.type === 'code' && p.name !== 'mcu_temp_offset';
			return html`<label title=${p.name}>${p.help}<input value=${val(p.name)} inputmode=${num ? 'numeric' : null}
				onInput=${(e) => set(p.type === 'code' ? +e.target.value : e.target.value)} /></label>`;
		})}
		<div class="row"><button class="primary" type="submit">Speichern</button></div>
		</form>
		<${Msg} m=${msg} />
		${msg && msg.ok && part !== 'temperatur' && html`<p class="muted msg">Zustand der Verbindung unter
			<a href="#/status">Status → Verbindungen</a>.</p>`}
		${cfg.restart_required && html`<div class="row msg"><span class="warn">Gespeicherte Werte wirken erst
			nach einem Neustart.</span><button onClick=${() => restart(setMsg)}>Jetzt neu starten</button></div>`}
	<//>`;
}

const dur = (ms) => {
	const m = Math.floor(ms / 60000);
	return m < 60 ? m + ' min' : m < 1440 ? Math.floor(m / 60) + ' h ' + (m % 60) + ' min'
		: Math.floor(m / 1440) + ' d ' + Math.floor(m % 1440 / 60) + ' h';
};

// Firmware-Update (SOLL-BOOT-30..33). Die Datei bleibt im Browser; die Seite laeuft weiter,
// waehrend die Steuerung an den Original-Bootloader uebergibt. Der antwortet unter derselben
// Adresse auf jedes GET mit HTML (Wallfree immer mit JSON, auch ohne Anmeldung), nimmt die
// Datei als Formular an /upload.cgi an und startet danach die neue Firmware.
const IMAGE_MAX = 0x50000; // Sektor 4..6 (320 KB), mehr loescht der Bootloader nicht
const TAG = 'WALLFREE-IMAGE:';
const TRAILER_MAGIC = 0x474D4957; // Anhang des Builds (SOLL-BOOT-35, scripts/image-trailer.py)

// CRC32 (IEEE) wie zlib und die Firmware
function crc32(b) {
	let c = ~0;
	for (let i = 0; i < b.length; i++) {
		c ^= b[i];
		for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1;
	}
	return (~c >>> 0).toString(16).padStart(8, '0');
}

// Anhang: Kennung, Laenge, CRC32, invertierte Kennung. @return CRC (hex), 'bad' oder null
function trailer(buf) {
	if (buf.byteLength < 24) return null;
	const dv = new DataView(buf), o = buf.byteLength - 16;
	if (dv.getUint32(o, true) !== TRAILER_MAGIC || dv.getUint32(o + 12, true) !== (~TRAILER_MAGIC >>> 0)) return null;
	const crc = crc32(new Uint8Array(buf, 0, o));
	return dv.getUint32(o + 4, true) === o && parseInt(crc, 16) === dv.getUint32(o + 8, true) ? crc : 'bad';
}

// Firmwarestand als Text: "Wallfree 0.1 (Build d2863d9)"; Kennung "<Version>+<Build>" aus dem Abbild,
// aeltere Abbilder tragen nur den Commit
function fwName(tag) {
	const i = tag.indexOf('+');
	return i < 0 ? 'Wallfree (Build ' + tag + ')' : 'Wallfree ' + tag.slice(0, i) + ' (Build ' + tag.slice(i + 1) + ')';
}

// Vorpruefung: was der Bootloader selbst nicht prueft. @return {version, crc} (fremd: beide
// null) oder Fehlertext
function checkImage(buf) {
	if (buf.byteLength < 8) return 'Datei zu klein';
	if (buf.byteLength > IMAGE_MAX) return 'Datei zu groß (höchstens 320 KB)';
	const dv = new DataView(buf), sp = dv.getUint32(0, true), rv = dv.getUint32(4, true);
	if ((sp & 0x2FF00000) !== 0x20000000 || !(rv & 1) || rv < 0x08010000 || rv >= 0x08070000) {
		return 'Kein Firmware-Abbild für die Adresse 0x08010000';
	}
	const b = new Uint8Array(buf), t = new TextEncoder().encode(TAG);
	let version = null;
	outer: for (let i = 0; i + t.length < b.length; i++) {
		for (let k = 0; k < t.length; k++) if (b[i + k] !== t[k]) continue outer;
		let j = i + t.length;
		version = '';
		while (j < b.length && b[j] && version.length < 40) version += String.fromCharCode(b[j++]);
		break;
	}
	const crc = trailer(buf);
	if (crc === 'bad') return 'Datei beschädigt oder unvollständig (Prüfsumme falsch)';
	if (version !== null && crc === null) return 'Wallfree-Abbild ohne Prüfsumme (zu alt), nicht verwendbar';
	return { version, crc };
}

// Wer antwortet unter der eigenen Adresse? 'wallfree', 'bootloader' oder 'weg'
async function probe() {
	try {
		const r = await fetch('/system/info', { cache: 'no-store', signal: AbortSignal.timeout(2000) });
		const t = await r.text();
		return t.trim().startsWith('{') ? 'wallfree' : r.ok ? 'bootloader' : 'weg';
	} catch (e) { return 'weg'; }
}

async function waitFor(what, seconds) {
	for (let i = 0; i < seconds; i++) {
		if (await probe() === what) return true;
		await new Promise((r) => setTimeout(r, 1000));
	}
	return false;
}

// Interne Schritte 0..5 (Uebergabe, Warten auf den Bootloader, Hochladen, Neustart, Ergebnis,
// fertig); angezeigt werden nur drei Stufen (Nutzer 2026-10-26)
const STAGES = [['Vorbereiten', 2], ['Übertragen', 3], ['Neustart', 5]];
const mmss = (s) => Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');

function Update({ onBusy }) {
	const [file, setFile] = useState(null); // {name, buf, check}
	const [step, setStep] = useState(-1);
	const [msg, setMsg] = useState(null);
	const busy = step >= 0 && step < 5;
	const [t0, setT0] = useState(0);
	const [now, setNow] = useState(0);
	useEffect(() => {
		if (!busy) return undefined;
		const t = setInterval(() => setNow(Date.now()), 1000);
		return () => clearInterval(t);
	}, [busy]);
	// nach Erfolg bis zum Neuladen: die Live-Verbindung steht dann noch nicht wieder
	const hold = busy || (step === 5 && msg && msg.ok);
	// Kopfzeile und Ausgrauen (Main); Schliessen des Tabs braeche den Upload ab
	useEffect(() => { onBusy(hold); }, [hold]);
	useEffect(() => {
		const warn = (ev) => { ev.preventDefault(); ev.returnValue = ''; };
		if (busy) addEventListener('beforeunload', warn);
		return () => removeEventListener('beforeunload', warn);
	}, [busy]);
	useEffect(() => () => onBusy(false), []);
	const pick = async (ev) => {
		const f = ev.target.files[0];
		setMsg(null);
		if (!f) { setFile(null); return; }
		const buf = await f.arrayBuffer();
		setFile({ name: f.name, buf, check: checkImage(buf) });
	};
	const run = async () => {
		const c = file.check, ver = c.version;
		const foreign = 'Kein Wallfree-Abbild (z. B. Originalfirmware). Danach ist diese ' +
			'Weboberfläche nicht mehr erreichbar. Wirklich aufspielen?';
		const others = '\n\nVorher evcc und andere Programme anhalten, die die Wallbox abfragen, und ' +
			'weitere Browser-Tabs schließen: Jede Anfrage während des Hochladens bricht das Update ab.';
		if (!confirm((ver ? fwName(ver) + ' aufspielen? Ein laufender Ladevorgang wird beendet.'
			: foreign) + others)) return;
		setMsg(null);
		setT0(Date.now());
		setNow(Date.now());
		try {
			setStep(0);
			const r = await post('/system/update', {});
			quiet = true;
			setStep(1);
			if (!await waitFor('bootloader', 60)) {
				throw new Error('Der Bootloader antwortet nicht. Er wartet unter http://' + r.address +
					'/ bis zu 2 min auf einen Upload (von Hand möglich).');
			}
			setStep(2);
			const fd = new FormData();
			fd.append('datafile', new Blob([file.buf], { type: 'application/octet-stream' }),
				file.name || 'firmware.bin');
			// Antwort und Verbindungsende sind beim Bootloader nicht verlaesslich; das
			// Ergebnis meldet Wallfree nach dem Neustart (SOLL-BOOT-33)
			try {
				await fetch('/upload.cgi', { method: 'POST', body: fd, signal: AbortSignal.timeout(30000) });
			} catch (e) { /* weiter mit dem Neustart */ }
			setStep(3);
			if (!ver) {
				setStep(5);
				setMsg({ ok: true, text: 'Übertragen. Die Steuerung startet die aufgespielte Firmware.' });
				return;
			}
			if (!await waitFor('wallfree', 180)) throw new Error('Wallfree meldet sich nicht zurück.');
			quiet = false;
			setStep(4);
			const si = await api('/system/info');
			// SOLL-BOOT-35: erst die Pruefsumme der laufenden Firmware belegt das ganze Abbild
			const ok = si.update_result && si.update_result.ok && si.image && si.image.ok &&
				si.image.crc === c.crc;
			setStep(5);
			setMsg({ ok, text: ok ? 'Update erfolgreich, ' + fwName(si.firmware + '+' + si.build) + '. Seite lädt neu …'
				: si.image && !si.image.ok ? 'Update unvollständig angekommen: Die Firmware ist ' +
					'beschädigt, Laden ist gesperrt. Bitte das Update wiederholen.'
				: 'Update fehlgeschlagen, es läuft ' + fwName(si.firmware + '+' + si.build) + '.' });
			if (ok) setTimeout(() => location.reload(), 3000);
		} catch (e) {
			quiet = false;
			setStep(-1);
			setMsg({ ok: false, text: e.message });
		}
	};
	const c = file && file.check;
	return html`<${Card} title="Firmware-Update">
		<div class="row"><input type="file" accept=".bin" disabled=${busy} onChange=${pick} /></div>
		${c && html`<p class=${typeof c === 'string' ? 'bad msg' : c.version ? 'msg' : 'warn msg'}>
			${typeof c === 'string' ? c : c.version ? fwName(c.version) + ', ' +
			Math.round(file.buf.byteLength / 1024) + ' KB, Prüfsumme ' + c.crc : 'Kein Wallfree-Abbild (z. B. Originalfirmware)'}</p>`}
		${busy && html`<div class="progress">
			<p><strong>Update läuft – bitte warten.</strong> Das dauert meist weniger als eine Minute
				<span class="muted"> (seit ${mmss(Math.max(0, Math.round((now - t0) / 1000)))})</span>.</p>
			<div class="stages">${STAGES.map(([s, end], i) => {
				const from = i ? STAGES[i - 1][1] : 0;
				const st = step >= end ? 'ok' : step >= from ? '' : 'muted';
				return html`<span class=${st}>${step >= end ? '✓ ' : ''}${s}${st === '' ? ' …' : ''}</span>`;
			})}</div>
			<p class="muted msg">Die Steuerung ist währenddessen nicht erreichbar. Diese Seite bitte offen lassen.</p>
		</div>`}
		<div class="row actions"><button class="primary" disabled=${!c || typeof c === 'string' || busy}
			onClick=${run}>Update starten</button></div>
		<${Msg} m=${msg} />
		<p class="muted msg">Die Datei geht an den Bootloader der Steuerung; ohne Upload startet er nach
			2 min wieder die bisherige Firmware. Die Einstellungen bleiben erhalten. Während des Updates
			darf nichts anderes die Wallbox abfragen (evcc anhalten, andere Tabs schließen).</p>
	<//>`;
}

// Expertenansicht (Nutzer 2026-10-27): nur Anzeige, je Browser gespeichert, ab Werk aus;
// ohne localStorage (privates Fenster) gilt die einfache Ansicht
const EXPERT_KEY = 'wallfree.expert';
function loadExpert() {
	try { return localStorage.getItem(EXPERT_KEY) === '1'; } catch (e) { return false; }
}
function saveExpert(on) {
	try { localStorage.setItem(EXPERT_KEY, on ? '1' : '0'); } catch (e) { /* nur fuer diese Sitzung */ }
}

function ViewCard({ expert, setExpert }) {
	return html`<${Card} title="Ansicht">
		<label class="check"><input type="checkbox" checked=${expert} onChange=${(e) => setExpert(e.target.checked)} />
			Expertenansicht</label>
		<p class="muted msg">Zeigt zusätzlich die Parameter der Ladelogik (Einstellungen → Laden) und die
			Rohwerte unter Status (Ladestrom, Zähler, Ladelogik, CP-Oszilloskop). Gilt nur für diesen Browser.</p>
	<//>`;
}

function SystemPage({ onBusy, expert, setExpert }) {
	const [si, setSi] = useState(null);
	const [msg, setMsg] = useState(null);
	useEvery(() => api('/system/info').then(setSi).catch(() => {}), 10000);
	return html`<${Card} title="System">
		${si && html`<table>
			<tr><td>Firmware</td><td class="num">Wallfree ${si.firmware}<br />
				<small class="muted">Build ${si.build}</small></td></tr>
			<tr><td>Laufzeit</td><td class="num">${dur(si.uptime_ms)}</td></tr>
			<tr><td>Letzter Reset</td><td class="num wrap">${resetText(si.reset_reason, si.reset_detail)}</td></tr>
			${si.mac && html`<tr><td>MAC-Adresse</td><td class="num">${si.mac}</td></tr>`}
			${si.image && html`<tr><td>Prüfsumme</td><td class="num ${si.image.ok ? '' : 'bad'}">
				${si.image.crc}${si.image.ok ? '' : ' – beschädigt, Laden gesperrt'}</td></tr>`}
			${si.update_result && html`<tr><td>Letztes Update</td><td class="num ${si.update_result.ok ? 'ok' : 'bad'}">
				${si.update_result.ok ? 'erfolgreich' : 'fehlgeschlagen'}</td></tr>`}
		</table>`}
		<div class="row actions"><button onClick=${() => restart(setMsg)}>Neu starten</button></div>
		<${Msg} m=${msg} />
	<//>
	<${ViewCard} expert=${expert} setExpert=${setExpert} />
	${expert && html`<${SysConfig} part="temperatur" />`}
	${expert && html`<${Console} />`}
	<${Update} onBusy=${onBusy} />
	<${Card} title="Lizenz">
		<p class="msg">Wallfree ist freie Software unter der Apache License 2.0 und wird ohne jede
			Gewährleistung bereitgestellt. Sie enthält Zephyr, Preact, htm und weitere Software anderer
			Autoren: <a href="/lizenzen.txt" target="_blank">Lizenzhinweise</a>.</p>
	<//>`;
}

// Ein Kommando je Anfrage; Verlauf mit Pfeil hoch/runter nur in dieser Seite
function Console() {
	const [lines, setLines] = useState([]);
	const [cmd, setCmd] = useState('');
	const [busy, setBusy] = useState(false);
	const hist = useRef({ list: [], pos: 0 });
	const out = useRef(null);
	const input = useRef(null);
	useEffect(() => {
		if (out.current) out.current.scrollTop = out.current.scrollHeight;
		if (input.current && lines.length) input.current.focus();
	}, [lines]);
	const run = async (ev) => {
		ev.preventDefault();
		const c = cmd.trim();
		if (!c || busy) return;
		const h = hist.current;
		if (h.list[h.list.length - 1] !== c) h.list = [...h.list, c].slice(-50);
		h.pos = h.list.length;
		setCmd('');
		setBusy(true);
		let text, bad = false;
		try { text = await shell(c); } catch (e) { text = e.message; bad = true; }
		setLines((l) => [...l.slice(-19), { cmd: c, text: text.replace(/\s+$/, ''), bad }]);
		setBusy(false);
	};
	const key = (ev) => {
		const h = hist.current;
		if (ev.key === 'ArrowUp' && h.pos > 0) h.pos--;
		else if (ev.key === 'ArrowDown' && h.pos < h.list.length) h.pos++;
		else return;
		ev.preventDefault();
		setCmd(h.list[h.pos] || '');
	};
	return html`<${Card} title="Konsole">
		<p class="muted msg">Shell-Kommandos für die Diagnose, z. B. <code>evse status</code>,
			<code>evse param list lock</code>, <code>evse log</code> (jüngste Meldungen) oder
			<code>evse threads</code>; <code>evse -h</code> zeigt alle.
			Kommandos wirken sofort und ohne Rückfrage.</p>
		${lines.length > 0 && html`<pre class="console" ref=${out}>${lines.map((l) =>
			html`<b>› ${l.cmd}</b>${'\n'}<span class=${l.bad ? 'bad' : ''}>${l.text}</span>${'\n'}`)}</pre>`}
		<form class="row" onSubmit=${run}>
			<input class="mono" ref=${input} value=${cmd} onInput=${(e) => setCmd(e.target.value)} onKeyDown=${key}
				maxlength="255" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="evse status"
				aria-label="Kommando" />
			<button type="submit" disabled=${busy}>${busy ? 'Läuft …' : 'Ausführen'}</button>
		</form>
	<//>`;
}

function Credentials({ first }) {
	const [u, setU] = useState('');
	useEffect(() => { api('/credentials').then((c) => setU(c.user || '')).catch(() => {}); }, []);
	const [p1, setP1] = useState('');
	const [p2, setP2] = useState('');
	const [msg, setMsg] = useState(null);
	const save = async (ev) => {
		ev.preventDefault();
		if (p1 !== p2) { setMsg({ ok: false, text: 'Passwörter verschieden' }); return; }
		try {
			await post('/credentials', { user: u, password: p1 });
			setMsg({ ok: true, text: 'Gespeichert. Bitte mit den neuen Zugangsdaten anmelden.' });
			setTimeout(() => location.reload(), 2500);
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	return html`<${Card} title=${first ? 'Zugangsdaten festlegen' : 'Zugangsdaten ändern'}>
		${first && html`<p>Die Steuerung nutzt noch den Werkszugang. Bitte Benutzer und neues Passwort
			festlegen; erst danach sind Oberfläche und API (auch für evcc) nutzbar.</p>`}
		<form onSubmit=${save}>
			<label>Benutzer<input value=${u} onInput=${(e) => setU(e.target.value)} autocomplete="username" /></label>
			<label>Neues Passwort (8–64 Zeichen)<input type="password" value=${p1}
				onInput=${(e) => setP1(e.target.value)} autocomplete="new-password" /></label>
			<label>Wiederholen<input type="password" value=${p2}
				onInput=${(e) => setP2(e.target.value)} autocomplete="new-password" /></label>
			<button class="primary" type="submit">Speichern</button>
		</form>
		<${Msg} m=${msg} />
		<p class="muted msg">Die Verbindung ist nicht verschlüsselt (kein HTTPS): Wer im Heimnetz mitlauscht,
			kann alle angezeigten Daten lesen. Bei der Anmeldung schickt der Browser das Passwort nicht
			mit, sondern nur einen daraus berechneten Prüfwert. Beim Ändern hier wird das neue Passwort
			dagegen lesbar übertragen.</p>
	<//>`;
}

// ------------------------------------------------------------ Verlauf (SOLL-API-33)

const HF_CONN = 1, HF_CHG = 2, HF_ERR = 4;
const HL = 36, HR = 8, HH = 90, HGAP = 34, HBAND = 10;
const hm = (t) => new Date(t).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
const flagText = (f) => (f & HF_ERR ? 'Fehler' : f & HF_CHG ? 'lädt' : f & HF_CONN ? 'angesteckt' : 'kein Fahrzeug');

function useHistory() {
	const [h, setH] = useState(null);
	useEvery(() => timed('/evse/history').then(([j, at]) => setH({ ...j, at })).catch(() => {}), 60000);
	return h;
}

// Zeilen mit Beginn/Ende in Browserzeit (age_ms gilt zum Abrufzeitpunkt); das laufende
// Intervall endet jetzt
function histRows(h) {
	const now = Date.now(), n = h.rows.length, end0 = h.at - h.age_ms;
	const rows = h.rows.map((r, i) => {
		const t1 = end0 - (n - 1 - i) * h.interval_ms;
		return { t0: t1 - h.interval_ms, t1, p: r[0], a: r[1], f: r[2] };
	});
	if (h.current) rows.push({ t0: end0, t1: now, p: h.current[0], a: h.current[1], f: h.current[2], cur: true });
	return rows;
}

// Stufen je Intervall; null = Luecke. @return [Linie, Flaeche bis base]
function steps(rows, X, Y, val, base) {
	let line = '', area = '', run = [];
	const flush = () => {
		if (!run.length) return;
		const pts = run.map(([x, y]) => x.toFixed(1) + ' ' + y.toFixed(1)).join('L');
		line += 'M' + pts;
		area += 'M' + run[0][0].toFixed(1) + ' ' + base + 'L' + pts + 'L' + run[run.length - 1][0].toFixed(1) + ' ' + base + 'Z';
		run = [];
	};
	for (const r of rows) {
		const v = val(r);
		if (v === null) { flush(); continue; }
		run.push([X(r.t0), Y(v)], [X(r.t1), Y(v)]);
	}
	flush();
	return [line, area];
}

// Zustandsband: gleiche Nachbarn zu einem Block zusammengefasst
const bandCls = (f) => (f & HF_ERR ? 'err' : f & HF_CHG ? 'chg' : f & HF_CONN ? 'conn' : null);
function band(rows) {
	const out = [];
	for (const r of rows) {
		const c = bandCls(r.f), last = out[out.length - 1];
		if (last && last[0] === c && last[2] === r.t0) last[2] = r.t1;
		else out.push([c, r.t0, r.t1]);
	}
	return out.filter((b) => b[0]);
}

// in der tatsaechlichen Breite gezeichnet, damit die Schrift auf dem Handy nicht schrumpft
function History({ h, maxA }) {
	const [hover, setHover] = useState(null);
	const [HW, setW] = useState(640);
	const ref = useRef(null);
	useEffect(() => {
		const o = new ResizeObserver(([e]) => setW(Math.max(280, Math.round(e.contentRect.width))));
		o.observe(ref.current);
		return () => o.disconnect();
	}, []);
	const rows = h ? histRows(h) : [];
	return html`<${Card} title="Verlauf"><div ref=${ref}>${!h ? html`<p class="muted">Lade …</p>`
		: !rows.length ? html`<p class="muted">Noch keine Werte seit dem Start.</p>`
		: chart(h, rows, maxA, HW, hover, setHover)}</div><//>`;
}

function chart(h, rows, maxA, HW, hover, setHover) {
	const now = Date.now();
	const x0 = Math.min(rows[0].t0, now - 6 * 3600e3), span = now - x0;
	const X = (t) => HL + (t - x0) / span * (HW - HL - HR);
	const panels = [];
	if (rows.some((r) => r.p !== null)) {
		const max = Math.max(1, Math.ceil(Math.max(...rows.map((r) => r.p || 0)) / 1000));
		panels.push(['Leistung (kW)', max, (r) => (r.p === null ? null : r.p / 1000), true]);
	}
	panels.push(['Erlaubter Strom (A)', Math.max(maxA || 32, ...rows.map((r) => Math.ceil(r.a / 1000))),
		(r) => r.a / 1000, false]);
	const bandY = panels.length * (HH + HGAP);
	const height = bandY + HBAND + 22;
	const stepH = span > 24 * 3600e3 ? 6 : span > 8 * 3600e3 ? 3 : 1;
	const ticks = [];
	for (let t = Math.ceil(x0 / 3600e3) * 3600e3; t < now; t += 3600e3) {
		if (new Date(t).getHours() % stepH === 0) ticks.push(t);
	}
	const r = hover === null ? null : rows[hover];
	const xm = r && X((r.t0 + r.t1) / 2);
	const move = (e) => {
		const b = e.currentTarget.ownerSVGElement.getBoundingClientRect();
		const t = x0 + ((e.clientX - b.left) / b.width * HW - HL) / (HW - HL - HR) * span;
		// naechstgelegenes Intervall: auch schmale (laufendes kurz nach dem Start) treffbar
		const dist = (x) => (t < x.t0 ? x.t0 - t : t > x.t1 ? t - x.t1 : 0);
		setHover(rows.reduce((b, x, i) => (dist(x) < dist(rows[b]) ? i : b), 0));
	};
	// Werte nur beim Zeigen/Tippen, an der Kurve
	return html`<div class="h-wrap">
		${r && html`<div class="h-tip" style=${'left:' + Math.min(Math.max(xm, 90), HW - 90) + 'px'}>
			${hm(r.t0)}–${r.cur ? 'jetzt' : hm(r.t1)}<br />
			${r.p !== null ? html`<b>${num(r.p / 1000, 1)} kW</b> · ` : ''}erlaubt <b>${amps(r.a)}</b> · ${flagText(r.f)}</div>`}
		<svg class="hist" viewBox=${'0 0 ' + HW + ' ' + height} role="img" aria-label="Verlauf der letzten Stunden">
			${panels.map(([title, max, val, fill], k) => {
				const y0 = k * (HH + HGAP) + 18, Y = (v) => y0 + HH - v / max * HH;
				const [line, area] = steps(rows, X, Y, val, y0 + HH);
				return html`<g>
					<text class="h-title" x=${HL} y=${y0 - 8}>${title}</text>
					${[0, max / 2, max].map((g) => html`<line class="h-grid" x1=${HL} x2=${HW - HR} y1=${Y(g)} y2=${Y(g)} />
						<text class="h-ax" x=${HL - 6} y=${Y(g) + 4}>${num(g, g % 1 ? 1 : 0)}</text>`)}
					${fill && html`<path class="h-area" d=${area} />`}
					<path class="h-line" d=${line} />
				</g>`;
			})}
			${band(rows).map(([c, t0, t1]) => html`<rect class=${'h-band ' + c} x=${X(t0)} y=${bandY}
				width=${Math.max(2, X(t1) - X(t0) - 2)} height=${HBAND} rx="2" />`)}
			${ticks.map((t) => html`<text class="h-ax mid" x=${X(t)} y=${bandY + HBAND + 16}>${hm(t)}</text>`)}
			${r && html`<line class="h-cross" x1=${xm} x2=${xm} y1="10" y2=${bandY + HBAND} />`}
			<rect class="h-hit" x=${HL} y="0" width=${HW - HL - HR} height=${bandY + HBAND}
				onPointerMove=${move} onPointerDown=${move} onPointerLeave=${() => setHover(null)} />
		</svg></div>
		<p class="legend"><i class="sw chg"></i>lädt <i class="sw conn"></i>angesteckt <i class="sw err"></i>Fehler</p>
		<p class="muted msg">Mittelwerte je ${h.interval_ms / 60000} min seit dem Start der Steuerung (höchstens 48 h);
			nach einem Neustart leer.</p>`;
}

// ------------------------------------------------------------ Bereiche

// CP-Oszilloskop (SOLL-API-34): aequivalente Abtastung der zweiten Periodenhaelfte; die zentrierte
// PWM ist symmetrisch, die erste Haelfte ist das Spiegelbild. Spannung je Punkt: CP+-Kanal,
// solange er ueber 1,5 V liegt, sonst minus CP--Kanal. Schwellen nach IEC 61851 (A..D, -12 V).
function CpScope() {
	const [d, setD] = useState(null);
	const [busy, setBusy] = useState(false);
	const [err, setErr] = useState(null);
	const take = async () => {
		setBusy(true);
		try { setD(await api('/evse/cp_scope')); setErr(null); } catch (e) { setErr(e.message); }
		setBusy(false);
	};
	const W = 600, H = 230, X0 = 40, Y = (mv) => 12 + (13000 - mv) * (H - 36) / 26000;
	const X = (us) => X0 + (us + 500) * (W - X0 - 8) / 1000;
	let path = '';
	if (d) {
		const pts = d.pos_mv.map((p, i) => [d.t0_us + i * d.dt_us - 1000, p > 1500 ? p : -d.neg_mv[i]]);
		const all = pts.concat(pts.map(([t, v]) => [-t, v])).sort((a, b) => a[0] - b[0]);
		path = all.map(([t, v], i) => (i ? 'L' : 'M') + X(t).toFixed(1) + ' ' + Y(v).toFixed(1)).join(' ');
	}
	const lines = [[12000, 'A 12 V'], [9000, 'B 9 V'], [6000, 'C 6 V'], [3000, 'D 3 V'], [0, '0 V'], [-12000, '−12 V']];
	return html`<${Card} title="CP-Oszilloskop">
		<div class="row"><button onClick=${take} disabled=${busy}>${busy ? 'Aufnahme …' : 'Aufnehmen'}</button>
			${d && html`<span class="muted">Tastgrad ${(d.duty / 10).toFixed(1)} %</span>`}</div>
		${err && html`<p class="bad msg">${err}</p>`}
		${d && html`<svg class="scope" viewBox=${'0 0 ' + W + ' ' + H} role="img" aria-label="CP-Signal über eine Periode (Oszilloskop)">
			${lines.map(([mv, t]) => html`<line class="h-grid" x1=${X0} x2=${W - 8} y1=${Y(mv)} y2=${Y(mv)} />
				<text class="h-ax" x=${X0 - 4} y=${Y(mv) + 4}>${t}</text>`)}
			${[-500, -250, 0, 250, 500].map((us) => html`<text class=${us === 500 ? 'h-ax' : 'h-ax mid'} x=${X(us)} y=${H - 4}>${us} µs</text>`)}
			<path class="scope-line" d=${path} />
		</svg>
		<p class="muted msg">Eine PWM-Periode (1 ms), Mitte = positiver Puls. Gemessen über etwa 110 Perioden mit
			verschobenem Abtastzeitpunkt; die Messung der Ladelogik läuft dabei unverändert weiter.</p>`}
	<//>`;
}

// Zustand der Verbindungen (die Werte dazu unter Einstellungen); nacheinander abgefragt, damit die
// Seite nur einen der wenigen Verbindungsplaetze der Steuerung braucht
function Connections({ meter }) {
	const [d, setD] = useState(null);
	const stop = useRef(false);
	useEffect(() => () => { stop.current = true; }, []);
	useEvery(async () => {
		try {
			const cfg = await api('/system/config');
			const si = await api('/system/info');
			const mq = await api('/mqtt/state');
			if (!stop.current) setD({ a: cfg.active, si, mq });
		} catch (e) { /* naechster Versuch */ }
	}, 10000);
	if (!d) return html`<${Card} title="Verbindungen"><p class="muted">…</p><//>`;
	const { a, si, mq } = d;
	const st = meter && meter.state;
	return html`<${Card} title="Verbindungen"><table>
		<tr><td>Netzwerk</td><td class="num wrap">${a.net_dhcp ? 'Adresse per DHCP' : 'feste Adresse ' + a.net_ip}<br />
			<small class="muted">Hostname ${a.net_hostname || 'automatisch'}</small></td></tr>
		<tr><td>Uhrzeit</td><td class="num wrap">${si.time ? html`${new Date(si.time.time_ms).toLocaleString('de-DE')}<br />
			<small class="muted">${si.time.server}, abgeglichen vor ${dur(si.time.synced_ms)}</small>`
			: a.ntp_enable ? 'unbekannt (Zeitserver antwortet nicht)' : 'unbekannt (Zeitserver aus)'}</td></tr>
		<tr><td>MQTT</td><td class="num wrap">${MQTT_STATES[mq.connection_state]}${mq.connection_state ? html`<br />
			<small class="muted">${a.mqtt_broker}:${a.mqtt_port}, Präfix ${mq.prefix}, gesendet ${mq.published},
			Befehle ${mq.commands}${mq.rejected ? ' (abgelehnt ' + mq.rejected + ': ' + mq.last_reject + ')' : ''}</small>` : ''}</td></tr>
		<tr><td>Zähler</td><td class="num wrap">${st ? METER_STATUS[st.status] || st.status : METER_STATUS.off}</td></tr>
		${si.mcu_temp_dc != null && html`<tr><td>Prozessortemperatur</td><td class="num wrap">${Math.round(si.mcu_temp_dc / 10)} °C<br />
			<small class="muted">${a.mcu_temp_offset ? 'abgeglichen um ' + (a.mcu_temp_offset / 10).toLocaleString('de-DE') + ' °C'
				: 'kann um einige Grad abweichen'}</small></td></tr>`}
	</table><//>`;
}

// Einfache Ansicht: Verlauf, Verbindungen, Fehler, Ereignisprotokoll; Expertenansicht zusaetzlich Rohwerte
function Status({ live, expert }) {
	const h = useHistory();
	return html`<${History} h=${h} maxA=${live.limits && live.limits.hardware / 1000} />
		<${Connections} meter=${live.meter} /><${Errors} /><${EventLog} live=${live} all=${true} />
		${expert && html`<${CurrentTable} l=${live.limits} meter=${live.meter} /><${CpScope} />
			<${Meter} m=${live.meter} /><${Params} kinds=${['status']} />`}`;
}

// "Laden" (Parameter der Ladelogik) nur in der Expertenansicht
const SETTINGS = [['laden', 'Laden'], ['netzwerk', 'Netzwerk'], ['mqtt', 'MQTT'], ['zaehler', 'Zähler'],
	['zugang', 'Anmeldedaten']];

function Settings({ sub, live, expert }) {
	const tabs = SETTINGS.filter(([k]) => expert || k !== 'laden');
	const s = tabs.some(([k]) => k === sub) ? sub : tabs[0][0];
	return html`<nav class="sub">${tabs.map(([k, t]) =>
		html`<a href=${'#/einstellungen/' + k} class=${s === k ? 'sel' : ''}>${t}</a>`)}</nav>
		${s === 'laden' ? html`<${Params} kinds=${['config', 'runtime']} />`
		: s === 'zugang' ? html`<${Credentials} first=${false} />`
		: html`<${SysConfig} key=${s} part=${s} />`}`;
}

// ------------------------------------------------------------ Rahmen

// Bereich in der Adresse (#/einstellungen/mqtt): Zurueck-Taste und Lesezeichen
const AREAS = [['', 'Übersicht'], ['status', 'Status'], ['einstellungen', 'Einstellungen'], ['system', 'System']];
const route = () => location.hash.replace(/^#\/?/, '').split('/');

function useRoute() {
	const [r, setR] = useState(route);
	useEffect(() => {
		const f = () => { setR(route()); scrollTo(0, 0); };
		addEventListener('hashchange', f);
		return () => removeEventListener('hashchange', f);
	}, []);
	return r;
}

// Wortmarke (Entwurf des Nutzers) als Text: passt sich Kopfhoehe und dunklem Modus an
// Verbindungszustand als Punkt am Logo (Nutzer 2026-10-27, Variante A); Text beim Zeigen
const Logo = ({ conn }) => html`<h1 class="logo"><a href="#/"><svg viewBox="0 0 32 32" aria-hidden="true"><rect width="32" height="32" rx="7" /><path d="M18.5 4 8 18h7l-2 10 11-14h-7z" /></svg>Wall<span>free</span>${conn
	&& html`<i class="conn dot ${conn.dot}" data-conn=${conn.text} title=${conn.title} aria-label=${conn.text}></i>`}</a></h1>`;

function Main() {
	const live = useLive();
	const [a, sub] = useRoute();
	// Firmware-Update: die Steuerung ist absichtlich weg, die Seite bleibt bedienbar
	const [updating, setUpdating] = useState(false);
	const [expert, setExpertState] = useState(loadExpert);
	const setExpert = (on) => { saveExpert(on); setExpertState(on); };
	const area = AREAS.some(([k]) => k === a) ? a : '';
	const dot = updating ? 'warn' : live.online ? 'ok' : live.online === false ? 'bad' : '';
	const conn = updating ? 'Update läuft …' : live.online ? 'verbunden'
		: live.online === false ? 'getrennt' : '…';
	return html`
		<header>
			<${Logo} conn=${{ dot, text: conn, title: conn + (live.mode === 'live' ? ' – live (WebSocket)'
				: live.mode === 'poll' ? ' – Abfrage alle 2 s' : '') }} />
			<nav class="top">${AREAS.map(([k, t]) => html`<a href=${'#/' + k} class=${area === k ? 'sel' : ''}>${t}</a>`)}</nav>
			${updating ? html`<div class="banner warn">Update läuft – die Steuerung startet neu, bitte warten.</div>`
				: live.online === false && html`<div class="banner bad">Verbindung zur Wallbox getrennt – angezeigte Werte sind nicht aktuell.</div>`}
		</header>
		<main class=${live.online === false && !updating ? 'offline' : ''}>
			${area === 'status' ? html`<${Status} live=${live} expert=${expert} />`
				: area === 'einstellungen' ? html`<${Settings} sub=${sub} live=${live} expert=${expert} />`
				: area === 'system' ? html`<${SystemPage} onBusy=${setUpdating} expert=${expert} setExpert=${setExpert} />`
				: html`<${Overview} live=${live} />`}
		</main>`;
}

function App() {
	const [cred, setCred] = useState(null);
	const [err, setErr] = useState(null);
	useEffect(() => { api('/credentials').then(setCred).catch((e) => setErr(e.message)); }, []);
	if (err) return html`<p class="pad bad">Keine Verbindung: ${err}</p>`;
	if (!cred) return html`<p class="pad muted">Lade …</p>`;
	if (cred.must_change) {
		return html`<header><${Logo} /></header><main><${Credentials} first=${true} /></main>`;
	}
	return html`<${Main} />`;
}

render(html`<${App} />`, document.getElementById('app'));
