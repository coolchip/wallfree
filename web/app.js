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
// Wert zur Kennung oder null (nicht vorhanden, ungueltig, veraltet)
const mval = (m, id) => {
	if (!m || !m.ids || !m.values) return null;
	const i = m.ids.indexOf(id);
	return i < 0 || m.values[i] === null || m.values[i] === undefined ? null : m.values[i];
};

// ------------------------------------------------------------ Live-Daten

// WebSocket /ws; schliesst der Server sofort (z. B. Browser sendet keine
// Anmeldung beim Aufbau), wird stattdessen alle 2 s abgefragt.
function useLive() {
	const [state, setState] = useState(null);
	const [ext, setExt] = useState(null);
	const [limits, setLimits] = useState(null); // Topic evse/limits
	const [mode, setMode] = useState('…');
	const [events, setEvents] = useState([]); // Topic evse/event (SOLL-ERR-10), juengste zuletzt
	const [meter, setMeter] = useState(null); // meters/0/*: {ids, values, state}; null = kein Zaehler

	useEffect(() => {
		let ws = null, timer = null, stop = false;
		const poll = async () => {
			try {
				setState(await api('/evse/state'));
				setExt(await api('/evse/external_current'));
				setLimits(await api('/evse/limits'));
			} catch (e) { /* naechster Versuch */ }
			try {
				const st = await api('/meters/0/state');
				const ids = await api('/meters/0/value_ids');
				setMeter({ ids, values: await api('/meters/0/values'), state: st });
			} catch (e) { if (e.status === 404) setMeter(null); }
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
					if (m.topic === 'evse/limits') setLimits(m.payload);
					if (m.topic === 'evse/event') setEvents((l) => l.concat([m.payload]).slice(-40));
					if (m.topic === 'meters/0/value_ids') setMeter((o) => ({ ...o, ids: m.payload }));
					if (m.topic === 'meters/0/values') setMeter((o) => ({ ...o, values: m.payload }));
					if (m.topic === 'meters/0/state') setMeter((o) => ({ ...o, state: m.payload }));
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
	return { state, ext, mode, setExt, events, limits, meter };
}

// ------------------------------------------------------------ Bausteine

function Card({ title, children }) {
	return html`<section class="card"><h2>${title}</h2>${children}</section>`;
}

function Msg({ m }) {
	if (!m) return null;
	return html`<div class="msg ${m.ok ? 'ok' : 'bad'}">${m.text}</div>`;
}

// ------------------------------------------------------------ Ereignisse, Zugriffsprotokoll

function evText(e) {
	switch (e.type) {
	case 'state': return 'Zustand ' + (STATES[e.from] || e.from) + ' → ' + (STATES[e.to] || e.to);
	case 'error': return [e.set.length && 'Fehler: ' + e.set.join(', '),
		e.cleared.length && 'behoben: ' + e.cleared.join(', ')].filter(Boolean).join('; ');
	case 'lock': return 'Verriegelung: ' + (LOCK[e.lock_state] || e.lock_state);
	case 'contactor': return 'Schütz ' + (e.on ? 'ein' : 'aus');
	case 'meter': return 'Zähler ' + (METER_STATUS[e.status] || e.status) +
		(e.energy_decreased ? ', Zählerstand gesunken' : '');
	}
	return e.type;
}

// Vorgeschichte aus GET /system/log (SOLL-ERR-11), danach live ueber den
// WebSocket; ohne WebSocket alle 5 s neu abfragen. Zeiten sind ms seit Start
// der Steuerung, umgerechnet ueber deren Laufzeit beim Abruf (ungefaehr).
function EventLog({ live, all }) {
	const [log, setLog] = useState(null);
	const [off, setOff] = useState(null);
	const [err, setErr] = useState(null);
	const load = async () => {
		try {
			const [l, i] = [await api('/system/log'), await api('/system/info')];
			setOff(Date.now() - i.uptime_ms);
			setLog(l.entries);
			setErr(null);
		} catch (e) { setErr(e.message); }
	};
	const polling = live.mode !== 'live';
	useEffect(() => {
		load();
		if (!polling) return undefined;
		const t = setInterval(load, 5000);
		return () => clearInterval(t);
	}, [polling]);
	if (!log) return err ? html`<${Card} title="Ereignisse"><p class="bad">${err}</p><//>` : null;
	const known = Math.max(0, ...log.filter((x) => x.kind === 'event').map((x) => x.event.seq));
	let list = log.concat(live.events.filter((e) => e.seq > known)
		.map((e) => ({ kind: 'event', t_ms: e.t_ms, event: e })));
	if (!all) list = list.filter((x) => x.kind === 'event').slice(-8);
	const time = (t) => new Date(off + t).toLocaleTimeString('de-DE');
	const text = (x) => (x.kind === 'event' ? evText(x.event)
		: 'Anmeldung fehlgeschlagen von ' + x.addr + (x.count > 1 ? ' (' + x.count + '×)' : ''));
	return html`<${Card} title=${all ? 'Zugriffsprotokoll' : 'Letzte Ereignisse'}>
		${list.length === 0 ? html`<p class="muted">Keine seit dem Start.</p>` : html`<table>
			${list.slice().reverse().map((x) => html`<tr>
				<td class="num muted">${time(x.t_ms)}</td>
				<td class=${x.kind === 'event' ? (x.event.type === 'error' && x.event.set.length ? 'bad' : '') : 'warn'}>${text(x)}</td>
			</tr>`)}
		</table>`}
		${all && html`<p class="muted msg">Letzte 32 Einträge seit dem Start der Steuerung; Uhrzeiten ungefähr.</p>`}
	<//>`;
}

// ------------------------------------------------------------ Einfach

// Sollwert der Schnittstelle, Grenzen und wirksamer Strom getrennt; die Grenze,
// die gerade wirkt, ist hervorgehoben. Mit Zaehler zusaetzlich der gemessene
// Strom (hoechste Phase), nur zur Anzeige.
const LIMIT_TEXT = { external: 'Sollwert', hardware: 'Maximum Drehschalter', cable: 'Kabel',
	input: 'Eingang', blocked: 'Sollwert 0 (Laden aus)', pwm5: '5-%-Modus' };

function Limits({ l, meter }) {
	if (!l) return null;
	const ph = [13, 17, 21].map((id) => mval(meter, id)).filter((v) => v !== null);
	const ist = ph.length ? Math.max(...ph) * 1000 : null;
	const rows = [
		['external', 'Sollwert (evcc/Schnittstelle)', l.external_ignored ? 0 : l.external,
			l.external_ignored ? 'ignoriert (Eingang)' : null],
		['hardware', 'Maximum Drehschalter', l.hardware, null],
		['cable', 'Kabel', l.cable, null],
		['input', 'Eingang', l.input, null],
	].filter((r) => r[0] === 'external' || r[0] === 'hardware' || r[2] > 0);
	const scale = Math.max(32000, ...rows.map((r) => r[2]));
	const pct = (ma) => Math.min(100, (ma / scale) * 100) + '%';
	const by = l.limited_by;
	return html`<div class="limits">
		${rows.map(([k, label, ma, note]) => html`<div class=${'lrow' + (by === k && k !== 'external' ? ' act' : '')}>
			<span>${label}</span>
			<span class="num">${note || (ma ? amps(ma) : k === 'external' ? 'aus' : '–')}
				${by === k && k !== 'external' ? html` <b class="warn">begrenzt</b>` : ''}</span>
			<div class="bar"><i style=${'width:' + pct(ma)}></i></div>
		</div>`)}
		<div class="lrow eff">
			<span>Wirksam (Fahrzeug darf, PWM)</span>
			<span class="num">${l.allowed ? amps(l.allowed) : '–'}</span>
			<div class="bar"><i style=${'width:' + pct(l.allowed)}></i></div>
		</div>
		${ist !== null && html`<div class="lrow ist">
			<span>Istwert (Zähler, höchste Phase)</span>
			<span class="num">${amps(ist)}</span>
			<div class="bar"><i style=${'width:' + pct(ist)}></i></div>
		</div>`}
		<p class="muted msg">${by === 'external' ? 'Der Sollwert wirkt.'
			: 'Es wirkt: ' + (LIMIT_TEXT[by] || by) + '.'}${!meter ? ' Ein Istwert wird nicht gemessen (kein Zähler).'
			: ist === null ? ' Kein gültiger Istwert vom Zähler.' : ''}</p>
	</div>`;
}

function CurrentControl({ ext, setExt, limits, meter }) {
	const [val, setVal] = useState(null);
	const [msg, setMsg] = useState(null);
	const cur = ext ? ext.current : 0;
	const shown = val !== null ? val : Math.min(32, Math.max(6, Math.round(cur / 1000) || 16));

	const send = async (ma) => {
		try {
			setExt(await post('/evse/external_current', { current: ma }));
			setMsg({ ok: true, text: ma ? 'Gesetzt: ' + amps(ma) : 'Laden aus' });
			setVal(null);
		} catch (e) { setMsg({ ok: false, text: e.message }); }
	};
	return html`<section class="card wide"><h2>Ladestrom</h2>
		<${Limits} l=${limits} meter=${meter} />
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
		<p class="muted msg">Setzt den Sollwert. Ist evcc verbunden, überschreibt evcc diesen Wert.</p>
	</section>`;
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
		${live.meter && html`<${MeterCard} m=${live.meter} />`}
		<${CurrentControl} ext=${live.ext} setExt=${live.setExt} limits=${live.limits} meter=${live.meter} />
	</div><${EventLog} live=${live} />`;
}

// Einfach: Leistung, Zaehlerstand, Zustand
function MeterCard({ m }) {
	const p = mval(m, 74), e = mval(m, 209);
	const st = m.state ? m.state.status : 'connecting';
	return html`<${Card} title="Zähler">
		<div class="big">${p === null ? '–' : num(p / 1000, 2) + ' kW'}</div>
		<div class="muted">${e === null ? 'Zählerstand –' : 'Zählerstand ' + num(e, 1) + ' kWh'}</div>
		<div class=${st === 'ok' ? 'muted' : 'warn'}>${METER_STATUS[st] || st}</div>
	<//>`;
}

// Experte: alle Messwerte und Fehlerzaehler (SOLL-MTR-15)
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
	return html`<${Card} title="Netz, Zähler und System">
		<form onSubmit=${save}>
		${info.map((p) => {
			const set = (v) => setEdit({ ...edit, [p.name]: v });
			const opts = p.name === 'meter_type' ? METER_TYPES : p.name === 'meter_framing' ? FRAMING : null;
			if (p.type === 'bool') {
				return html`<label class="check"><input type="checkbox" checked=${val(p.name)}
					onChange=${(e) => set(e.target.checked)} />${p.name} – ${p.help}</label>`;
			}
			if (opts) {
				return html`<label>${p.name}<select value=${val(p.name)} onChange=${(e) => set(+e.target.value)}>
					${opts.map((t, i) => html`<option value=${i}>${t}</option>`)}</select></label>`;
			}
			return html`<label>${p.name} – ${p.help}<input value=${val(p.name)} inputmode=${p.type === 'code' ? 'numeric' : null}
				onInput=${(e) => set(p.type === 'code' ? +e.target.value : e.target.value)} /></label>`;
		})}
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
				: html`<${Errors} /><${Meter} m=${live.meter} /><${EventLog} live=${live} all=${true} /><${Params} /><${System} /><${Credentials} first=${false} />`}
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
