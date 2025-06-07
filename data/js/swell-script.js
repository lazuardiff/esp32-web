/**
 * swell-script.js
 * Versi fungsional penuh untuk homepage dan halaman detail.
 * Berkomunikasi langsung dengan ESP32 via WebSocket.
 * Mengelola daftar perangkat menggunakan localStorage browser.
 */

// =================================================================
// LOGIKA UTAMA & INISIALISASI
// =================================================================

document.addEventListener('DOMContentLoaded', () => {
    createStars(); // Efek bintang di latar belakang

    // Cek halaman mana yang aktif dan jalankan inisialisasi yang sesuai.
    if (document.getElementById('home-page')) {
        initializeHomePage();
    } else if (document.getElementById('detail-page')) {
        initializeDeviceDetailPage();
    }
});


// =================================================================
// FUNGSI UNTUK HALAMAN UTAMA (swell-homepage.html)
// =================================================================

function initializeHomePage() {
    console.log("Initializing Home Page...");
    const addDeviceBtn = document.getElementById('add-device-btn');
    const connectionModal = document.getElementById('connection-modal');
    const closeModalBtn = document.querySelector('.close-modal');
    const deviceListContainer = document.getElementById('device-list');

    // Ganti form scan dengan form input IP
    const modalBody = document.querySelector('.modal-body');
    modalBody.innerHTML = `
        <p>Masukkan detail untuk perangkat SWELL Anda.</p>
        <div class="control-group">
            <label for="deviceNameInput" class="control-label">Nama Perangkat:</label>
            <input type="text" id="deviceNameInput" placeholder="Contoh: Lampu Kamar" class="song-dropdown">
        </div>
        <div class="control-group">
            <label for="deviceIpInput" class="control-label">Alamat IP Perangkat:</label>
            <input type="text" id="deviceIpInput" placeholder="Contoh: 192.168.1.15" class="song-dropdown">
        </div>
        <div class="confirm-button-wrapper">
            <button id="save-device-btn" class="confirm-button">Simpan Perangkat</button>
        </div>
    `;

    const saveDeviceBtn = document.getElementById('save-device-btn');

    // --- Event Listeners ---
    addDeviceBtn.addEventListener('click', () => connectionModal.classList.add('active'));
    closeModalBtn.addEventListener('click', () => connectionModal.classList.remove('active'));
    saveDeviceBtn.addEventListener('click', saveNewDevice);

    // --- Fungsi ---
    function getSavedDevices() {
        return JSON.parse(localStorage.getItem('swell_devices') || '[]');
    }

    function saveDevices(devices) {
        localStorage.setItem('swell_devices', JSON.stringify(devices));
    }

    function saveNewDevice() {
        const name = document.getElementById('deviceNameInput').value.trim();
        const ip = document.getElementById('deviceIpInput').value.trim();

        if (!name || !ip) {
            alert('Nama dan Alamat IP tidak boleh kosong.');
            return;
        }

        let devices = getSavedDevices();
        // Cek jika IP sudah ada, untuk menghindari duplikat
        if (devices.find(d => d.ip === ip)) {
            alert('Perangkat dengan Alamat IP ini sudah ada.');
            return;
        }

        devices.push({ name, ip });
        saveDevices(devices);
        refreshDeviceList();
        connectionModal.classList.remove('active'); // Tutup modal setelah disimpan
    }

    function removeDevice(ip) {
        if (confirm('Apakah Anda yakin ingin menghapus perangkat ini?')) {
            let devices = getSavedDevices();
            devices = devices.filter(d => d.ip !== ip);
            saveDevices(devices);
            refreshDeviceList();
        }
    }

    function refreshDeviceList() {
        const devices = getSavedDevices();
        deviceListContainer.innerHTML = ''; // Kosongkan daftar

        if (devices.length === 0) {
            deviceListContainer.innerHTML = `<div class="no-devices">Belum ada perangkat. Klik "Add Device" untuk memulai.</div>`;
            return;
        }

        devices.forEach(device => {
            const deviceElement = document.createElement('div');
            deviceElement.className = 'device-container';
            // Arahkan ke halaman detail dengan membawa parameter IP
            deviceElement.innerHTML = `
                <a href="swell-device-detail.html?ip=${encodeURIComponent(device.ip)}&name=${encodeURIComponent(device.name)}" class="device-link">
                    <div class="device-card" data-ip="${device.ip}">
                        <div class="device-icon">
                            <img src="media/icons/ActiveDevice.png" alt="Device" class="device-img">
                        </div>
                        <div class="device-info">
                            <div class="device-name">${device.name}</div>
                            <div class="device-status">${device.ip}</div>
                        </div>
                    </div>
                </a>
                <button class="remove-device-btn" data-ip="${device.ip}">✕</button>
            `;
            deviceListContainer.appendChild(deviceElement);

            // Tambahkan event listener untuk tombol hapus
            deviceElement.querySelector('.remove-device-btn').addEventListener('click', (e) => {
                removeDevice(e.currentTarget.dataset.ip);
            });
        });
    }

    // Muat daftar perangkat saat halaman pertama kali dibuka
    refreshDeviceList();
}


