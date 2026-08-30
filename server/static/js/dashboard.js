const PHASE_ORDER = ['L1', 'L2', 'L3'];
const PHASE_COLORS = { L1: '#f87171', L2: '#60a5fa', L3: '#fbbf24' };
const HISTORY_MAX = 60;

let selectedId = null;
let fleetChart = null;
const detailCharts = { v: null, i: null, p: null };
const nodeHistory = new Map();
const devicesCache = new Map();
const trendLoaded = new Set();

function escapeHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function fmtSeen(ts) {
    if (ts == null) return '—';
    const sec = Math.floor(Date.now() / 1000 - ts);
    if (sec < 45) return C.seen_just_now || 'now';
    if (sec < 3600) return Math.floor(sec / 60) + ' ' + (C.seen_min_ago || 'min');
    return Math.floor(sec / 3600) + ' ' + (C.seen_h_ago || 'h');
}

function cardTitle(d) {
    return d.device_label || d.remote_name || d.device_id;
}

function chartTheme() {
    return VW_CHART.theme();
}

function trendOptions(kind) {
    return VW_CHART.trendOptions(kind, true);
}

function ensureHistory(id) {
    if (!nodeHistory.has(id)) {
        const empty = () => ({ L1: [], L2: [], L3: [] });
        nodeHistory.set(id, { t: [], v: empty(), i: empty(), p: empty() });
    }
    return nodeHistory.get(id);
}

function pushHistory(id, sensors, timestamp) {
    const h = ensureHistory(id);
    const ts = timestamp ?? Date.now() / 1000;
    h.t.push(ts);
    if (h.t.length > HISTORY_MAX) h.t.shift();
    PHASE_ORDER.forEach(l => {
        const s = sensors?.[l];
        h.v[l].push(s?.voltage ?? null);
        h.i[l].push(s?.current ?? null);
        h.p[l].push(s?.power ?? null);
        if (h.v[l].length > HISTORY_MAX) h.v[l].shift();
        if (h.i[l].length > HISTORY_MAX) h.i[l].shift();
        if (h.p[l].length > HISTORY_MAX) h.p[l].shift();
    });
}

function seedHistoryFromTrend(id, points) {
    if (!points?.length) return;
    const empty = () => ({ L1: [], L2: [], L3: [] });
    const h = { t: [], v: empty(), i: empty(), p: empty() };
    points.slice(-HISTORY_MAX).forEach(pt => {
        h.t.push(pt.timestamp ?? Date.now() / 1000);
        PHASE_ORDER.forEach(l => {
            const s = pt.sensors?.[l];
            h.v[l].push(s?.voltage ?? null);
            h.i[l].push(s?.current ?? null);
            h.p[l].push(s?.power ?? null);
        });
    });
    nodeHistory.set(id, h);
}

async function loadDeviceTrend(id) {
    if (trendLoaded.has(id)) return;
    try {
        const r = await fetch('/api/devices/' + encodeURIComponent(id) + '/trend?limit=' + HISTORY_MAX);
        if (!r.ok) return;
        seedHistoryFromTrend(id, await r.json());
        trendLoaded.add(id);
    } catch (e) { /* ignore */ }
}

async function loadFleetTrend() {
    try {
        const r = await fetch('/api/fleet/trend?limit=' + HISTORY_MAX);
        if (!r.ok) return;
        const points = await r.json();
        window._fleetHist = points.map(p => p.total_power);
        window._fleetTimestamps = points.map(p => p.timestamp ?? Date.now() / 1000);
    } catch (e) {
        if (!window._fleetHist) window._fleetHist = [];
        if (!window._fleetTimestamps) window._fleetTimestamps = [];
    }
}

function trendDatasets(hist, key) {
    return PHASE_ORDER.map(l => ({
        label: l,
        data: [...hist[key][l]],
        borderColor: PHASE_COLORS[l],
        backgroundColor: 'transparent',
        tension: 0.35,
        pointRadius: 0,
        borderWidth: 1.5,
        spanGaps: true,
    }));
}

function historyLabels(hist) {
    return VW_CHART.timeLabels(hist.t);
}

function upsertChart(chartRef, canvasId, labels, datasets, options) {
    const ctx = document.getElementById(canvasId)?.getContext('2d');
    if (!ctx) return chartRef;
    if (chartRef) {
        chartRef.data.labels = labels;
        chartRef.data.datasets = datasets;
        chartRef.update('none');
        return chartRef;
    }
    return new Chart(ctx, { type: 'line', data: { labels, datasets }, options });
}

