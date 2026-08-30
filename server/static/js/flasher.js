const STORAGE_KEY = 'voltwise_svt_setup';

function loadSetup() {
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) return;
        const d = JSON.parse(raw);
        if (d.name) document.getElementById('svt-name').value = d.name;
        if (d.mqtt) document.getElementById('svt-mqtt').value = d.mqtt;
        if (d.port) document.getElementById('svt-port').value = d.port;
    } catch (e) { /* ignore */ }
}

function readSetup() {
    return {
        name: document.getElementById('svt-name').value.trim(),
        mqtt: document.getElementById('svt-mqtt').value.trim(),
        port: document.getElementById('svt-port').value.trim() || '1883',
    };
}

function saveSetup() {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(readSetup()));
}

function buildManifestUrl(profile) {
    const d = readSetup();
    const params = new URLSearchParams();
    if (d.name) params.set('name', d.name);
    if (d.mqtt) params.set('mqtt_host', d.mqtt);
    if (d.port) params.set('mqtt_port', d.port);
    const qs = params.toString();
    return '/api/flasher/manifest/' + profile + (qs ? '?' + qs : '');
}

function updateManifest() {
    const btn = document.getElementById('flash-btn');
    if (btn) btn.setAttribute('manifest', buildManifestUrl(selected));
}

['svt-name', 'svt-mqtt', 'svt-port'].forEach(id => {
    document.getElementById(id)?.addEventListener('input', () => { saveSetup(); updateManifest(); });
});

loadSetup();

let selected = 'wt32-eth01';
document.querySelectorAll('.vw-profile-card').forEach((card) => {
    card.addEventListener('click', () => {
        document.querySelectorAll('.vw-profile-card').forEach((c) => c.classList.remove('selected'));
        card.classList.add('selected');
        selected = card.dataset.profile;
        updateManifest();
    });
});

updateManifest();

const flashBtn = document.getElementById('flash-btn');
if (flashBtn) {
    flashBtn.addEventListener('click', () => {
        saveSetup();
        const d = readSetup();
        const hint = document.getElementById('flash-hint');
        if (d.name || d.mqtt) {
            hint.textContent = C.flasher_provision_ok || '';
        } else {
            hint.textContent = C.flasher_after_flash || '';
        }
        hint.classList.remove('hidden');
    });
}
