// SPDX-License-Identifier: Apache-2.0
// Screenshots fuer die Anleitung der Weboberflaeche (docs/anleitung/) aus der PC-Simulation.
// Startet von scripts/doc-screenshots.sh: Simulation fabrikneu, 60-fach beschleunigt
// (--rt-ratio=60), damit der Verlauf in wenigen Minuten einige Stunden fuellt.
//   node scripts/doc-screenshots.mjs <http-port> <telnet-port> <bildverzeichnis>
import { spawn, execFileSync } from 'node:child_process';
import { writeFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import net from 'node:net';

const [port, tport, outdir] = process.argv.slice(2);
const base = `http://127.0.0.1:${port}`;
const USER = 'admin', PASS = 'Wallfree-Anleitung';
let cred = ['admin', 'wallfree'];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// DOC_QUICK=1: Ablauf ohne Ladegeschichte (Selektoren pruefen)
const HOUR = process.env.DOC_QUICK ? 1000 : 60000; // 1 h Simulationszeit bei --rt-ratio=60

const dbg = 9355;
const chrome = spawn('google-chrome-stable', ['--headless=new', '--lang=de-DE', `--remote-debugging-port=${dbg}`,
	`--user-data-dir=${mkdtempSync(tmpdir() + '/wf-doc-')}`, '--no-first-run', '--hide-scrollbars',
	'--force-color-profile=srgb', 'about:blank'], { stdio: 'ignore', env: { ...process.env, LANGUAGE: 'de', LANG: 'de_DE.UTF-8', LC_ALL: 'de_DE.UTF-8' } });
let targets;
for (let i = 0; i < 50; i++) {
	try { targets = await (await fetch(`http://127.0.0.1:${dbg}/json`)).json(); break; } catch { await sleep(200); }
}
const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let id = 0;
const pending = new Map();
ws.addEventListener('message', (m) => {
	const d = JSON.parse(m.data);
	if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); return; }
	if (d.method === 'Fetch.authRequired') {
		send('Fetch.continueWithAuth', { requestId: d.params.requestId,
			authChallengeResponse: { response: 'ProvideCredentials', username: cred[0], password: cred[1] } });
	} else if (d.method === 'Fetch.requestPaused') {
		send('Fetch.continueRequest', { requestId: d.params.requestId });
	}
});
function send(method, params = {}) {
	return new Promise((r) => { pending.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
}
const js = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true, awaitPromise: true })).result.result.value;
const view = (w, h) => send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 2, mobile: w < 600 });
const go = async (hash, ms = 2500) => { await js(`location.hash = ${JSON.stringify(hash)}`); await sleep(ms); };

// Ausschnitt: ganze Seite oder eine Karte (Titel, Gross-/Kleinschreibung egal; erste Karte
// in main bei "main section") mit Hoehe h
async function shot(name, sel, h) {
	let clip;
	if (sel) {
		const r = await js(`(() => { const t = ${JSON.stringify(sel)}; const e = t === 'main section'
			? document.querySelector(t) : [...document.querySelectorAll('section.card')].find((x) =>
				x.querySelector('h2') && x.querySelector('h2').textContent.trim().toLowerCase() === t.toLowerCase());
			if (!e) return null; const r = e.getBoundingClientRect(); return [r.left, r.top + scrollY, r.width, r.height]; })()`);
		if (!r) throw new Error('Karte nicht gefunden: ' + sel);
		clip = { x: Math.max(0, r[0] - 12), y: Math.max(0, r[1] - 12), width: r[2] + 24, height: (h || r[3]) + 24, scale: 1 };
	} else {
		const hh = await js("Math.ceil(document.querySelector('main').getBoundingClientRect().bottom + scrollY)");
		const w = await js('innerWidth');
		clip = { x: 0, y: 0, width: w, height: h || hh, scale: 1 };
	}
	const r = await send('Page.captureScreenshot', { format: 'png', clip, captureBeyondViewport: true });
	writeFileSync(`${outdir}/${name}.png`, Buffer.from(r.result.data, 'base64'));
	console.log('Bild', name);
}