function renderSummary(devices) {
    const el = document.getElementById('dash-summary');
    if (!el) return;
    const online = devices.filter(d => d.status === 'online').length;
    const warnings = devices.filter(d => d.imbalance?.warning).length;
    let totalP = 0;
    devices.forEach(d => { if (d.total_power) totalP += d.total_power; });
    el.innerHTML = `
        <div class="vw-dash-kpi accent"><span class="lbl">${escapeHtml(C.dashboard_stat_devices||'')}</span><span class="val">${devices.length}</span></div>
        <div class="vw-dash-kpi success"><span class="lbl">${escapeHtml(C.dashboard_stat_online||'')}</span><span class="val">${online}<span class="sub"> / ${devices.length}</span></span></div>
        <div class="vw-dash-kpi accent"><span class="lbl">${escapeHtml(C.dashboard_stat_total_power||'')}</span><span class="val">${fmtNum(totalP, 0)}<span class="unit">W</span></span></div>
        <div class="vw-dash-kpi ${warnings?'danger':'neutral'}"><span class="lbl">${escapeHtml(C.dashboard_stat_warnings||'')}</span><span class="val">${warnings}</span></div>`;
}

function renderNodeList(devices) {
    const list = document.getElementById('node-list');
    if (!list) return;

    list.innerHTML = '';
    devices.forEach(d => {
        devicesCache.set(d.device_id, d);
        const online = d.status === 'online';
        const p = d.total_power != null ? fmtUnit(d.total_power, 'W', 0) : '—';
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'vw-node-nav-item' +
            (d.device_id === selectedId ? ' selected' : '') +
            (online ? '' : ' offline');
        btn.dataset.deviceId = d.device_id;
        btn.setAttribute('role', 'option');
        btn.setAttribute('aria-selected', d.device_id === selectedId ? 'true' : 'false');
        btn.innerHTML = `
            <div class="vw-node-nav-row">
                <span class="vw-node-nav-dot ${online ? 'online' : ''}"></span>
                <span class="vw-node-nav-name">${escapeHtml(cardTitle(d))}</span>
                <span class="vw-node-nav-power">${p}</span>
            </div>
            <div class="vw-node-nav-meta">${escapeHtml(d.ip || '—')} · ${fmtSeen(d.last_seen)}</div>`;
        btn.onclick = () => selectNode(d.device_id);
        list.appendChild(btn);
    });

    if (!selectedId && devices.length) {
        const first = devices.find(d => d.status === 'online') || devices[0];
        selectNode(first.device_id);
    } else if (selectedId && !devices.some(d => d.device_id === selectedId) && devices.length) {
        selectNode(devices[0].device_id);
    } else if (!devices.length) {
        selectedId = null;
        document.getElementById('detail-empty').classList.remove('hidden');
        document.getElementById('detail-content').classList.add('hidden');
    }
}

function selectNode(id) {
    selectedId = id;
    document.querySelectorAll('#node-list .vw-node-nav-item').forEach(el => {
        const on = el.dataset.deviceId === id;
        el.classList.toggle('selected', on);
        el.setAttribute('aria-selected', on ? 'true' : 'false');
    });
    document.getElementById('detail-empty').classList.add('hidden');
    document.getElementById('detail-content').classList.remove('hidden');
    const d = devicesCache.get(id);
    if (d) updateDetailMeta(d);
    loadDeviceTrend(id).then(() => refreshDetailTelemetry());
}

function updateDetailMeta(d) {
    document.getElementById('detail-name').textContent = cardTitle(d);
    document.getElementById('detail-meta').textContent = `${d.device_id} · ${d.network_type||'—'}`;
    const online = d.status === 'online';
    document.getElementById('detail-status').innerHTML =
        `<span class="vw-pill ${online?'vw-pill-online':'vw-pill-offline'}"><span class="vw-dot"></span>${online?(C.grid_status_on||'on'):(C.grid_status_off||'off')}</span>`;
    document.getElementById('detail-link').href = '/devices/' + encodeURIComponent(d.device_id);
    const edge = document.getElementById('detail-edge');
    if (d.ip) { edge.href = 'http://' + d.ip + '/'; edge.classList.remove('hidden'); }
    else edge.classList.add('hidden');
    const exportBtn = document.getElementById('detail-export');
    if (exportBtn) {
        exportBtn.href = '/api/devices/' + encodeURIComponent(d.device_id) + '/export';
        exportBtn.classList.remove('hidden');
    }
}

