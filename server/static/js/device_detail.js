const deviceId = window.VW_DEVICE_ID;
const PHASE_ORDER = ['L1', 'L2', 'L3'];
const PHASE_COLORS = { L1: '#f87171', L2: '#60a5fa', L3: '#fbbf24' };
const LIVE_MAX = 60;

const liveHistory = { t: [], v: phaseEmpty(), i: phaseEmpty(), p: phaseEmpty() };
const detailCharts = { v: null, i: null, p: null };
let historyChart = null;
let deviceMeta = null;

function phaseEmpty() {
    return { L1: [], L2: [], L3: [] };
}

function fmtUptime(sec) {
    if (sec == null || sec < 0) return '—';
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
}

function fmtSeen(ts) {
    if (ts == null) return '—';
    const sec = Math.floor(Date.now() / 1000 - ts);
    if (sec < 45) return C.seen_just_now || 'now';
    if (sec < 3600) return Math.floor(sec / 60) + ' ' + (C.seen_min_ago || 'min');
    return Math.floor(sec / 3600) + ' ' + (C.seen_h_ago || 'h');
}

function deviceTitle(d) {
    return d?.device_label || d?.remote_name || deviceId;
}

function chartTheme() {
    return VW_CHART.theme();
}

function trendOptions(kind) {
    return VW_CHART.trendOptions(kind, true);
}

function pushLive(sensors, timestamp) {
    const ts = timestamp ?? Date.now() / 1000;
    liveHistory.t.push(ts);
    if (liveHistory.t.length > LIVE_MAX) liveHistory.t.shift();
    PHASE_ORDER.forEach(l => {
        const s = sensors?.[l];
        liveHistory.v[l].push(s?.voltage ?? null);
        liveHistory.i[l].push(s?.current ?? null);
        liveHistory.p[l].push(s?.power ?? null);
        if (liveHistory.v[l].length > LIVE_MAX) liveHistory.v[l].shift();
        if (liveHistory.i[l].length > LIVE_MAX) liveHistory.i[l].shift();
        if (liveHistory.p[l].length > LIVE_MAX) liveHistory.p[l].shift();
    });
}

function seedLiveFromTrend(points) {
    if (!points?.length) return;
    liveHistory.t = [];
    liveHistory.v = phaseEmpty();
    liveHistory.i = phaseEmpty();
    liveHistory.p = phaseEmpty();
    points.slice(-LIVE_MAX).forEach(pt => {
        liveHistory.t.push(pt.timestamp ?? Date.now() / 1000);
        PHASE_ORDER.forEach(l => {
            const s = pt.sensors?.[l];
            liveHistory.v[l].push(s?.voltage ?? null);
            liveHistory.i[l].push(s?.current ?? null);
            liveHistory.p[l].push(s?.power ?? null);
        });
    });
}

let liveTrendLoaded = false;

