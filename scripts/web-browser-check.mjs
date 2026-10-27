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

const [port, user, pass, outdir, variant] = process.argv.slice(2);
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
ws.addEventListener('message', (m) => {
	const d = JSON.parse(m.data);
	if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); return; }
	if (d.method === 'Fetch.authRequired') {
		send('Fetch.continueWithAuth', { requestId: d.params.requestId,
			authChallengeResponse: { response: 'ProvideCredentials', username: user, password: pass } });
	} else if (d.method === 'Fetch.requestPaused') {
		send('Fetch.continueRequest', { requestId: d.params.requestId });
	} else if (d.method === 'Runtime.consoleAPICalled' || d.method === 'Runtime.exceptionThrown') {
		logs.push(JSON.stringify(d.params).slice(0, 300));
	} else if (d.method === 'Network.webSocketCreated' || d.method === 'Network.webSocketClosed' ||
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
out.expert = await go('#/insights');
// Zeigen auf das juengste Intervall: Hinweis an der Kurve
out.tipBefore = await evalJs("!!document.querySelector('.h-tip')");
await evalJs("(() => { const h = document.querySelector('.h-hit'); const b = h.getBoundingClientRect(); h.dispatchEvent(new PointerEvent('pointermove', { clientX: b.right - 1, clientY: b.top + 5, bubbles: true })); })()");
await sleep(300);
out.history = await evalJs("(() => { const s = document.querySelector('svg.hist'), t = document.querySelector('.h-tip'); return s && t ? s.querySelectorAll('path.h-line').length + '|' + t.innerText.replace(/\\n/g, ' ') : ''; })()");
await shot('3-insights');
out.settings = await go('#/einstellungen/zaehler');
await shot('3b-einstellungen-zaehler');
out.settingsNet = await go('#/einstellungen/netzwerk');
out.system = await go('#/system');
await evalJs('history.back()');
await sleep(1500);
out.back = await evalJs('location.hash');
await go('#/insights');

await shell('sim car A');
await sleep(3000);
out.expert2 = await text();
// Verbindungsabbruch: Netz im Browser trennen, nach OFFLINE_MS grau; danach Wiederkehr
await send('Network.emulateNetworkConditions', { offline: true, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
await sleep(7000);
out.mode2 = await evalJs(conn);
await shot('4-getrennt');
await send('Network.emulateNetworkConditions', { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
await sleep(5000);
out.mode3 = await evalJs(conn);
out.logs = logs;
writeFileSync(`${outdir}/browse.json`, JSON.stringify(out, null, 1));
ws.close();
chrome.kill();

const checks = [
	['WebSocket live', out.mode1.startsWith('verbunden|verbunden – live') && out.mode1.includes('||')],
	['getrennt: grau, Hinweisleiste', out.mode2.startsWith('getrennt|') && out.mode2.includes('Verbindung zur Wallbox getrennt')
		&& out.mode2.endsWith('|offline')],
	['wieder verbunden, ohne Hinweisleiste', out.mode3.startsWith('verbunden|') && out.mode3.includes('||')
		&& !out.mode3.endsWith('offline')],
	['Ereignis Start (live nachgereicht)', out.simple2.includes('S0 Start → S1 Bereit')],
	['Laden', out.simple2.includes('Lädt') && out.simple2.includes('Schütz ein')],
	['Maximum wirkt sichtbar', out.simple2.includes('begrenzt durch Maximum Drehschalter (32 A)')],
	['Laden aus', out.simple3.includes('S7 Beenden → S3 Warten') && out.simple3.includes('Vorgabe: Laden aus')],
	['Insights: Ladestrom als Werte', out.expert.includes('Maximum Drehschalter') && out.expert.includes('Erlaubt')],
	['Insights: Verlauf ohne Ablesezeile, Hinweis nur beim Zeigen', !out.tipBefore && out.history.includes('jetzt')],
	['Insights: Verlauf (laufendes Intervall)', /jetzt .* · (lädt|angesteckt)/.test(out.history)],
	['Einstellungen Netzwerk', out.settingsNet.includes('DHCP') && out.settingsNet.includes('Telnet') && !out.settingsNet.includes('Broker')],
	['System', out.system.includes('Firmware') && out.system.includes('Neu starten')],
	['Zurueck-Taste', out.back === '#/einstellungen/netzwerk'],
	['Log mit Loeschen', out.expert2.includes('Log löschen')],
	['Entriegelt nach Abstecken', out.expert2.includes('Verriegelung: Entriegelt')],
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
		['Zaehler: Auswahl in Einstellungen', out.settings.includes('Eastron SDM630') && out.settings.includes('Zustand: verbunden')]);
} else {
	checks.push(['ohne Zaehler: Ring zeigt erlaubt', out.gauge2.includes('|32 A|erlaubt')]);
	checks.push(['ohne Zaehler: kein Messwert', !out.gauge2.includes('Messwert') && !out.simple2.includes('Messwert')]);
	checks.push(['ohne Zaehler: leere Zaehlerkachel', /zähler\n/i.test(out.simple1) && !/ kW/.test(out.simple1)]);
	checks.push(['ohne Zaehler: Verlauf nur Strom', out.history.startsWith('1|') && !/kW/.test(out.history)]);
}
let bad = 0;
for (const [name, ok] of checks) { console.log((ok ? 'ok   ' : 'FAIL ') + name); if (!ok) bad++; }
process.exit(bad ? 1 : 0);