function shell(cmd) {
	return new Promise((res) => {
		const s = net.connect(Number(tport), '127.0.0.1');
		s.on('data', () => {});
		setTimeout(() => s.write(cmd + '\r\n'), 300);
		setTimeout(() => { s.end(); res(); }, 800);
	});
}
const curl = (path, body) => execFileSync('curl', ['-s', '--digest', '-u', cred.join(':'),
	...(body ? ['-H', 'Content-Type: application/json', '-d', JSON.stringify(body)] : []), base + path]).toString();
const setCurrent = (ma) => curl('/evse/external_current', { current: ma });

await send('Page.enable');
await send('Network.enable'); // vor dem WebSocket-Aufbau, sonst wirkt "offline" nicht auf ihn
await send('Fetch.enable', { handleAuthRequests: true, patterns: [{ urlPattern: '*' }] });
await view(1100, 800);

// 1. Werkszugang
await send('Page.navigate', { url: base + '/' });
await sleep(3000);
await shot('erstanmeldung', 'main section');
curl('/credentials', { user: USER, password: PASS });
cred = [USER, PASS];

// Zaehler einrichten, Neustart (Simulation startet sich selbst neu)
curl('/system/config', { meter_type: 2, net_hostname: 'wallfree-garage' });
curl('/system/restart', {});
await sleep(8000);

// 2. Ladegeschichte: Abend mit 16 A, geregelt 10/6 A, voll; spaeter erneut angesteckt
await shell('sim car B');
await sleep(0.3 * HOUR);
setCurrent(16000);
await shell('sim car C');
await sleep(1.5 * HOUR);
setCurrent(10000);
await sleep(1 * HOUR);
setCurrent(7000);
await sleep(0.6 * HOUR);
await shell('sim car B');
await sleep(0.4 * HOUR);
await shell('sim car A');
await sleep(1.7 * HOUR);
await shell('sim car B');
await sleep(0.2 * HOUR);
setCurrent(11000);
await shell('sim car C');
await sleep(0.6 * HOUR);

// 3. Bilder
await send('Page.navigate', { url: base + '/#/' });
await send('Page.reload', { ignoreCache: true }); // nur der Hash aendert sich: sonst kein Neuladen
await sleep(4000);
await shot('uebersicht');
await go('#/insights', 4000);
// Hinweis an der Kurve zeigen (Zeigen auf ein Intervall des ersten Ladevorgangs)
await js(`(() => { const h = document.querySelector('.h-hit'); const b = h.getBoundingClientRect(); h.dispatchEvent(new PointerEvent('pointermove', { clientX: b.left + b.width * 0.35, clientY: b.top + 20, bubbles: true })); })()`);
await sleep(400);
await shot('insights-verlauf', 'main section');
await js('document.querySelector(".h-hit").dispatchEvent(new PointerEvent("pointerleave"))');
await shot('insights-ladestrom', 'Ladestrom');
await shot('insights-fehler', 'Fehler');
await shot('insights-zaehler', 'Zähler (nur Anzeige)');
await shot('insights-protokoll', 'Ereignisprotokoll', 420);
await shot('insights-status', 'Status der Ladelogik', 360);
for (const p of ['laden', 'netzwerk', 'mqtt', 'zaehler', 'zugang']) {
	await go('#/einstellungen/' + p);
	await shot('einstellungen-' + p, null, p === 'laden' ? 760 : null);
}
await go('#/system');
await shot('system');

// Handy
await view(390, 844);
await go('#/');
await sleep(1500);
await shot('handy-uebersicht', null, 844);
await view(1100, 800);

// Getrennt: Netz im Browser trennen
await go('#/');
await send('Network.emulateNetworkConditions', { offline: true, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
await sleep(7000);
await shot('getrennt', null, 420);
await send('Network.emulateNetworkConditions', { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });

ws.close();
chrome.kill();
process.exit(0);
