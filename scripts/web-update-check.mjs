// SPDX-License-Identifier: Apache-2.0
// Browser-Durchlauf des Firmware-Updates (SOLL-BOOT-32) gegen die Simulation mit dem Ersatz-
// Bootloader (scripts/bootloader-sim.py, WALLFREE_SIM_BOOTLOADER, sim-run.sh setzt ihn).
// headless Chrome ueber das DevTools-Protokoll wie scripts/web-browser-check.mjs.
// Zugangsdaten muessen bereits geaendert sein.
//   node scripts/web-update-check.mjs <http-port> <user> <passwort> <ausgabeverzeichnis>
// Prueft: Vorpruefung (Wallfree-Abbild mit Version, ungueltige Datei), Ablauf bis
// "Update erfolgreich". Ergebnis: Screenshots, update.json, Exitcode 1 bei Fehlschlag.
import { spawn } from 'node:child_process';
import { writeFileSync, mkdtempSync } from 'node:fs';

const [port, user, pass, outdir] = process.argv.slice(2);
const base = `http://127.0.0.1:${port}/`;
const dbg = 9334;
const prof = mkdtempSync(outdir + '/chrome-');
const chrome = spawn('google-chrome-stable', ['--headless=new', `--remote-debugging-port=${dbg}`,
	`--user-data-dir=${prof}`, '--no-first-run', '--window-size=1100,1200', 'about:blank'],
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
const dialogs = [];
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

// Testabbild: gueltiger Start, Kennung mit der laufenden Version (die Simulation bleibt
// nach dem Update dieselbe Firmware, die Oberflaeche vergleicht die Version)
function image(version, sp = 0x20020000) {
	const tag = Buffer.from('WALLFREE-IMAGE:' + version + '\0');
	const b = Buffer.alloc(60000 + tag.length);
	for (let i = 8; i < 60000; i++) b[i] = (i * 13) & 0xff;
	b.writeUInt32LE(sp, 0);
	b.writeUInt32LE(0x08010201, 4);
	tag.copy(b, 60000);
	const f = `${outdir}/test-${sp.toString(16)}.bin`;
	writeFileSync(f, b);
	return f;
}
async function choose(file) {
	const doc = await send('DOM.getDocument', { depth: -1 });
	const q = await send('DOM.querySelector', { nodeId: doc.result.root.nodeId, selector: 'input[type=file]' });
	await send('DOM.setFileInputFiles', { nodeId: q.result.nodeId, files: [file] });
	await sleep(500);
}

await send('Page.enable');
await send('Runtime.enable');
await send('DOM.enable');
await send('Fetch.enable', { handleAuthRequests: true, patterns: [{ urlPattern: '*' }] });

const out = {};
await send('Page.navigate', { url: base + '#/system' });
await sleep(3000);
const version = await evalJs("fetch('/system/info').then((r) => r.json()).then((j) => j.firmware)");
out.version = version;
await choose(image(version, 0xffffffff));
out.bad = await text();
await choose(image(version));
out.good = await text();
await shot('1-update-auswahl');
out.started = await click('Update starten');
let t = '';
for (let i = 0; i < 90 && !/Update (erfolgreich|fehlgeschlagen)|antwortet nicht|meldet sich nicht/.test(t); i++) {
	await sleep(1000);
	t = await text();
	if (i === 8) await shot('2-update-laeuft');
}
out.done = t;
out.dialogs = dialogs;
await shot('3-update-fertig');
await sleep(4000); // Seite laedt nach Erfolg neu
out.after = await text();
writeFileSync(`${outdir}/update.json`, JSON.stringify(out, null, 1));
ws.close();
chrome.kill();

const checks = [
	['Vorpruefung lehnt ungueltige Datei ab', out.bad.includes('Kein Firmware-Abbild')],
	['Vorpruefung erkennt Wallfree mit Version', out.good.includes('Wallfree ' + version)],
	['Rueckfrage nennt die Version', dialogs.length === 1 && dialogs[0].includes(version)],
	['Ablauf bis Erfolg', out.done.includes('Update erfolgreich')],
	['nach Neuladen: letztes Update erfolgreich', out.after.includes('Letztes Update') && out.after.includes('erfolgreich')],
];
let fail = 0;
for (const [n, ok] of checks) {
	console.log((ok ? 'ok   ' : 'FAIL ') + n);
	if (!ok) fail++;
}
process.exit(fail ? 1 : 0);