// =================================================================
// FUNGSI UNTUK HALAMAN DETAIL (swell-device-detail.html)
// (Kode dari jawaban sebelumnya, sudah kompatibel)
// =================================================================

let websocket;

function initializeDeviceDetailPage() {
    const urlParams = new URLSearchParams(window.location.search);
    const deviceIp = urlParams.get('ip');
    const deviceName = urlParams.get('name') || 'Swell Smart Lamp';

    document.querySelector('.device-name').textContent = deviceName;

    if (!deviceIp) {
        document.querySelector('.device-status').textContent = 'Alamat IP tidak ditemukan!';
        alert('Tidak ada alamat IP perangkat yang dipilih. Silakan kembali ke halaman utama dan pilih perangkat.');
        return;
    }

    console.log(`Initializing Detail Page for device at ${deviceIp}`);
    const gateway = `ws://${deviceIp}/ws`;

    // --- Inisialisasi WebSocket ---
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;

    function onOpen(event) {
        console.log(`Connection to ${deviceIp} opened.`);
        updateConnectionStatus(true, deviceName);
        sendCommand('getStatus');
        sendCommand('getPlaylist');
    }

    function onClose(event) {
        console.log(`Connection to ${deviceIp} closed. Retrying...`);
        updateConnectionStatus(false, deviceName);
        setTimeout(() => {
            // Coba sambung lagi
            websocket = new WebSocket(gateway);
            websocket.onopen = onOpen;
            websocket.onclose = onClose;
            websocket.onmessage = onMessage;
        }, 3000);
    }

    function onMessage(event) {
        // ... (fungsi onMessage, updateUIFromState, dll. sama seperti jawaban sebelumnya)
        // ... (Kode ini sudah saya sertakan di jawaban sebelumnya, jadi tidak saya copy-paste ulang di sini untuk keringkasan)
        // ... (Pastikan semua fungsi helper dari jawaban sebelumnya ada di sini)
        try {
            const data = JSON.parse(event.data);
            console.log(`Message from ${deviceIp}:`, data);

            if (data.type === 'statusUpdate') {
                updateUIFromState(data.state);
            } else if (data.type === 'playlist') {
                populateSongDropdown(data.playlist);
            }
        } catch (e) {
            console.error("Failed to parse JSON from ESP32:", e);
        }
    }

    // Pasang semua event listener untuk elemen UI
    setupEventListeners();
}

/** Mengirim perintah dalam format JSON ke ESP32. */
function sendCommand(command, value) {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        const message = JSON.stringify({ command, value });
        console.log('Sending to ESP32:', message);
        websocket.send(message);
    } else {
        console.log('WebSocket not open. Command not sent.');
    }
}

