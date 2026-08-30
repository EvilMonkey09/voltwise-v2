const eventId = window.VW_EVENT_ID;
const PHASE_ORDER = ['L1', 'L2', 'L3'];
let eventChart = null;
let selectedDeviceId = null;
let liveDevices = [];

document.getElementById('export-link').href = '/api/events/' + eventId + '/export';

function escapeHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function deviceName(d) {
    return d.device_label || d.remote_name || d.device_id;
}

function renderDeviceList(devices) {
    const list = document.getElementById('event-device-list');
    if (!list) return;
    liveDevices = devices;
    list.innerHTML = '';
    devices.forEach(d => {
        const tel = d.telemetry;
        const online = tel && tel.sensors;
        const total = online ? PHASE_ORDER.reduce((s, l) => s + (tel.sensors[l]?.power || 0), 0) : null;
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'vw-node-nav-item' +
            (d.device_id === selectedDeviceId ? ' selected' : '') +
            (online ? '' : ' offline');
        btn.dataset.deviceId = d.device_id;
        btn.innerHTML = `
            <div class="vw-node-nav-row">
                <span class="vw-node-nav-dot ${online ? 'online' : ''}"></span>
                <span class="vw-node-nav-name">${escapeHtml(deviceName(d))}</span>
                <span class="vw-node-nav-power">${total != null ? fmtUnit(total, 'W', 0) : '—'}</span>
            </div>
            <div class="vw-node-nav-meta">${escapeHtml(d.device_id)}</div>`;
        btn.onclick = () => selectDevice(d.device_id);
        list.appendChild(btn);
    });

    if (!selectedDeviceId && devices.length) {
        selectDevice(devices[0].device_id);
    } else if (selectedDeviceId) {
        selectDevice(selectedDeviceId);
    } else {
        document.getElementById('event-detail-empty').classList.remove('hidden');
        document.getElementById('event-detail-content').classList.add('hidden');
    }
}

function selectDevice(id) {
    selectedDeviceId = id;
    document.querySelectorAll('#event-device-list .vw-node-nav-item').forEach(el => {
        el.classList.toggle('selected', el.dataset.deviceId === id);
    });
    const d = liveDevices.find(x => x.device_id === id);
    if (!d) return;
    document.getElementById('event-detail-empty').classList.add('hidden');
    document.getElementById('event-detail-content').classList.remove('hidden');
    const tel = d.telemetry;
    const online = tel && tel.sensors;
    document.getElementById('event-device-name').textContent = deviceName(d);
    document.getElementById('event-device-meta').textContent = d.device_id;
    document.getElementById('event-device-status').innerHTML =
        `<span class="vw-pill ${online ? 'vw-pill-online' : 'vw-pill-offline'}"><span class="vw-dot"></span>${online ? (C.grid_status_on || 'on') : (C.grid_status_off || 'off')}</span>`;

    const tbody = document.getElementById('event-phase-body');
    tbody.innerHTML = '';
    let total = 0;
    if (online) {
        PHASE_ORDER.forEach(l => {
            const s = tel.sensors[l];
            if (s) total += s.power || 0;
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td class="phase-${l.toLowerCase()}">${l}</td>
                <td>${s ? fmtUnit(s.voltage, 'V', 1) : '—'}</td>
                <td>${s ? fmtUnit(s.current, 'A', 3) : '—'}</td>
                <td>${s ? fmtUnit(s.power, 'W', 1) : '—'}</td>`;
            tbody.appendChild(tr);
        });
    }
    document.getElementById('event-total-p').textContent = online ? fmtUnit(total, 'W', 1) : '—';
    document.getElementById('event-neutral').textContent =
        online && tel.neutral_current != null ? fmtUnit(tel.neutral_current, 'A', 3) : '—';
}

function updateRecordingUI(recording, isFinished) {
    document.getElementById('btn-record-event').classList.toggle('hidden', recording || isFinished);
    document.getElementById('btn-stop-event').classList.toggle('hidden', !recording);
    const status = document.getElementById('event-status');
    status.classList.toggle('hidden', !recording);
    if (recording) {
        status.className = 'vw-banner vw-banner-warn';
        status.textContent = C.events_status_recording || 'Recording';
    }
}

function renderChart(logs) {
    if (typeof Chart === 'undefined' || !logs.length) return;
    const labels = logs.map(l => fmtTime(l.timestamp * 1000));
    const data = logs.map(l => (l.l1_p||0)+(l.l2_p||0)+(l.l3_p||0));
    const ctx = document.getElementById('event-chart').getContext('2d');
    const t = VW_CHART.theme();
    if (eventChart) eventChart.destroy();
    eventChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels,
            datasets: [{
                label: C.chart_total_power || 'Total power',
                data,
                borderColor: t.accent,
                backgroundColor: t.accent + '22',
                tension: 0.3,
                fill: true,
                pointRadius: 0,
            }],
        },
        options: VW_CHART.seriesOptions('p', { showX: true }),
    });
}

async function refreshLive() {
    const r = await fetch('/api/events/' + eventId + '/live');
    if (!r.ok) return;
    const live = await r.json();
    const layout = document.getElementById('event-layout');
    if (!live.devices.length) {
        layout?.classList.add('hidden');
        return;
    }
    layout?.classList.remove('hidden');
    renderDeviceList(live.devices);
}

async function loadEvent() {
    const r = await fetch('/api/events/' + eventId);
    if (!r.ok) return;
    const ev = await r.json();
    document.getElementById('event-title').textContent = ev.name;
    document.getElementById('event-meta').textContent =
        fmtDateTime(new Date(ev.start_time * 1000)) + ' · ' +
        (ev.device_ids||[]).length + ' ' + (C.events_devices||'devices') + ' · ' +
        fmtNum(ev.log_count || 0, 0) + ' ' + (C.events_samples_suffix || 'samples');
    updateRecordingUI(ev.recording, ev.is_finished);
    renderChart(ev.logs || []);
    refreshLive();
}

document.getElementById('btn-record-event')?.addEventListener('click', async () => {
    await fetch('/api/events/' + eventId + '/recording/start', { method: 'POST' });
    loadEvent();
});

document.getElementById('btn-stop-event')?.addEventListener('click', async () => {
    await fetch('/api/events/' + eventId + '/recording/stop', { method: 'POST' });
    loadEvent();
});

document.getElementById('delete-event')?.addEventListener('click', async () => {
    if (!confirm(C.events_delete_confirm || 'Delete?')) return;
    await fetch('/api/events/' + eventId, { method: 'DELETE' });
    window.location.href = '/events';
});

loadEvent();
setInterval(() => { refreshLive(); loadEvent(); }, 3000);
document.addEventListener('vw-theme-change', () => {
    if (eventChart) { eventChart.destroy(); eventChart = null; }
    loadEvent();
});
