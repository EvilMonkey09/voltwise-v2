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

function showFlasherWarning(html, kind = 'warn') {
    const box = document.getElementById('flasher-warnings');
    if (!box) return;
    const el = document.createElement('div');
    el.className = 'vw-banner vw-banner-' + kind;
    el.innerHTML = html;
    box.appendChild(el);
}

function checkFlasherEnvironment() {
    const host = window.location.hostname;
    const isLocal = host === 'localhost' || host === '127.0.0.1' || host === '[::1]';
    if (!isLocal) {
        const url = `http://localhost:${window.location.port || '25555'}/flasher`;
        showFlasherWarning(
            `${C.flasher_secure_context_warn || 'Use localhost:'} <a href="${url}"><strong>${url}</strong></a>`
        );
    }
    if (!('serial' in navigator)) {
        showFlasherWarning(C.flasher_no_serial_api || 'Web Serial not supported.', 'warn');
    }
}

async function testUsbConnection() {
    const msg = document.getElementById('usb-test-msg');
    if (!msg) return;
    if (!('serial' in navigator)) {
        msg.textContent = C.flasher_no_serial_api || 'Web Serial not supported.';
        return;
    }
    msg.textContent = '…';
    try {
        const port = await navigator.serial.requestPort();
        const info = port.getInfo ? port.getInfo() : {};
        const bits = [];
        if (info.usbVendorId != null) {
            bits.push(`VID ${info.usbVendorId.toString(16)}`);
        }
        if (info.usbProductId != null) {
            bits.push(`PID ${info.usbProductId.toString(16)}`);
        }
        msg.textContent = (C.flasher_test_usb_ok || 'Port OK') +
            (bits.length ? ` (${bits.join(', ')})` : '');
    } catch (e) {
        if (e.name === 'NotFoundError') {
            msg.textContent = C.flasher_test_usb_none || 'No port selected.';
        } else {
            msg.textContent = (C.flasher_test_usb_err || 'Failed:') + ' ' + e.message;
        }
    }
}

['svt-name', 'svt-mqtt', 'svt-port'].forEach(id => {
    document.getElementById(id)?.addEventListener('input', () => { saveSetup(); updateManifest(); });
});

loadSetup();
checkFlasherEnvironment();

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

document.getElementById('btn-test-usb')?.addEventListener('click', testUsbConnection);

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