// ... SALIN SEMUA FUNGSI HELPER DARI JAWABAN SEBELUMNYA KE SINI ...
/** Menyiapkan semua event listener untuk elemen UI yang interaktif. */
function setupEventListeners() {
    // Listener untuk semua toggle
    document.querySelectorAll('.toggle').forEach(toggle => {
        toggle.addEventListener('click', (e) => {
            e.stopPropagation(); // Mencegah event 'click' pada kartu induk
            const feature = e.currentTarget.id.replace('-toggle', '');
            const isActive = !e.currentTarget.classList.contains('active'); // Status baru setelah di-klik

            // Logika khusus untuk timer dependency
            if (e.currentTarget.classList.contains('dependent-disabled')) {
                showTemporaryMessage('Aktifkan dan konfirmasi Timer terlebih dahulu!');
                return;
            }

            sendCommand(`${feature}-toggle`, isActive);
        });
    });

    // Listener untuk slider intensitas lampu
    document.getElementById('intensity-slider').addEventListener('input', e => {
        const value = parseInt(e.currentTarget.value);
        e.currentTarget.nextElementSibling.textContent = `${value}%`; // Update UI langsung untuk feedback
        sendCommand('light-intensity', value);
    });

    // Listener untuk slider volume musik
    document.getElementById('volume-slider').addEventListener('input', e => {
        const value = parseInt(e.currentTarget.value);
        e.currentTarget.nextElementSibling.textContent = `${value}%`; // Update UI langsung
        sendCommand('music-volume', value);
    });

    // Listener untuk dropdown lagu
    document.getElementById('song-select').addEventListener('change', e => {
        const track = parseInt(e.currentTarget.value);
        sendCommand('music-track', track);
    });

    // Listener untuk tombol konfirmasi Timer
    document.getElementById('timer-confirm-button').addEventListener('click', () => {
        const startTime = document.getElementById('start-time').value;
        const endTime = document.getElementById('end-time').value;
        sendCommand('timer-confirm', { start: startTime, end: endTime });
    });

    // Listener untuk kartu yang bisa di-expand (collapsible)
    document.querySelectorAll('.collapsible').forEach(card => {
        card.addEventListener('click', (e) => {
            // Hanya expand jika toggle-nya aktif
            const toggle = card.querySelector('.toggle');
            if (toggle && toggle.classList.contains('active')) {
                card.classList.toggle('expanded');
                const controlsId = card.id.replace('-card', '-controls');
                document.getElementById(controlsId).classList.toggle('expanded');
            }
        });
    });
}


