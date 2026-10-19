// SPDX-License-Identifier: Apache-2.0
// Weboberflaeche Wallfree (SOLL-API-31): nutzt nur die HTTP-/WebSocket-API.
import { html, render, useState, useEffect } from './vendor.js';

// ------------------------------------------------------------ API

async function api(path, opts) {
	const r = await fetch(path, Object.assign({ cache: 'no-store' }, opts));
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

// ------------------------------------------------------------ Texte

const CHARGER = ['Kein Fahrzeug', 'Wartet auf Freigabe', 'Bereit', 'Lädt', 'Fehler'];
const IEC = ['A – kein Fahrzeug', 'B – verbunden', 'C – lädt', 'D – lädt (Lüftung)', 'E/F – Fehler'];
const ERRSTATE = ['Kein Fehler', 'Fehler (Details: Experte)', 'Schalter-/Konfigurationsfehler',
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

// ------------------------------------------------------------ Live-Daten

// WebSocket /ws; schliesst der Server sofort (z. B. Browser sendet keine
// Anmeldung beim Aufbau), wird stattdessen alle 2 s abgefragt.
function useLive() {
	const [state, setState] = useState(null);
	const [ext, setExt] = useState(null);
	const [mode, setMode] = useState('…');

	useEffect(() => {
		let ws = null, timer = null, stop = false;
		const poll = async () => {
			try {
				setState(await api('/evse/state'));
				setExt(await api('/evse/external_current'));
			} catch (e) { /* naechster Versuch */ }
		};
		const startPoll = () => {
			if (timer) return;
			setMode('Abfrage');
			poll();
			timer = setInterval(poll, 2000);
		};
		const connect = () => {
			const opened = Date.now();
			let got = false;
			ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
			ws.onmessage = (ev) => {
				got = true;
				setMode('live');
				for (const line of ev.data.split('\n')) {
					if (!line.trim()) continue;
					const m = JSON.parse(line);
					if (m.topic === 'evse/state') setState(m.payload);
					if (m.topic === 'evse/external_current') setExt(m.payload);
				}
			};
			ws.onclose = () => {
				if (stop) return;
				if (!got || Date.now() - opened < 3000) startPoll();
				else setTimeout(connect, 3000);
			};
		};
		connect();
		return () => { stop = true; if (ws) ws.close(); clearInterval(timer); };
	}, []);
	return { state, ext, mode, setExt };
}

// ------------------------------------------------------------ Bausteine

function Card({ title, children }) {
	return html`<section class="card"><h2>${title}</h2>${children}</section>`;
}

function Msg({ m }) {
	if (!m) return null;
	return html`<div class="msg ${m.ok ? 'ok' : 'bad'}">${m.text}</div>`;
}

// ------------------------------------------------------------ Einfach

function CurrentControl({ ext, setExt }) {
	const [val, setVal] = useState(null);
	const [msg, setMsg] = useState(null);
	const cur = ext ? ext.current : 0;
	const shown = val !== null ? val : Math.max(6, Math.round(cur / 1000) || 16);

	const send = async (ma) => {
		try {
			setExt(await post('/evse/external_current', { current: ma }));
			setMsg({ ok: true, text: ma ? 'Gesetzt: ' + amps(ma) : 'Laden aus' });
			setVal(null);
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	return html`<${Card} title="Ladestrom (Schnittstelle)">
		<div class="big">${ext ? (cur ? amps(cur) : 'aus') : '–'}</div>
		<div class="row">
			<input type="range" min="6" max="32" step="1" value=${shown}
				onInput=${(e) => setVal(+e.target.value)} />
			<span>${shown} A</span>
		</div>
		<div class="row">
			<button class="primary" onClick=${() => send(shown * 1000)}>Übernehmen</button>
			<button onClick=${() => send(0)}>Laden aus</button>
		</div>
		<${Msg} m=${msg} />
		<p class="muted msg">Ist evcc verbunden, überschreibt evcc diesen Wert.</p>
	<//>`;
}

function Simple({ live }) {
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
		<${CurrentControl} ext=${live.ext} setExt=${live.setExt} />
	</div>`;
}

// ------------------------------------------------------------ Experte

function fmt(name, v) {
	if (name === 'state') return STATES[v] || v;
	if (name === 'cp_status' || name === 'cp_raw') return 'ABCDEF'[v] || v;
	if (name === 'errors' || name === 'errors_seen') return errText(v);
	if (name === 'dip_override') return '0x' + v.toString(16);
	return v;
}

function ParamRow({ p, value, onSaved }) {
	const [edit, setEdit] = useState('');
	const [msg, setMsg] = useState(null);
	const writable = p.kind !== 'status';
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
		<td><b>${p.name}</b><div class="muted msg">${p.help}${p.field ? ' (Feld ' + p.field + ')' : ''}</div></td>
		<td class="num">${value === undefined ? '–' : fmt(p.name, value)} ${p.unit}</td>
		<td class="num hide-s muted">${writable ? p.min + '…' + p.max : ''}</td>
		<td>${writable && html`<div class="row">
			<input value=${edit} placeholder=${String(p.default)} onInput=${(e) => setEdit(e.target.value)}
				onKeyDown=${(e) => e.key === 'Enter' && save()} />
			<button onClick=${save}>OK</button></div><${Msg} m=${msg} />`}</td>
	</tr>`;
}

function Params() {
	const [info, setInfo] = useState(null);
	const [vals, setVals] = useState({});
	const [err, setErr] = useState(null);
	const load = async () => {
		try { setVals(await api('/evse/params')); } catch (e) { setErr(e.message); }
	};
	useEffect(() => {
		api('/evse/param_info').then(setInfo).catch((e) => setErr(e.message));
		load();
		const t = setInterval(load, 2000);
		return () => clearInterval(t);
	}, []);
	if (err) return html`<p class="bad">${err}</p>`;
	if (!info) return html`<p class="muted">Lade …</p>`;
	const groups = [['config', 'Konfiguration (gespeichert)'], ['runtime', 'Laufzeitwerte (nicht gespeichert)'],
		['status', 'Status']];
	return groups.map(([k, t]) => html`<${Card} title=${t}><table>
		<tr><th>Name</th><th>Wert</th><th class="hide-s">Bereich</th><th>${k !== 'status' ? 'Neu' : ''}</th></tr>
		${info.filter((p) => p.kind === k).map((p) =>
			html`<${ParamRow} key=${p.name} p=${p} value=${vals[p.name]} onSaved=${load} />`)}
	</table><//>`);
}

function Errors() {
	const [d, setD] = useState(null);
	const [msg, setMsg] = useState(null);
	const load = () => api('/evse/errors').then(setD).catch((e) => setMsg({ ok: false, text: e.message }));
	useEffect(() => { load(); const t = setInterval(load, 3000); return () => clearInterval(t); }, []);
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
		<div class="row"><button onClick=${ack}>Schützfehler quittieren</button></div>
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

function System() {
	const [cfg, setCfg] = useState(null);
	const [info, setInfo] = useState([]);
	const [edit, setEdit] = useState({});
	const [msg, setMsg] = useState(null);
	const [sysinfo, setSysinfo] = useState(null);
	const load = () => api('/system/config').then(setCfg).catch((e) => setMsg({ ok: false, text: e.message }));
	useEffect(() => {
		api('/system/config_info').then(setInfo).catch(() => {});
		api('/system/info').then(setSysinfo).catch(() => {});
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
	const restart = async () => {
		if (!confirm('Steuerung neu starten? Ein laufender Ladevorgang wird beendet.')) return;
		try {
			await post('/system/restart', {});
			setMsg({ ok: true, text: 'Neustart (laufendes Laden endet geordnet) … Seite lädt in 25 s neu' });
			setTimeout(() => location.reload(), 25000);
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	return html`<${Card} title="Netz und System">
		<form onSubmit=${save}>
		${info.map((p) => p.type === 'bool'
			? html`<label class="check"><input type="checkbox" checked=${val(p.name)}
				onChange=${(e) => setEdit({ ...edit, [p.name]: e.target.checked })} />${p.name} – ${p.help}</label>`
			: html`<label>${p.name} – ${p.help}<input value=${val(p.name)}
				onInput=${(e) => setEdit({ ...edit, [p.name]: e.target.value })} /></label>`)}
		<div class="row"><button class="primary" type="submit">Speichern</button>
			<button type="button" onClick=${restart}>Neu starten</button></div>
		</form>
		<${Msg} m=${msg} />
		${cfg.restart_required && html`<p class="warn msg">Gespeicherte Werte wirken erst nach einem Neustart.</p>`}
		<p class="muted msg">${'Aktiv: ' + (cfg.active.net_dhcp ? 'DHCP' : cfg.active.net_ip) +
			', Hostname ' + (cfg.active.net_hostname || 'automatisch')}</p>
		${sysinfo && html`<p class="muted msg">${'Firmware ' + sysinfo.firmware + ', letzter Reset: ' +
			sysinfo.reset_reason + ', Laufzeit ' + Math.round(sysinfo.uptime_ms / 60000) + ' min'}</p>`}
	<//>`;
}

function Credentials({ first }) {
	const [u, setU] = useState('admin');
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
		<p class="muted msg">Ohne TLS: Daten im LAN sind mitlesbar; das Passwort selbst wird nicht übertragen.</p>
	<//>`;
}

// ------------------------------------------------------------ Rahmen

function Main() {
	const live = useLive();
	let initial = 'simple';
	try { initial = localStorage.getItem('wf-view') || 'simple'; } catch (e) { /* privat */ }
	const [view, setView] = useState(initial);
	const choose = (v) => { setView(v); try { localStorage.setItem('wf-view', v); } catch (e) { /* */ } };
	const dot = live.mode === 'live' ? 'ok' : live.mode === 'Abfrage' ? 'warn' : '';
	return html`
		<header>
			<h1>Wallfree</h1><span class="grow muted"><span class="dot ${dot}"></span> ${live.mode}</span>
			<nav>
				<button class=${view === 'simple' ? 'sel' : ''} onClick=${() => choose('simple')}>Einfach</button>
				<button class=${view === 'expert' ? 'sel' : ''} onClick=${() => choose('expert')}>Experte</button>
			</nav>
		</header>
		<main>
			${view === 'simple' ? html`<${Simple} live=${live} />`
				: html`<${Errors} /><${Params} /><${System} /><${Credentials} first=${false} />`}
		</main>`;
}

function App() {
	const [cred, setCred] = useState(null);
	const [err, setErr] = useState(null);
	useEffect(() => { api('/credentials').then(setCred).catch((e) => setErr(e.message)); }, []);
	if (err) return html`<p class="pad bad">Keine Verbindung: ${err}</p>`;
	if (!cred) return html`<p class="pad muted">Lade …</p>`;
	if (cred.must_change) {
		return html`<header><h1>Wallfree</h1></header><main><${Credentials} first=${true} /></main>`;
	}
	return html`<${Main} />`;
}

render(html`<${App} />`, document.getElementById('app'));
