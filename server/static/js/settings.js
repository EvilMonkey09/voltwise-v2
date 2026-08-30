document.getElementById('settings-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const form = e.target;
    const data = Object.fromEntries(new FormData(form));
    const r = await fetch('/api/settings', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data),
    });
    const msg = document.getElementById('settings-msg');
    if (r.ok) msg.textContent = C.settings_saved || 'Settings saved.';
    else msg.textContent = C.settings_error || 'Error saving settings.';
});

document.getElementById('btn-check-update')?.addEventListener('click', async () => {
    const msg = document.getElementById('update-status-msg');
    if (!msg) return;
    msg.textContent = C.settings_checking || 'Checking…';
    const info = await checkAppUpdate();
    if (!info) {
        msg.textContent = C.settings_update_error || 'Could not check for updates.';
        return;
    }
    if (info.update_available) {
        const latest = C.upd_latest || 'Latest:';
        msg.textContent = `${latest} ${info.latest_tag || info.latest_version}`;
    } else if (info.ok === false) {
        msg.textContent = info.error || (C.settings_update_error || 'Could not check for updates.');
    } else {
        msg.textContent = C.settings_up_to_date || 'Up to date.';
    }
});