/** Memperbarui seluruh UI berdasarkan satu objek 'state' dari ESP32. */
function updateUIFromState(state) {
    if (!state) return;

    // --- Update Timer ---
    const timerCard = document.getElementById('timer-card');
    const timerToggle = document.getElementById('timer-toggle');
    const timerStatus = document.getElementById('timer-status');
    const timerControls = document.getElementById('timer-controls');
    const confirmBtn = document.getElementById('timer-confirm-button');
    const startTimeInput = document.getElementById('start-time');
    const endTimeInput = document.getElementById('end-time');

    timerToggle.classList.toggle('active', state.timer.on);
    timerStatus.textContent = state.timer.on ? 'ON' : 'OFF';
    timerControls.classList.toggle('expanded', state.timer.on);
    timerCard.classList.toggle('expanded', state.timer.on);
    startTimeInput.value = state.timer.start;
    endTimeInput.value = state.timer.end;

    // Update tombol konfirmasi
    if (state.timer.confirmed) {
        confirmBtn.classList.add('confirmed');
        confirmBtn.textContent = 'Timer Terkonfirmasi';
        confirmBtn.disabled = true;
        timerCard.classList.add('timer-confirmed');
        enableDependentFeatures(true);
    } else {
        confirmBtn.classList.remove('confirmed');
        confirmBtn.textContent = 'Konfirmasi Timer';
        confirmBtn.disabled = !state.timer.on;
        timerCard.classList.remove('timer-confirmed');
        enableDependentFeatures(false);
    }

    // --- Update Lampu ---
    const lightToggle = document.getElementById('light-toggle');
    const lightStatus = document.getElementById('light-status');
    const intensitySlider = document.getElementById('intensity-slider');
    lightToggle.classList.toggle('active', state.light.on);
    lightStatus.textContent = state.light.on ? 'ON' : 'OFF';
    intensitySlider.value = state.light.intensity;
    intensitySlider.nextElementSibling.textContent = `${state.light.intensity}%`;

    // --- Update Musik ---
    const musicToggle = document.getElementById('music-toggle');
    const musicStatus = document.getElementById('music-status');
    const songSelect = document.getElementById('song-select');
    const volumeSlider = document.getElementById('volume-slider');
    musicToggle.classList.toggle('active', state.music.on);
    musicStatus.textContent = state.music.on ? 'ON' : 'OFF';
    songSelect.value = state.music.track;
    volumeSlider.value = state.music.volume;
    volumeSlider.nextElementSibling.textContent = `${state.music.volume}%`;
}


/** Mengunci atau membuka fitur-fitur yang bergantung pada Timer. */
function enableDependentFeatures(isEnabled) {
    document.querySelectorAll('.feature-dependent').forEach(card => {
        const toggle = card.querySelector('.toggle');
        if (isEnabled) {
            card.classList.remove('feature-dependent');
            toggle.classList.remove('dependent-disabled');
        } else {
            card.classList.add('feature-dependent');
            toggle.classList.add('dependent-disabled');
        }
    });
}


function populateSongDropdown(playlist) {
    const songSelect = document.getElementById('song-select');
    songSelect.innerHTML = ''; // Kosongkan daftar lama
    playlist.forEach(song => {
        const option = document.createElement('option');
        option.value = song.trackNumber;
        option.textContent = song.title;
        songSelect.appendChild(option);
    });
}


function updateConnectionStatus(isConnected) {
    const statusEl = document.querySelector('.device-status');
    const imgEl = document.querySelector('.device-img');
    if (!statusEl || !imgEl) return;

    statusEl.textContent = isConnected ? 'Connected' : 'Disconnected. Reconnecting...';
    imgEl.src = isConnected ? 'media/ActiveDevice.png' : 'media/NotActiveDevice.png';
}


/** Menampilkan pesan sementara di bagian bawah layar. */
function showTemporaryMessage(message) {
    const existingMessage = document.querySelector('.temp-message');
    if (existingMessage) existingMessage.remove();

    const messageDiv = document.createElement('div');
    messageDiv.className = 'temp-message';
    messageDiv.style.cssText = 'position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background-color: rgba(247, 163, 37, 0.9); color: white; padding: 10px 20px; border-radius: 8px; z-index: 10000; transition: opacity 0.5s;';
    messageDiv.textContent = message;
    document.body.appendChild(messageDiv);

    setTimeout(() => {
        messageDiv.style.opacity = '0';
        setTimeout(() => messageDiv.remove(), 500);
    }, 3000);
}


/** Membuat efek bintang di latar belakang. */
function createStars() {
    const starsContainer = document.getElementById('stars');
    if (!starsContainer) return;
    const starCount = 30;
    for (let i = 0; i < starCount; i++) {
        const star = document.createElement('div');
        star.classList.add('star');
        const size = Math.random() * 3 + 1;
        star.style.width = `${size}px`;
        star.style.height = `${size}px`;
        star.style.top = `${Math.random() * 100}%`;
        star.style.left = `${Math.random() * 100}%`;
        star.style.opacity = Math.random() * 0.7 + 0.3;
        starsContainer.appendChild(star);
    }
}