function updateDetailTelemetry(data) {
    if (!data?.sensors || !selectedId) return;
    pushHistory(selectedId, data.sensors, data.timestamp);
    const hist = ensureHistory(selectedId);
    const labels = historyLabels(hist);
    let total = 0;
    const tbody = document.getElementById('detail-phase-body');
    tbody.innerHTML = '';
    PHASE_ORDER.forEach(l => {
        const s = data.sensors[l];
        if (s) total += s.power || 0;
        const tr = document.createElement('tr');
        tr.innerHTML = `<td class="phase-${l.toLowerCase()}">${l}</td>
            <td>${s ? fmtUnit(s.voltage, 'V', 1) : '—'}</td>
            <td>${s ? fmtUnit(s.current, 'A', 3) : '—'}</td>
            <td>${s ? fmtUnit(s.power, 'W', 1) : '—'}</td>`;
        tbody.appendChild(tr);
    });
    document.getElementById('detail-total-p').textContent = fmtUnit(total, 'W', 1);
    document.getElementById('detail-neutral').textContent =
        data.neutral_current != null ? fmtUnit(data.neutral_current, 'A', 3) : '—';
    const warn = document.getElementById('detail-warn');
    if (data.imbalance?.warning) {
        const tpl = C.imbalance_detail || 'ΔI = {abs} A ({pct}%)';
        warn.textContent = (C.imbalance_warning||'') + ': ' + tpl.replace('{abs}', data.imbalance.abs_diff_a).replace('{pct}', data.imbalance.pct_diff);
        warn.classList.remove('hidden');
    } else warn.classList.add('hidden');

    detailCharts.v = upsertChart(detailCharts.v, 'chart-v', labels, trendDatasets(hist, 'v'), trendOptions('v'));
    detailCharts.i = upsertChart(detailCharts.i, 'chart-i', labels, trendDatasets(hist, 'i'), trendOptions('i'));
    detailCharts.p = upsertChart(detailCharts.p, 'chart-p', labels, trendDatasets(hist, 'p'), trendOptions('p'));

    const item = document.querySelector(`#node-list .vw-node-nav-item[data-device-id="${CSS.escape(selectedId)}"]`);
    if (item) item.querySelector('.vw-node-nav-power').textContent = fmtUnit(total, 'W', 0);
}

async function refreshDetailTelemetry() {
    if (!selectedId) return;
    const d = devicesCache.get(selectedId);
    if (!d || d.status !== 'online') return;
    try {
        const r = await fetch('/api/devices/' + encodeURIComponent(selectedId) + '/telemetry');
        if (!r.ok) return;
        updateDetailTelemetry(await r.json());
    } catch (e) { /* ignore */ }
}

function updateFleetChart(devices) {
    if (typeof Chart === 'undefined') return;
    let total = 0;
    devices.forEach(d => { if (d.total_power) total += d.total_power; });
    if (!window._fleetHist) window._fleetHist = [];
    if (!window._fleetTimestamps) window._fleetTimestamps = [];
    if (window._fleetTrendLoaded) {
        window._fleetHist.push(total);
        window._fleetTimestamps.push(Date.now() / 1000);
        if (window._fleetHist.length > HISTORY_MAX) window._fleetHist.shift();
        if (window._fleetTimestamps.length > HISTORY_MAX) window._fleetTimestamps.shift();
    }
    const fleetLabels = VW_CHART.timeLabels(window._fleetTimestamps);
    const t = chartTheme();
    const ds = [{
        label: C.chart_total_power || 'Total power',
        data: window._fleetHist,
        borderColor: t.accent,
        backgroundColor: t.accent + '22',
        fill: true,
        tension: 0.35,
        pointRadius: 0,
        borderWidth: 1.5,
    }];
    fleetChart = upsertChart(fleetChart, 'fleet-chart', fleetLabels, ds, VW_CHART.seriesOptions('p', { showX: true }));
}

async function loadDevices() {
    const r = await fetch('/api/devices');
    const devices = await r.json();
    const empty = document.getElementById('no-devices');
    const layout = document.getElementById('dash-layout');
    const fleet = document.getElementById('fleet-chart-panel');
    empty.classList.toggle('hidden', devices.length > 0);
    layout.classList.toggle('hidden', devices.length === 0);
    fleet.classList.toggle('hidden', devices.length === 0);
    renderSummary(devices);
    renderNodeList(devices);
    updateFleetChart(devices);
    if (selectedId) {
        const d = devices.find(x => x.device_id === selectedId);
        if (d) updateDetailMeta(d);
    }
}

function updateClock() {
    const el = document.getElementById('dash-clock');
    if (el) el.textContent = fmtDateTime();
}

function destroyAllCharts() {
    if (fleetChart) { fleetChart.destroy(); fleetChart = null; }
    Object.keys(detailCharts).forEach(k => { if (detailCharts[k]) { detailCharts[k].destroy(); detailCharts[k] = null; } });
}

updateClock();
setInterval(updateClock, 1000);
loadFleetTrend().then(() => {
    window._fleetTrendLoaded = true;
    loadDevices();
});
setInterval(loadDevices, 5000);
setInterval(refreshDetailTelemetry, 2000);

document.addEventListener('vw-theme-change', () => {
    destroyAllCharts();
    if (selectedId) refreshDetailTelemetry();
    loadDevices();
});
