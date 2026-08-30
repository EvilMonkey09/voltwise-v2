let allDevices = [];

function openCreateModal() {
    document.getElementById('create-event-backdrop')?.classList.remove('hidden');
    document.getElementById('create-event-modal')?.classList.remove('hidden');
    document.getElementById('event-name')?.focus();
}

function closeCreateModal() {
    document.getElementById('create-event-backdrop')?.classList.add('hidden');
    document.getElementById('create-event-modal')?.classList.add('hidden');
}

async function loadDevicePicker() {
    const r = await fetch('/api/devices');
    allDevices = await r.json();
    const el = document.getElementById('device-picker');
    if (!allDevices.length) {
        el.innerHTML = `<p class="vw-muted">${C.empty_devices || ''}</p>`;
        return;
    }
    el.innerHTML = allDevices.map(d => {
        const name = d.device_label || d.remote_name || d.device_id;
        return `<label class="vw-check-item"><input type="checkbox" name="device" value="${d.device_id}" checked> ${name} <span class="vw-muted">(${d.device_id})</span></label>`;
    }).join('');
}

document.getElementById('btn-create-event')?.addEventListener('click', () => {
    loadDevicePicker();
    openCreateModal();
});

document.getElementById('create-event-cancel')?.addEventListener('click', closeCreateModal);
document.getElementById('create-event-backdrop')?.addEventListener('click', closeCreateModal);

document.getElementById('create-event-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const name = document.getElementById('event-name').value.trim();
    const device_ids = [...document.querySelectorAll('#device-picker input[name=device]:checked')].map(c => c.value);
    const r = await fetch('/api/events', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, device_ids }),
    });
    if (r.ok) {
        const ev = await r.json();
        window.location.href = '/events/' + ev.id;
    }
});

async function deleteEvent(id) {
    if (!confirm(C.events_delete_confirm || 'Delete this event?')) return;
    const r = await fetch('/api/events/' + id, { method: 'DELETE' });
    if (r.ok) loadEvents();
}

async function loadEvents() {
    const r = await fetch('/api/events');
    const events = await r.json();
    const tbody = document.querySelector('#events-table tbody');
    const empty = document.getElementById('events-empty');
    tbody.innerHTML = '';
    empty.classList.toggle('hidden', events.length > 0);
    const suffix = C.events_duration_suffix || 's';
    events.forEach((ev) => {
        const tr = document.createElement('tr');
        const start = fmtDateTime(new Date(ev.start_time * 1000));
        const dur = ev.duration != null ? fmtNum(ev.duration, 1) + ' ' + suffix : '—';
        const status = ev.recording
            ? `<span class="vw-pill vw-pill-recording">${C.events_status_recording||'REC'}</span>`
            : (ev.is_finished ? `<span class="vw-pill vw-pill-offline">${C.events_status_finished||''}</span>` : `<span class="vw-pill vw-pill-online">${C.events_status_ready||''}</span>`);
        tr.innerHTML = `
            <td>${status} ${ev.name}</td>
            <td>${start}</td>
            <td class="col-num">${dur}</td>
            <td class="col-num">${fmtNum((ev.device_ids||[]).length, 0)}</td>
            <td class="col-num">${fmtNum(ev.log_count||0, 0)}</td>
            <td class="col-actions">
                <a class="vw-btn vw-btn-ghost vw-btn-sm" href="/events/${ev.id}">${C.events_open||'Open'}</a>
                <button type="button" class="vw-btn vw-btn-danger vw-btn-sm btn-delete-event" data-id="${ev.id}">${C.events_delete||'Delete'}</button>
            </td>`;
        tr.querySelector('.btn-delete-event')?.addEventListener('click', () => deleteEvent(ev.id));
        tbody.appendChild(tr);
    });
}

loadEvents();