async function loadLiveTrend() {
    if (liveTrendLoaded) return;
    try {
        const r = await fetch('/api/devices/' + encodeURIComponent(deviceId) + '/trend?limit=' + LIVE_MAX);
        if (!r.ok) return;
        seedLiveFromTrend(await r.json());
        liveTrendLoaded = true;
    } catch (e) { /* ignore */ }
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

function renderKpis(tel) {
    const sensors = tel.sensors || {};
    let total = 0;
    let uSum = 0;
    let uCount = 0;
    let fSum = 0;
    let fCount = 0;
    PHASE_ORDER.forEach(l => {
        const s = sensors[l];
        if (!s) return;
        total += s.power || 0;
        if (s.voltage != null) { uSum += s.voltage; uCount++; }
        if (s.frequency != null) { fSum += s.frequency; fCount++; }
    });
    const el = document.getElementById('device-kpis');
    if (!el) return;
    el.innerHTML = `
        <div class="vw-dash-kpi accent">
            <span class="lbl">${escapeHtml(C.grid_total_power || 'Total power')}</span>
            <span class="val">${fmtNum(total, 1)}<span class="unit">W</span></span>
        </div>
        <div class="vw-dash-kpi neutral">
            <span class="lbl">${escapeHtml(C.grid_neutral || 'Neutral I')}</span>
            <span class="val">${tel.neutral_current != null ? fmtNum(tel.neutral_current, 3) + '<span class="unit">A</span>' : '—'}</span>
        </div>
        <div class="vw-dash-kpi neutral">
            <span class="lbl">${escapeHtml(C.device_avg_voltage || 'Avg voltage')}</span>
            <span class="val">${uCount ? fmtNum(uSum / uCount, 1) + '<span class="unit">V</span>' : '—'}</span>
        </div>
        <div class="vw-dash-kpi neutral">
            <span class="lbl">${escapeHtml(C.device_frequency || 'Frequency')}</span>
            <span class="val">${fCount ? fmtNum(fSum / fCount, 2) + '<span class="unit">Hz</span>' : '—'}</span>
        </div>`;
}

function renderPhaseTable(sensors) {
    const tbody = document.getElementById('phase-body');
    if (!tbody) return;
    tbody.innerHTML = '';
    PHASE_ORDER.forEach(l => {
        const s = sensors?.[l];
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td class="phase-${l.toLowerCase()}">${l}</td>
            <td>${s ? fmtUnit(s.voltage, 'V', 1) : '—'}</td>
            <td>${s ? fmtUnit(s.current, 'A', 3) : '—'}</td>
            <td>${s ? fmtUnit(s.power, 'W', 1) : '—'}</td>
            <td>${s ? fmtUnit(s.frequency, 'Hz', 2) : '—'}</td>
            <td>${s ? fmtNum(s.power_factor, 3) : '—'}</td>`;
        tbody.appendChild(tr);
    });
}

function renderSystemInfo(tel, status) {
    const sys = tel.system || {};
    const el = document.getElementById('device-sys');
    if (!el) return;
    const rows = [
        [C.grid_ip || 'IP', sys.ip || '—'],
        [C.grid_network || 'Network', sys.network_type || '—'],
        [C.device_uptime || 'Uptime', fmtUptime(sys.uptime_s)],
        [C.device_last_update || 'Last update', fmtSeen(tel.timestamp)],
        [C.grid_last_seen || 'Last seen', fmtSeen(deviceMeta?.last_seen)],
    ];
    el.innerHTML = rows.map(([k, v]) => `<dt>${escapeHtml(k)}</dt><dd>${escapeHtml(String(v))}</dd>`).join('');
}

function renderHeader(tel, online) {
    document.getElementById('device-title').textContent = deviceTitle(deviceMeta);
    document.getElementById('device-meta').textContent = `${deviceId} · ${tel.system?.network_type || '—'}`;
    document.getElementById('device-status').innerHTML =
        `<span class="vw-pill ${online ? 'vw-pill-online' : 'vw-pill-offline'}"><span class="vw-dot"></span>${online ? (C.grid_status_on || 'on') : (C.grid_status_off || 'off')}</span>`;
    const edge = document.getElementById('edge-link');
    if (tel.system?.ip) {
        edge.href = 'http://' + tel.system.ip + '/';
        edge.classList.remove('hidden');
    } else {
        edge.classList.add('hidden');
    }
    document.getElementById('sim-banner').classList.toggle('hidden', !tel.simulation);
    document.getElementById('offline-banner').classList.toggle('hidden', online);
    const warn = document.getElementById('imbalance-banner');
    if (tel.imbalance?.warning) {
        const tpl = C.imbalance_detail || 'ΔI = {abs} A ({pct}%)';
        warn.textContent = (C.imbalance_warning || 'Imbalance') + ': ' +
            tpl.replace('{abs}', tel.imbalance.abs_diff_a).replace('{pct}', tel.imbalance.pct_diff);
        warn.classList.remove('hidden');
    } else {
        warn.classList.add('hidden');
    }
}

function escapeHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function updateLiveCharts() {
    if (typeof Chart === 'undefined') return;
    const labels = VW_CHART.timeLabels(liveHistory.t);
    detailCharts.v = upsertChart(detailCharts.v, 'chart-v', labels, trendDatasets(liveHistory, 'v'), trendOptions('v'));
    detailCharts.i = upsertChart(detailCharts.i, 'chart-i', labels, trendDatasets(liveHistory, 'i'), trendOptions('i'));
    detailCharts.p = upsertChart(detailCharts.p, 'chart-p', labels, trendDatasets(liveHistory, 'p'), trendOptions('p'));
}

function updateHistoryChart(hist) {
    if (typeof Chart === 'undefined' || !hist.length) return;
    const t = chartTheme();
    const labels = hist.map(r => fmtTime(r.timestamp * 1000));
    const data = hist.map(r => (r.l1_p || 0) + (r.l2_p || 0) + (r.l3_p || 0));
    const ctx = document.getElementById('history-chart')?.getContext('2d');
    if (!ctx) return;
    const ds = [{
        label: C.chart_total_power || 'Total power',
        data,
        borderColor: t.accent,
        backgroundColor: t.accent + '22',
        fill: true,
        tension: 0.35,
        pointRadius: 0,
        borderWidth: 1.5,
    }];
    if (historyChart) {
        historyChart.data.labels = labels;
        historyChart.data.datasets = ds;
        historyChart.options = VW_CHART.seriesOptions('p', { showX: true });
        historyChart.update('none');
        return;
    }
    historyChart = new Chart(ctx, {
        type: 'line',
        data: { labels, datasets: ds },
        options: VW_CHART.seriesOptions('p', { showX: true }),
    });
}

async function loadDeviceMeta() {
    try {
        const r = await fetch('/api/devices');
        if (!r.ok) return;
        const devices = await r.json();
        deviceMeta = devices.find(d => d.device_id === deviceId) || { device_id: deviceId, status: 'offline' };
    } catch (e) { /* ignore */ }
}

async function refresh() {
    await loadDeviceMeta();
    await loadLiveTrend();
    const online = deviceMeta?.status === 'online';
    let tel = null;
    try {
        const telR = await fetch('/api/devices/' + encodeURIComponent(deviceId) + '/telemetry');
        if (telR.ok) tel = await telR.json();
    } catch (e) { /* ignore */ }

    if (!tel) {
        renderHeader({ system: {}, simulation: false }, false);
        document.getElementById('device-kpis').innerHTML = '';
        document.getElementById('phase-body').innerHTML = '';
        document.getElementById('device-sys').innerHTML = '';
        return;
    }

    renderHeader(tel, online);
    renderKpis(tel);
    renderPhaseTable(tel.sensors);
    renderSystemInfo(tel, online);
    if (online) pushLive(tel.sensors, tel.timestamp);
    updateLiveCharts();

    try {
        const histR = await fetch('/api/devices/' + encodeURIComponent(deviceId) + '/history?limit=200');
        if (histR.ok) updateHistoryChart(await histR.json());
    } catch (e) { /* ignore */ }
}

function destroyAllCharts() {
    if (historyChart) { historyChart.destroy(); historyChart = null; }
    Object.keys(detailCharts).forEach(k => {
        if (detailCharts[k]) { detailCharts[k].destroy(); detailCharts[k] = null; }
    });
}

refresh();
setInterval(refresh, 2000);
document.addEventListener('vw-theme-change', () => { destroyAllCharts(); refresh(); });
