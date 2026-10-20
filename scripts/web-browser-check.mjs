// SPDX-License-Identifier: Apache-2.0
// Browser-Durchlauf der Weboberflaeche gegen die laufende Simulation (scripts/sim-run.sh):
// headless Chrome ueber das DevTools-Protokoll (Node >= 22, google-chrome-stable), Digest-
// Anmeldung durch Chrome selbst, Fahrzeug ueber die Telnet-Shell (Port 2323).
// Zugangsdaten muessen bereits geaendert sein (Werkszugang zeigt nur die Zugangsdaten-Seite).
//   node scripts/web-browser-check.mjs <http-port> <user> <passwort> <ausgabeverzeichnis>
// Ergebnis: Screenshots und browse.json im Ausgabeverzeichnis, Exitcode 1 bei Fehlschlag.
import { spawn } from 'node:child_process';
import { writeFileSync, mkdtempSync } from 'node:fs';
import net from 'node:net';

const [port, user, pass, outdir] = process.argv.slice(2);
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
		const s = net.connect(2323, '127.0.0.1');
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
out.mode1 = await evalJs("document.querySelector('header .grow').innerText");
await shot('1-einfach');

await shell('sim car C');
await sleep(4000);
out.simple2 = await text();
await shot('2-einfach-laden');

out.clickOff = await click('Laden aus');
await sleep(7000);
out.simple3 = await text();
await shot('2b-einfach-laden-aus');
out.clickExpert = await click('Experte');
await sleep(2500);
out.expert = await text();
await shot('3-experte');

await shell('sim car A');
await sleep(3000);
out.expert2 = await text();
out.logs = logs;
writeFileSync(`${outdir}/browse.json`, JSON.stringify(out, null, 1));
ws.close();
chrome.kill();

const checks = [
	['WebSocket live', out.mode1.includes('live')],
	['Ereignis Start (live nachgereicht)', out.simple2.includes('S0 Start → S1 Bereit')],
	['Laden', out.simple2.includes('Lädt') && out.simple2.includes('Schütz ein')],
	['Laden aus', out.simple3.includes('S7 Beenden → S3 Warten')],
	['Zugriffsprotokoll', /zugriffsprotokoll/i.test(out.expert2)],
	['Entriegelt nach Abstecken', out.expert2.includes('Verriegelung: Entriegelt')],
	['keine Skriptfehler', !logs.some((l) => l.includes('exceptionDetails'))],
];
let bad = 0;
for (const [name, ok] of checks) { console.log((ok ? 'ok   ' : 'FAIL ') + name); if (!ok) bad++; }
process.exit(bad ? 1 : 0);
