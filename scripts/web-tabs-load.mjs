// SPDX-License-Identifier: Apache-2.0
// Browser-Tabs gegen die Steuerung: N headless-Chrome-Instanzen mit je einem Tab auf Status
// (Expertenansicht), die ersten H verborgen (document.hidden). Meldet je Tab Anfragen und
// TCP-Verbindungen der API im Messfenster; die Erreichbarkeit daneben getrennt messen.
// Beendet alle Chrome-Prozesse am Ende.
//   node scripts/web-tabs-load.mjs <host> <user> <passwort> <tabs> <sekunden> <verborgen> <ausgabe>
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
const [host, user, pass, nTabs, secs, nHidden, outdir] = process.argv.slice(2);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const procs = [], profs = [];
const cleanup = () => { for (const p of procs) try { process.kill(-p.pid, 'SIGKILL'); } catch (e) {} for (const d of profs) rmSync(d, { recursive: true, force: true }); };
process.on('exit', cleanup); process.on('SIGINT', () => process.exit(1)); process.on('SIGTERM', () => process.exit(1));
async function tab(i) {
	const port = 9400 + i, prof = mkdtempSync(outdir + '/chrome-'); profs.push(prof);
	const p = spawn('google-chrome-stable', ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${prof}`, '--no-first-run', 'about:blank'], { stdio: 'ignore', detached: true });
	procs.push(p);
	let targets;
	for (let k = 0; k < 50; k++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); break; } catch (e) { await sleep(200); } }
	const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
	await new Promise((r) => ws.addEventListener('open', r));
	let id = 0; const pend = new Map(); const conns = new Set(); let reqs = 0;
	ws.addEventListener('message', (m) => {
		const d = JSON.parse(m.data);
		if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); return; }
		if (d.method === 'Fetch.authRequired') send('Fetch.continueWithAuth', { requestId: d.params.requestId, authChallengeResponse: { response: 'ProvideCredentials', username: user, password: pass } });
		else if (d.method === 'Fetch.requestPaused') send('Fetch.continueRequest', { requestId: d.params.requestId });
		if (d.method === 'Network.requestWillBeSent') reqs++;
		if (d.method === 'Network.responseReceived' && d.params.type === 'Fetch') conns.add(d.params.response.connectionId);
	});
	const send = (method, params = {}) => new Promise((r) => { pend.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
	const ev = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result.result.value;
	await send('Network.enable'); await send('Fetch.enable', { handleAuthRequests: true, patterns: [{ urlPattern: '*' }] });
	await send('Page.navigate', { url: `http://${host}/#/status` }); await sleep(3000);
	await ev("localStorage.setItem('wallfree.expert', '1'); location.reload()"); await sleep(4000);
	const hidden = i < Number(nHidden);
	if (hidden) await ev("Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }); document.dispatchEvent(new Event('visibilitychange'))");
	return { i, hidden, ev, stat: () => ({ reqs, conns: conns.size }), reset: () => { reqs = 0; conns.clear(); } };
}
const tabs = [];
for (let i = 0; i < Number(nTabs); i++) tabs.push(await tab(i));
await sleep(1000);
for (const t of tabs) t.reset();
console.log(`START ${new Date().toISOString()} ${nTabs} Tabs, ${nHidden} verborgen`);
await sleep(Number(secs) * 1000);
for (const t of tabs) {
	const conn = await t.ev("(() => { const e = document.querySelector('.logo .conn'); return e ? e.title : '?'; })()");
	console.log(`Tab ${t.i} ${t.hidden ? 'verborgen' : 'sichtbar '}: ${JSON.stringify(t.stat())} ${conn}`);
}
process.exit(0);
