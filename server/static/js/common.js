const C = window.VW_CENTRAL || {};
let isRecording = false;
let modalResolve = null;

function $(id) { return document.getElementById(id); }

function showModal(title, hint, placeholder, initial) {
    $('modal-title').textContent = title;
    $('modal-hint').textContent = hint || '';
    const inp = $('modal-input');
    inp.placeholder = placeholder || '';
    inp.value = initial || '';
    $('modal-backdrop').classList.remove('hidden');
    $('modal').classList.remove('hidden');
    inp.focus();
    return new Promise((resolve) => { modalResolve = resolve; });
}

function hideModal(val) {
    $('modal-backdrop').classList.add('hidden');
    $('modal').classList.add('hidden');
    if (modalResolve) modalResolve(val);
    modalResolve = null;
}

if ($('modal-cancel')) $('modal-cancel').onclick = () => hideModal(null);
if ($('modal-ok')) $('modal-ok').onclick = () => hideModal($('modal-input').value.trim());
if ($('modal-backdrop')) $('modal-backdrop').onclick = () => hideModal(null);

async function toggleRecording() {
    const btn = $('btn-record');
    if (!btn) return;
    if (!isRecording) {
        const name = await showModal(
            C.modal_recording_title || 'Recording',
            C.modal_recording_hint || '',
            C.modal_placeholder || '',
            C.modal_event_default || ''
        );
        if (!name) return;
        await fetch('/api/recording/start_all', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name }),
        });
        isRecording = true;
        btn.textContent = C.nav_record_stop || 'Stop';
        btn.className = 'vw-btn vw-btn-ghost vw-btn-sm';
        $('recording-badge')?.classList.remove('hidden');
    } else {
        await fetch('/api/recording/stop_all', { method: 'POST' });
        isRecording = false;
        btn.textContent = C.nav_record_start || 'Record';
        btn.className = 'vw-btn vw-btn-danger vw-btn-sm';
        $('recording-badge')?.classList.add('hidden');
    }
}

async function checkRecordingStatus() {
    try {
        const r = await fetch('/api/recording/status');
        const d = await r.json();
        if (!d.recording) return;
        isRecording = true;
        const btn = $('btn-record');
        if (btn) {
            btn.textContent = C.nav_record_stop || 'Stop';
            btn.className = 'vw-btn vw-btn-ghost vw-btn-sm';
        }
        $('recording-badge')?.classList.remove('hidden');
    } catch (e) { /* ignore */ }
}

if ($('btn-record')) $('btn-record').onclick = toggleRecording;
checkRecordingStatus();

function getTheme() {
    return document.documentElement.getAttribute('data-theme') === 'light' ? 'light' : 'dark';
}

function setTheme(theme) {
    const t = theme === 'light' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', t);
    try { localStorage.setItem('vw-theme', t); } catch (e) { /* ignore */ }
    const btn = $('btn-theme');
    if (btn) btn.textContent = t === 'dark' ? '☀' : '◐';
    document.dispatchEvent(new CustomEvent('vw-theme-change', { detail: { theme: t } }));
}

if ($('btn-theme')) {
    $('btn-theme').textContent = getTheme() === 'dark' ? '☀' : '◐';
    $('btn-theme').onclick = () => setTheme(getTheme() === 'dark' ? 'light' : 'dark');
}

const _numFormatters = new Map();

function numberLocale() {
    return window.VW_LANG === 'de' ? 'de-DE' : 'en-US';
}

function fmtNum(value, decimals = 1) {
    if (value == null || Number.isNaN(Number(value))) return '—';
    const loc = numberLocale();
    const key = `${loc}:${decimals}`;
    if (!_numFormatters.has(key)) {
        _numFormatters.set(key, new Intl.NumberFormat(loc, {
            minimumFractionDigits: decimals,
            maximumFractionDigits: decimals,
        }));
    }
    return _numFormatters.get(key).format(Number(value));
}

function fmtUnit(value, unit, decimals = 1) {
    if (value == null || Number.isNaN(Number(value))) return '—';
    return `${fmtNum(value, decimals)} ${unit}`;
}

function fmtDateTime(date = new Date()) {
    return date.toLocaleString(numberLocale());
}

function fmtTime(date) {
    const d = date instanceof Date ? date : new Date(date);
    return d.toLocaleTimeString(numberLocale());
}

const VW_UPDATE_DISMISS_KEY = 'vw-update-dismiss';

function showUpdateBanner(info) {
    const banner = $('update-banner');
    if (!banner || !info?.update_available) return;
    const tag = info.latest_tag || info.latest_version || '';
    try {
        if (localStorage.getItem(VW_UPDATE_DISMISS_KEY) === tag) return;
    } catch (e) { /* ignore */ }
    const detail = $('update-banner-detail');
    if (detail) {
        const lead = C.upd_lead || 'Installed:';
        const latest = C.upd_latest || 'Latest:';
        detail.textContent = ` ${lead} v${info.current || '—'} · ${latest} ${tag}`;
    }
    const link = $('update-banner-link');
    if (link && info.html_url) link.href = info.html_url;
    banner.dataset.latestTag = tag;
    banner.classList.remove('hidden');
}

function hideUpdateBanner(tag) {
    const banner = $('update-banner');
    if (banner) banner.classList.add('hidden');
    if (tag) {
        try { localStorage.setItem(VW_UPDATE_DISMISS_KEY, tag); } catch (e) { /* ignore */ }
    }
}

async function checkAppUpdate() {
    try {
        const r = await fetch('/api/app/update-status');
        if (!r.ok) return null;
        const info = await r.json();
        if (info.update_available) showUpdateBanner(info);
        return info;
    } catch (e) {
        return null;
    }
}

if ($('update-banner-dismiss')) {
    $('update-banner-dismiss').onclick = () => {
        const tag = $('update-banner')?.dataset.latestTag || '';
        hideUpdateBanner(tag);
    };
}

checkAppUpdate();
