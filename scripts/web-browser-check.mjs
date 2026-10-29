// SPDX-License-Identifier: Apache-2.0
// Browser-Durchlauf der Weboberflaeche gegen die laufende Simulation (scripts/sim-run.sh):
// headless Chrome ueber das DevTools-Protokoll (Node >= 22, google-chrome-stable), Digest-
// Anmeldung durch Chrome selbst, Fahrzeug ueber die Telnet-Shell (Port 2323, anders mit
// WALLFREE_TELNET_PORT, z. B. fuer eine zweite Simulation mit -DCONFIG_SHELL_TELNET_PORT=2324).
// Zugangsdaten muessen bereits geaendert sein (Werkszugang zeigt nur die Zugangsdaten-Seite).
//   node scripts/web-browser-check.mjs <http-port> <user> <passwort> <ausgabeverzeichnis> [meter]
// "meter": in der Simulation ist ein Zaehler konfiguriert (meter_type != 0), zusaetzliche Pruefungen.
// Ergebnis: Screenshots und browse.json im Ausgabeverzeichnis, Exitcode 1 bei Fehlschlag.
import { spawn } from 'node:child_process';
import { writeFileSync, mkdtempSync } from 'node:fs';
import net from 'node:net';

const [port, user, pass0, outdir, variant] = process.argv.slice(2);
let pass = pass0; // aendert sich beim Pruefen der Zugangsdaten-Seite
const withMeter = variant === 'meter';
const base = `http://127.0.0.1:${port}/`;
const dbg = 9333;
const prof = mkdtempSync(outdir + '/chrome-');
const chrome = spawn('google-chrome-stable', ['--headless=new', `--remote-debugging-port=${dbg}`,
	`--user-data-dir=${prof}`, '--no-first-run', '--window-size=1100,1400', 'about:blank'],
	{ stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let targets;
for (let i = 0; i < 50; i++) {
	try { targets = await (await fetch(`http://127.0.0.1:${dbg}/json`)).json(); break; } catch { await sleep(200); }
}
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let id = 0;
const pending = new Map();
const logs = [];
const dialogs = [];
const netLog = [];
const conns = new Set(); // TCP-Verbindungen der API-Abfragen in einem Zeitfenster // HTTP-Anfragen und WebSocket-Auf-/Abbau der Seite
ws.addEventListener('message', (m) => {
	const d = JSON.parse(m.data);
	if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); return; }
	if (d.method === 'Fetch.authRequired') {
		send('Fetch.continueWithAuth', { requestId: d.params.requestId,
			authChallengeResponse: { response: 'ProvideCredentials', username: user, password: pass } });
	} else if (d.method === 'Fetch.requestPaused') {
		send('Fetch.continueRequest', { requestId: d.params.requestId });
	} else if (d.method === 'Page.javascriptDialogOpening') {
		dialogs.push(d.params.message);
		send('Page.handleJavaScriptDialog', { accept: true });
	} else if (d.method === 'Runtime.consoleAPICalled' || d.method === 'Runtime.exceptionThrown') {
		logs.push(JSON.stringify(d.params).slice(0, 300));
	}
	if (d.method === 'Network.requestWillBeSent') netLog.push('http ' + d.params.request.url);
	if (d.method === 'Network.webSocketCreated') netLog.push('ws+');
	if (d.method === 'Network.responseReceived' && d.params.type === 'Fetch') conns.add(d.params.response.connectionId);
	if (d.method === 'Network.webSocketClosed') netLog.push('ws-');
	if (d.method === 'Network.webSocketCreated' || d.method === 'Network.webSocketClosed' ||
		d.method === 'Network.webSocketHandshakeResponseReceived') {
		logs.push(d.method + ' ' + JSON.stringify(d.params.response ? d.params.response.status : d.params.url || ''));
	}
});
function send(method, params = {}) {
	return new Promise((r) => { pending.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
}
const evalJs = async (expr) => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result.value;
const shot = async (name) => {
	const r = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
	writeFileSync(`${outdir}/${name}.png`, Buffer.from(r.result.data, 'base64'));
};
const text = () => evalJs('document.body.innerText');
const click = (label) => evalJs(`(() => { const b = [...document.querySelectorAll('button')].find((x) => x.textContent.trim() === ${JSON.stringify(label)}); if (!b) return false; b.click(); return true; })()`);

// Telnet-Shell der Simulation
function shell(cmd) {
	return new Promise((res) => {
		const s = net.connect(Number(process.env.WALLFREE_TELNET_PORT || 2323), '127.0.0.1');
		let buf = '';
		s.on('data', (d) => { buf += d.toString('latin1'); });
		setTimeout(() => s.write(cmd + '\r\n'), 300);
		setTimeout(() => { s.end(); res(buf); }, 1200);
	});
}

await send('Page.enable');
await send('Runtime.enable');
await send('Network.enable');
await send('Fetch.enable', { handleAuthRequests: true, patterns: [{ urlPattern: '*' }] });

const out = {};
await send('Page.navigate', { url: base });
await sleep(3000);
out.simple1 = await text();
// Zustand am Logo-Punkt, Hinweisleiste nur bei Stoerung (Kopfzeile Variante A)
const conn = "(() => { const e = document.querySelector('.logo .conn'); const b = document.querySelector('header .banner'); return e.dataset.conn + '|' + e.title + '|' + (b ? b.innerText.trim() : '') + '|' + document.querySelector('main').className; })()";
out.mode1 = await evalJs(conn);
await shot('1-einfach');

// Ab Werk einfache Ansicht: ohne Rohwerte und ohne "Laden"; dann Expertenansicht auf der Seite System
const goEarly = async (hash) => { await evalJs(`location.hash = ${JSON.stringify(hash)}`); await sleep(2500); return text(); };
out.simpleStatus = await goEarly('#/status');
out.simpleSettings = await goEarly('#/einstellungen');
await goEarly('#/system');
out.expertOn = await evalJs(`(() => { const l = [...document.querySelectorAll('label.check')].find((x) => x.textContent.includes('Expertenansicht'));
	if (!l) return false; const c = l.querySelector('input'); c.click(); return c.checked; })()`);
await sleep(500);
out.expertStored = await evalJs("localStorage.getItem('wallfree.expert')");
await goEarly('#/');

await shell('sim car C');
await sleep(4000);
out.simple2 = await text();
out.gauge2 = await evalJs("[...document.querySelectorAll('.gauge text')].map((t) => t.textContent).join('|')");
await shot('2-einfach-laden');

out.clickOff = await click('Laden aus');
await sleep(7000);
out.simple3 = await text();
await shot('2b-einfach-laden-aus');
const go = async (hash) => { await evalJs(`location.hash = ${JSON.stringify(hash)}`); await sleep(2500); return text(); };
out.expert = await go('#/status');
// Zeigen auf das juengste Intervall: Hinweis an der Kurve
out.tipBefore = await evalJs("!!document.querySelector('.h-tip')");
await evalJs("(() => { const h = document.querySelector('.h-hit'); const b = h.getBoundingClientRect(); h.dispatchEvent(new PointerEvent('pointermove', { clientX: b.right - 1, clientY: b.top + 5, bubbles: true })); })()");
await sleep(300);
out.history = await evalJs("(() => { const s = document.querySelector('svg.hist'), t = document.querySelector('.h-tip'); return s && t ? s.querySelectorAll('path.h-line').length + '|' + t.innerText.replace(/\\n/g, ' ') : ''; })()");
await shot('3-auswertung');
out.settings = await go('#/einstellungen/zaehler');
await shot('3b-einstellungen-zaehler');
out.settingsNet = await go('#/einstellungen/netzwerk');
out.system = await go('#/system');
// Konsole (SOLL-SH-19): Kommando eingeben und ausfuehren
await evalJs(`(() => { const e = document.querySelector('input[aria-label=Kommando]');
	e.value = 'status'; e.dispatchEvent(new Event('input', { bubbles: true })); })()`);
await sleep(100);
out.consoleClick = await click('Ausführen');
await sleep(1500);
out.console = await evalJs("(document.querySelector('pre.console') || {}).innerText || ''");
// Vervollstaendigung (SOLL-SH-23): Tab ergaenzt eindeutig, Vorschlaege zum Antippen
const typeCmd = (v) => evalJs(`(() => { const e = document.querySelector('input[aria-label=Kommando]'); e.focus();
	e.value = ${JSON.stringify(v)}; e.dispatchEvent(new Event('input', { bubbles: true })); })()`);
const pressTab = () => evalJs(`document.querySelector('input[aria-label=Kommando]').dispatchEvent(
	new KeyboardEvent('keydown', { key: 'Tab', bubbles: true, cancelable: true }))`);
await typeCmd('sta');
await sleep(200);
await pressTab();
await sleep(200);
out.tab1 = await evalJs("document.querySelector('input[aria-label=Kommando]').value");
await typeCmd('param get lock_');
await sleep(1500); // Parameternamen nachladen
out.sugg = await evalJs("[...document.querySelectorAll('.sugg button')].map((b) => b.textContent).join(',')");
await shot('5-konsole');
await typeCmd('');
await evalJs('history.back()');
await sleep(1500);
out.back = await evalJs('location.hash');
await go('#/status');

await shell('sim car A');
await sleep(3000);
out.expert2 = await text();
// Status mit allen Karten: alle Abfragen ueber eine Verbindung (4 Plaetze auf der Steuerung)
conns.clear();
await go('#/einstellungen');
await go('#/status');
await sleep(8000);
out.conns = [...conns];
// Tab im Hintergrund (Status mit allen Nachladern): WebSocket zu, keine Abfragen; sichtbar wieder live
const setHidden = (h) => evalJs(`(() => { Object.defineProperty(document, 'hidden', { configurable: true, get: () => ${h} });
	document.dispatchEvent(new Event('visibilitychange')); })()`);
const nh = netLog.length;
await setHidden(true);
await sleep(500);
const n0 = netLog.length;
out.hideNet = netLog.slice(nh, n0);
await sleep(12000);
out.hiddenNet = netLog.slice(n0);
await setHidden(false);
await sleep(3000);
out.visibleMode = await evalJs(conn);
out.visibleNet = netLog.slice(n0 + out.hiddenNet.length);
// Verbindungsabbruch: Netz im Browser trennen, nach OFFLINE_MS grau; danach Wiederkehr
await send('Network.emulateNetworkConditions', { offline: true, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
await sleep(7000);
out.mode2 = await evalJs(conn);
await shot('4-getrennt');
await send('Network.emulateNetworkConditions', { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
await sleep(5000);
out.mode3 = await evalJs(conn);

// Eingaben wie ein Nutzer: Wert setzen und das Ereignis ausloesen, auf das Preact hoert
const setField = (sel, value, ev) => evalJs(`(() => { const e = ${sel}; if (!e) return false;
	if (e.type === 'checkbox') e.checked = ${JSON.stringify(value)}; else e.value = ${JSON.stringify(value)};
	e.dispatchEvent(new Event(${JSON.stringify(ev)}, { bubbles: true })); return true; })()`);
const paramRow = (name) => `[...document.querySelectorAll('td[title]')].find((t) => t.title === '${name}').parentElement`;
const api = (path) => evalJs(`fetch(${JSON.stringify(path)}).then((r) => r.json())`);

// Einstellungen/Laden: Auswahlliste und Kaestchen schreiben, danach zurueck
await go('#/einstellungen/laden');
const p0 = await api('/evse/params');
out.selSet = await setField(`${paramRow('wakeup_mode')}.querySelector('select')`, '1', 'change');
await evalJs(`${paramRow('wakeup_mode')}.querySelector('button').click()`);
out.boxSet = await setField(`${paramRow('reject_state_d')}.querySelector('input[type=checkbox]')`, !p0.reject_state_d, 'change');
await evalJs(`${paramRow('reject_state_d')}.querySelector('button').click()`);
await sleep(1500);
const p1 = await api('/evse/params');
out.params = [p0.wakeup_mode, p1.wakeup_mode, p0.reject_state_d, p1.reject_state_d];
out.paramMsg = await evalJs(`${paramRow('wakeup_mode')}.innerText`);
await evalJs(`fetch('/evse/params', { method: 'POST', body: JSON.stringify({ wakeup_mode: ${p0.wakeup_mode}, reject_state_d: ${p0.reject_state_d} }) })`);

// Ereignisprotokoll per Klick loeschen (mit Rueckfrage)
await go('#/status');
out.clearClick = await click('Protokoll löschen');
await sleep(2500);
out.afterClear = await text();
const log = await api('/system/log');
out.clearLog = log.entries.map((e) => e.kind + (e.addr ? ' ' + e.addr : '')).join(',');

// Netzwerk: Wert speichern, der erst nach Neustart wirkt; "Jetzt neu starten"
const cfg0 = await api('/system/config');
await go('#/einstellungen/netzwerk');
await setField(`document.querySelector('label[title=net_hostname] input')`, 'browsertest', 'input');
await click('Speichern');
await sleep(1500);
out.netSaved = await text();
const boot0 = (await api('/system/log')).boot;
out.restartClick = await click('Jetzt neu starten');
await sleep(32000); // Seite laedt nach 25 s neu, Simulation startet per execv neu
const si = await api('/system/info');
const cfg1 = await api('/system/config');
out.restart = [si.reset_reason, cfg1.active.net_hostname, (await api('/system/log')).boot - boot0];
await evalJs(`fetch('/system/config', { method: 'POST', body: JSON.stringify({ net_hostname: ${JSON.stringify(cfg0.saved.net_hostname)} }) })`);

// Zugangsdaten: Benutzer vorbelegt, Passwort aendern (Seite laedt neu, neue Anmeldung), zurueck
const changePw = async (pw) => {
	await go('#/einstellungen/zugang');
	const u = await evalJs(`document.querySelector('input[autocomplete=username]').value`);
	await evalJs(`[...document.querySelectorAll('input[type=password]')].forEach((e) => { e.value = ${JSON.stringify(pw)};
		e.dispatchEvent(new Event('input', { bubbles: true })); })`);
	await click('Speichern');
	await sleep(1000);
	const msg = await text();
	pass = pw; // Chrome fragt nach dem Neuladen neu an
	await sleep(4000);
	return [u, msg.includes('Bitte mit den neuen Zugangsdaten anmelden'), (await api('/system/info')).firmware];
};
out.pw1 = await changePw(pass0 + '-neu');
out.pw2 = await changePw(pass0);
out.dialogs = dialogs;
out.logs = logs;
writeFileSync(`${outdir}/browse.json`, JSON.stringify(out, null, 1));
ws.close();
chrome.kill();

const checks = [
	['einfache Ansicht ab Werk: Status ohne Rohwerte', /ereignisprotokoll/i.test(out.simpleStatus)
		&& !/ladelogik/i.test(out.simpleStatus) && !/cp-oszilloskop/i.test(out.simpleStatus)],
	['Status: Verbindungen (Netzwerk, Uhrzeit, MQTT, Zaehler)', /verbindungen/i.test(out.simpleStatus)
		&& /Netzwerk\s+Adresse per DHCP/.test(out.simpleStatus) && /MQTT\s+aus/.test(out.simpleStatus)],
	['einfache Ansicht ab Werk: kein Reiter Laden', !out.simpleSettings.includes('Laden\n') && out.simpleSettings.includes('Netzwerk')
		&& !/konfiguration \(gespeichert\)/i.test(out.simpleSettings)],
	['Expertenansicht einschalten, im Browser gespeichert', out.expertOn === true && out.expertStored === '1'],
	['WebSocket live', out.mode1.startsWith('verbunden|verbunden – live') && out.mode1.includes('||')],
	['getrennt: grau, Hinweisleiste', out.mode2.startsWith('getrennt|') && out.mode2.includes('Verbindung zur Wallbox getrennt')
		&& out.mode2.endsWith('|offline')],
	['wieder verbunden, ohne Hinweisleiste', out.mode3.startsWith('verbunden|') && out.mode3.includes('||')
		&& !out.mode3.endsWith('offline')],
	['Ereignis Start (live nachgereicht)', out.simple2.includes('S0 Start → S1 Bereit')],
	['Laden', out.simple2.includes('Lädt') && out.simple2.includes('Schütz ein')],
	['Maximum wirkt sichtbar', out.simple2.includes('begrenzt durch Maximum Drehschalter (32 A)')],
	['Laden aus', out.simple3.includes('S7 Beenden → S3 Warten') && out.simple3.includes('Vorgabe: Laden aus')],
	['Status: Ladestrom als Werte', out.expert.includes('Maximum Drehschalter') && out.expert.includes('Erlaubt')],
	['Status: Verlauf ohne Ablesezeile, Hinweis nur beim Zeigen', !out.tipBefore && out.history.includes('jetzt')],
	['Status: Verlauf (laufendes Intervall)', /jetzt .* · (lädt|angesteckt)/.test(out.history)],
	['Einstellungen Netzwerk ohne Zustandszeile', out.settingsNet.includes('DHCP') && !out.settingsNet.includes('Telnet')
		&& !out.settingsNet.includes('Broker') && !out.settingsNet.includes('Aktiv:')],
	['System', out.system.includes('Firmware') && out.system.includes('Neu starten')],
	['System: Konsole (Expertenansicht)', out.consoleClick && out.console.includes('› status')
		&& out.console.includes('Zustand  S')],
	['Konsole: Tab ergaenzt', out.tab1 === 'status '],
	['Konsole: Vorschlaege Parameternamen', out.sugg.includes('lock_pulse') && out.sugg.includes('lock_cycle_gap')
		&& !out.sugg.includes('unlock_pulse')],
	['System: Lizenzhinweis', out.system.includes('Apache License 2.0') && out.system.includes('Lizenzhinweise')],
	['Zurueck-Taste', out.back === '#/einstellungen/netzwerk'],
	['Ereignisprotokoll mit Loeschen', out.expert2.includes('Protokoll löschen')],
	['Entriegelt nach Abstecken', out.expert2.includes('Verriegelung: Entriegelt')],
	['Laden: Auswahlliste geschrieben', out.selSet && out.params[0] !== out.params[1] && out.params[1] === 1],
	['Laden: Kaestchen geschrieben', out.boxSet && out.params[2] !== out.params[3]],
	['Laden: Meldung "gespeichert"', out.paramMsg.includes('gespeichert')],
	['Protokoll loeschen: Rueckfrage, nur Loescheintrag', out.clearClick && dialogs.some((d) => d.includes('Ereignisprotokolls löschen'))
		&& out.clearLog.startsWith('cleared 127.0.0.1') && out.afterClear.includes('Ereignisprotokoll gelöscht von 127.0.0.1')],
	['Netzwerk: Hinweis Neustart', out.netSaved.includes('wirkt nach Neustart') && out.netSaved.includes('Jetzt neu starten')
		&& out.netSaved.includes('Status → Verbindungen')],
	['Neustart ueber die Seite: Rueckfrage, Neustart, Wert aktiv', out.restartClick && dialogs.some((d) => d.includes('neu starten'))
		&& out.restart[0] === 'user_restart' && out.restart[1] === 'browsertest' && out.restart[2] === 1],
	['Zugangsdaten: Benutzer vorbelegt, Aenderung und Neuanmeldung', out.pw1[0] === user && out.pw1[1] && !!out.pw1[2]
		&& out.pw2[1] && !!out.pw2[2]],
	['Abfragen ueber eine Verbindung', out.conns.length === 1],
	['Hintergrund: keine Anfragen, WebSocket zu', out.hiddenNet.length === 0 && out.hideNet.includes('ws-')],
	['Hintergrund: sichtbar wieder live', out.visibleMode.includes('live (WebSocket)') && out.visibleNet.includes('ws+')
		&& out.visibleNet.some((u) => u.includes('/evse/errors'))],
	['keine Skriptfehler', !logs.some((l) => l.includes('exceptionDetails'))],
];
if (withMeter) {
	checks.push(
		// Kartentitel stehen per CSS in Grossbuchstaben (innerText)
		['Zaehler: Leistung beim Laden', /zähler\n[\d.,]+ kW\nZählerstand [\d.,]+ kWh\nverbunden/i.test(out.simple2)],
		['Zaehler: Messwert im Ring', /Messwert\|[\d.,]+ kW · erlaubt 32 A/.test(out.gauge2)],
		['Zaehler: Messwerte und Fehlerzaehler', /zähler \(nur anzeige\)/i.test(out.expert) &&
			out.expert.includes('Spannung L1') && out.expert.includes('requests')],
		['Zaehler: Leistung im Verlauf', out.history.startsWith('2|') && /kW/.test(out.history)],
		['Zaehler: Auswahl in Einstellungen', out.settings.includes('Eastron SDM630')],
		['Zaehler: Zustand unter Verbindungen', /Zähler\s+verbunden/.test(out.expert)]);
} else {
	checks.push(['ohne Zaehler: Ring zeigt erlaubt', out.gauge2.includes('|32 A|erlaubt')]);
	checks.push(['ohne Zaehler: kein Messwert', !out.gauge2.includes('Messwert') && !out.simple2.includes('Messwert')]);
	checks.push(['ohne Zaehler: leere Zaehlerkachel', /zähler\n/i.test(out.simple1) && !/ kW/.test(out.simple1)]);
	checks.push(['ohne Zaehler: Verlauf nur Strom', out.history.startsWith('1|') && !/kW/.test(out.history)]);
}
let bad = 0;
for (const [name, ok] of checks) { console.log((ok ? 'ok   ' : 'FAIL ') + name); if (!ok) bad++; }
process.exit(bad ? 1 : 0);
