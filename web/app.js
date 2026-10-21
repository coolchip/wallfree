// SPDX-License-Identifier: Apache-2.0
// Weboberflaeche Wallfree (SOLL-API-31): nutzt nur die HTTP-/WebSocket-API.
import { html, render, useState, useEffect, useRef } from './vendor.js';

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
const ERRSTATE = ['Kein Fehler', 'Fehler (Details: Insights)', 'Schalter-/Konfigurationsfehler',
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
			if (ws) return;
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
		// Ein haengender WebSocket (Kabel gezogen) meldet sich nicht von selbst
		const watch = setInterval(() => {
			if (!lastOk || Date.now() - lastOk < OFFLINE_MS) return;
			setOnline(false);
			if (ws && ws.readyState === WebSocket.OPEN) ws.drop();
		}, 1000);
		connect();
		return () => { stop = true; if (ws) ws.close(); clearInterval(timer); clearInterval(watch); clearTimeout(retry); };
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

// ------------------------------------------------------------ Uebersicht

// Ladestrom (SOLL-API-32): Vorgabe, Grenzen, erlaubter Strom (Tastgrad) und mit
// Zaehler der Messwert (hoechste Phase, nur Anzeige).
const LIMIT_TEXT = { external: 'Vorgabe', hardware: 'Maximum Drehschalter', cable: 'Kabel', input: 'Eingang' };

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
	return [t + (l.external_ignored ? ' · Vorgabe ignoriert (Eingang)' : ''), by === 'external' ? '' : 'warn'];
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

// Insights: dieselben Werte als Zahlen
function CurrentTable({ l, meter }) {
	if (!l) return null;
	const ist = measured(meter);
	const rows = [
		['external', 'Vorgabe', l.external_ignored ? 'ignoriert (Eingang)' : l.external ? amps(l.external) : 'Laden aus'],
		['hardware', 'Maximum Drehschalter', amps(l.hardware)],
		['cable', 'Kabel', l.cable ? amps(l.cable) : '–'],
		['input', 'Eingang', l.input ? amps(l.input) : '–'],
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
		${live.meter && html`<${MeterCard} m=${live.meter} />`}
		<${CurrentControl} ext=${live.ext} setExt=${live.setExt} limits=${live.limits} meter=${live.meter} />
	</div><${EventLog} live=${live} />`;
}

// Uebersicht: Leistung, Zaehlerstand, Zustand
function MeterCard({ m }) {
	const p = mval(m, 74), e = mval(m, 209);
	const st = m.state ? m.state.status : 'connecting';
	return html`<${Card} title="Zähler">
		<div class="big">${p === null ? '–' : num(p / 1000, 2) + ' kW'}</div>
		<div class="muted">${e === null ? 'Zählerstand –' : 'Zählerstand ' + num(e, 1) + ' kWh'}</div>
		<div class=${st === 'ok' ? 'muted' : 'warn'}>${METER_STATUS[st] || st}</div>
	<//>`;
}

// Insights: alle Messwerte und Fehlerzaehler (SOLL-MTR-15)
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
		<td title=${p.name}>${p.help}${p.field ? html`<div class="muted msg">Feld ${p.field}</div>` : ''}</td>
		<td class="num">${value === undefined ? '–' : fmt(p.name, value)} ${p.unit}</td>
		<td class="num hide-s muted">${writable ? p.min + '…' + p.max : ''}</td>
		<td>${writable && html`<div class="row">
			<input value=${edit} placeholder=${String(p.default)} onInput=${(e) => setEdit(e.target.value)}
				onKeyDown=${(e) => e.key === 'Enter' && save()} />
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
	useEffect(() => {
		api('/evse/param_info').then(setInfo).catch((e) => setErr(e.message));
		load();
		const t = setInterval(load, 2000);
		return () => clearInterval(t);
	}, []);
	if (err) return html`<p class="bad">${err}</p>`;
	if (!info) return html`<p class="muted">Lade …</p>`;
	const groups = [['config', 'Konfiguration (gespeichert)'], ['runtime', 'Laufzeitwerte (nicht gespeichert)'],
		['status', 'Status der Ladelogik']];
	return groups.filter(([k]) => kinds.includes(k)).map(([k, t]) => html`<${Card} title=${t}><table>
		<tr><th>Parameter</th><th>Wert</th><th class="hide-s">${k !== 'status' ? 'Bereich' : ''}</th><th>${k !== 'status' ? 'Neu' : ''}</th></tr>
		${info.filter((p) => p.kind === k).map((p) =>
			html`<${ParamRow} key=${p.name} p=${p} value=${vals[p.name]} onSaved=${load} />`)}
	</table><//>`);
}

function Errors() {
	const [d, setD] = useState(null);
	const [msg, setMsg] = useState(null);
	const load = () => api('/evse/errors').then(setD).catch(() => { /* Verbindung zeigt der Kopf */ });
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
	netzwerk: ['Netzwerk', ['net_', 'telnet_']],
	mqtt: ['MQTT', ['mqtt_']],
	zaehler: ['Zähler (nur Anzeige)', ['meter_']],
};

function SysConfig({ part, meter }) {
	const [title, prefixes] = SYS_PARTS[part];
	const [cfg, setCfg] = useState(null);
	const [info, setInfo] = useState([]);
	const [edit, setEdit] = useState({});
	const [msg, setMsg] = useState(null);
	const [mqtt, setMqtt] = useState(null);
	const load = () => api('/system/config').then(setCfg).catch((e) => setMsg({ ok: false, text: e.message }));
	useEffect(() => {
		api('/system/config_info').then(setInfo).catch(() => {});
		load();
		if (part !== 'mqtt') return undefined;
		const lm = () => api('/mqtt/state').then(setMqtt).catch(() => {});
		lm();
		const t = setInterval(lm, 3000);
		return () => clearInterval(t);
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
	let status = null;
	if (part === 'netzwerk') {
		status = 'Aktiv: ' + (cfg.active.net_dhcp ? 'DHCP' : cfg.active.net_ip) + ', Hostname ' +
			(cfg.active.net_hostname || 'automatisch');
	} else if (part === 'mqtt' && mqtt) {
		status = 'Zustand: ' + MQTT_STATES[mqtt.connection_state] + (mqtt.connection_state ? ', Präfix ' +
			mqtt.prefix + ', gesendet ' + mqtt.published + ', Befehle ' + mqtt.commands + (mqtt.rejected
			? ' (abgelehnt ' + mqtt.rejected + ': ' + mqtt.last_reject + ')' : '') : '');
	} else if (part === 'zaehler') {
		const st = meter && meter.state;
		status = 'Zustand: ' + (st ? METER_STATUS[st.status] || st.status : METER_STATUS.off);
	}
	return html`<${Card} title=${title}>
		${status && html`<p class="muted">${status}</p>`}
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
			return html`<label title=${p.name}>${p.help}<input value=${val(p.name)} inputmode=${p.type === 'code' ? 'numeric' : null}
				onInput=${(e) => set(p.type === 'code' ? +e.target.value : e.target.value)} /></label>`;
		})}
		<div class="row"><button class="primary" type="submit">Speichern</button></div>
		</form>
		<${Msg} m=${msg} />
		${cfg.restart_required && html`<div class="row msg"><span class="warn">Gespeicherte Werte wirken erst
			nach einem Neustart.</span><button onClick=${() => restart(setMsg)}>Jetzt neu starten</button></div>`}
	<//>`;
}

const dur = (ms) => {
	const m = Math.floor(ms / 60000);
	return m < 60 ? m + ' min' : m < 1440 ? Math.floor(m / 60) + ' h ' + (m % 60) + ' min'
		: Math.floor(m / 1440) + ' d ' + Math.floor(m % 1440 / 60) + ' h';
};

function SystemPage() {
	const [si, setSi] = useState(null);
	const [msg, setMsg] = useState(null);
	useEffect(() => {
		const load = () => api('/system/info').then(setSi).catch(() => {});
		load();
		const t = setInterval(load, 10000);
		return () => clearInterval(t);
	}, []);
	return html`<${Card} title="System">
		${si && html`<table>
			<tr><td>Firmware</td><td class="num">${si.firmware}</td></tr>
			<tr><td>Laufzeit</td><td class="num">${dur(si.uptime_ms)}</td></tr>
			<tr><td>Letzter Reset</td><td class="num">${si.reset_reason}</td></tr>
		</table>`}
		<div class="row msg"><button onClick=${() => restart(setMsg)}>Neu starten</button></div>
		<${Msg} m=${msg} />
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
	useEffect(() => {
		const load = () => api('/evse/history').then(setH).catch(() => {});
		load();
		const t = setInterval(load, 60000);
		return () => clearInterval(t);
	}, []);
	return h;
}

// Zeilen mit Beginn/Ende in Browserzeit; das laufende Intervall endet jetzt
function histRows(h) {
	const now = Date.now(), n = h.rows.length, end0 = now - h.age_ms;
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
	const sel = hover === null ? rows.length - 1 : hover;
	const r = rows[sel];
	const move = (e) => {
		const b = e.currentTarget.ownerSVGElement.getBoundingClientRect();
		const t = x0 + ((e.clientX - b.left) / b.width * HW - HL) / (HW - HL - HR) * span;
		const i = rows.findIndex((x) => t >= x.t0 && t < x.t1);
		setHover(i < 0 ? null : i);
	};
	return html`
		<p class="h-read">${hm(r.t0)}–${r.cur ? 'jetzt' : hm(r.t1)}:
			${r.p !== null ? html` <b>${num(r.p / 1000, 1)} kW</b> ·` : ''} erlaubt <b>${amps(r.a)}</b> · ${flagText(r.f)}</p>
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
			${hover !== null && html`<line class="h-cross" x1=${X((r.t0 + r.t1) / 2)} x2=${X((r.t0 + r.t1) / 2)} y1="10" y2=${bandY + HBAND} />`}
			<rect class="h-hit" x=${HL} y="0" width=${HW - HL - HR} height=${bandY + HBAND}
				onPointerMove=${move} onPointerDown=${move} onPointerLeave=${() => setHover(null)} />
		</svg>
		<p class="legend"><i class="sw chg"></i>lädt <i class="sw conn"></i>angesteckt <i class="sw err"></i>Fehler</p>
		<p class="muted msg">Mittelwerte je ${h.interval_ms / 60000} min seit dem Start der Steuerung (höchstens 48 h);
			nach einem Neustart leer.</p>`;
}

// ------------------------------------------------------------ Bereiche

function Insights({ live }) {
	const h = useHistory();
	return html`<${History} h=${h} maxA=${live.limits && live.limits.hardware / 1000} />
		<${CurrentTable} l=${live.limits} meter=${live.meter} /><${Errors} /><${Meter} m=${live.meter} />
		<${EventLog} live=${live} all=${true} /><${Params} kinds=${['status']} />`;
}

const SETTINGS = [['laden', 'Laden'], ['netzwerk', 'Netzwerk'], ['mqtt', 'MQTT'], ['zaehler', 'Zähler'],
	['zugang', 'Zugang']];

function Settings({ sub, live }) {
	const s = SETTINGS.some(([k]) => k === sub) ? sub : 'laden';
	return html`<nav class="sub">${SETTINGS.map(([k, t]) =>
		html`<a href=${'#/einstellungen/' + k} class=${s === k ? 'sel' : ''}>${t}</a>`)}</nav>
		${s === 'laden' ? html`<${Params} kinds=${['config', 'runtime']} />`
		: s === 'zugang' ? html`<${Credentials} first=${false} />`
		: html`<${SysConfig} key=${s} part=${s} meter=${live.meter} />`}`;
}

// ------------------------------------------------------------ Rahmen

// Bereich in der Adresse (#/einstellungen/mqtt): Zurueck-Taste und Lesezeichen
const AREAS = [['', 'Übersicht'], ['insights', 'Insights'], ['einstellungen', 'Einstellungen'], ['system', 'System']];
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

function Main() {
	const live = useLive();
	const [a, sub] = useRoute();
	const area = AREAS.some(([k]) => k === a) ? a : '';
	const dot = live.online ? 'ok' : live.online === false ? 'bad' : '';
	const conn = live.online ? 'verbunden' : live.online === false ? 'getrennt' : '…';
	return html`
		<header>
			<h1>Wallfree</h1><span class="grow muted" title=${live.mode === 'live' ? 'live (WebSocket)' : live.mode === 'poll' ? 'Abfrage alle 2 s' : ''}><span class="dot ${dot}"></span> ${conn}</span>
			<nav class="top">${AREAS.map(([k, t]) => html`<a href=${'#/' + k} class=${area === k ? 'sel' : ''}>${t}</a>`)}</nav>
		</header>
		<main class=${live.online === false ? 'offline' : ''}>
			${area === 'insights' ? html`<${Insights} live=${live} />`
				: area === 'einstellungen' ? html`<${Settings} sub=${sub} live=${live} />`
				: area === 'system' ? html`<${SystemPage} />`
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
		return html`<header><h1>Wallfree</h1></header><main><${Credentials} first=${true} /></main>`;
	}
	return html`<${Main} />`;
}

render(html`<${App} />`, document.getElementById('app'));
