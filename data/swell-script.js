/**
 * swell-script.js - Updated for NO REPEAT 1-Hour Music System + RTC Calibration
 * Versi fungsional penuh untuk homepage dan halaman detail.
 * Berkomunikasi langsung dengan ESP32 via WebSocket.
 * Mengelola daftar perangkat menggunakan localStorage browser.
 * UPDATED PLAYLIST: Nature sounds dengan durasi maksimal 1 jam tanpa repeat
 * NEW FEATURE: RTC Calibration dengan waktu browser
 */

// =================================================================
// UPDATED PLAYLIST CONFIGURATION - MATCH WITH MAIN.CPP
// =================================================================
const FIXED_RELAX_PLAYLIST = [
    { trackNumber: 1, title: "AYAT KURSI", filename: "0001_Relax_AYAT_KURSI.mp3" },
    { trackNumber: 2, title: "FAN", filename: "0002_Relax_FAN.mp3" },
    { trackNumber: 3, title: "FROG", filename: "0003_Relax_FROG.mp3" },
    { trackNumber: 4, title: "OCEAN WAVES", filename: "0004_Relax_OCEAN_WAVES.mp3" },
    { trackNumber: 6, title: "RAINDROP", filename: "0006_Relax_RAINDROP.mp3" },
    { trackNumber: 7, title: "RIVER", filename: "0007_Relax_RIVER.mp3" },
    { trackNumber: 8, title: "VACUUM CLEANER", filename: "0008_Relax_VACUM_CLEANER.mp3" }
];

const ALARM_TRACK = { trackNumber: 5, title: "ALARM SOUND", filename: "0005_Alarm_sound_alarm.mp3" };

// =================================================================
// RTC TIME TRACKING VARIABLES
// =================================================================
let rtcTimeOffset = 0; // Offset in milliseconds between RTC and browser time
let rtcTimeReceived = null; // Last received RTC time from ESP32
let rtcTimeReceivedAt = null; // When the RTC time was received

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
                            <img src="media/logo.png" alt="Device" class="device-img">
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
    console.log(`📻 Updated fixed playlist available: ${FIXED_RELAX_PLAYLIST.length} nature sounds (NO REPEAT, MAX 1 HOUR)`);

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
        sendCommand('getRTC'); // Request RTC time

        // ⭐ UPDATE: Populate dropdown dengan playlist baru
        populateSongDropdownWithFixedPlaylist();

        // Initialize RTC time display updates
        startRTCTimeUpdates();
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
        try {
            const data = JSON.parse(event.data);
            console.log(`Message from ${deviceIp}:`, data);

            if (data.type === 'statusUpdate') {
                updateUIFromState(data.state);
            } else if (data.type === 'playlist') {
                // ⭐ UPDATED: Gunakan fixed playlist, abaikan playlist dari device
                console.log('📻 Received playlist from device, but using fixed playlist instead');
                populateSongDropdownWithFixedPlaylist();
            } else if (data.type === 'rtcTime') {
                // ⭐ NEW: Handle RTC time update
                handleRTCTimeUpdate(data);
            } else if (data.type === 'rtcCalibrated') {
                // ⭐ NEW: Handle RTC calibration response
                handleRTCCalibrationResponse(data);
            }
        } catch (e) {
            console.error("Failed to parse JSON from ESP32:", e);
        }
    }

    // Pasang semua event listener untuk elemen UI
    setupEventListeners();
    setupRTCCalibration(); // Setup RTC calibration
}

// =================================================================
// NEW: RTC TIME FUNCTIONS
// =================================================================

/**
 * Handle RTC time update from ESP32
 */
function handleRTCTimeUpdate(data) {
    console.log('🕐 RTC Time received from ESP32:', data);

    // Parse RTC time from ESP32
    const rtcData = data.rtc;
    const rtcTime = new Date(
        rtcData.year + 2000, // ESP32 sends year as offset from 2000
        rtcData.month - 1,   // JS months are 0-based
        rtcData.day,
        rtcData.hour,
        rtcData.minute,
        rtcData.second
    );

    // Store RTC time and calculate offset
    rtcTimeReceived = rtcTime;
    rtcTimeReceivedAt = new Date();
    rtcTimeOffset = rtcTimeReceived.getTime() - rtcTimeReceivedAt.getTime();

    console.log(`🕐 RTC Time: ${rtcTime.toLocaleString()}`);
    console.log(`🕐 Browser Time: ${rtcTimeReceivedAt.toLocaleString()}`);
    console.log(`🕐 Offset: ${rtcTimeOffset / 1000} seconds`);

    // Update display immediately
    updateRTCTimeDisplay();
}

/**
 * Handle RTC calibration response from ESP32
 */
function handleRTCCalibrationResponse(data) {
    const calibrationMessage = document.getElementById('calibration-message');
    const calibrateButton = document.getElementById('rtc-calibrate-button');

    if (data.success) {
        calibrationMessage.textContent = '✅ RTC successfully calibrated with browser time!';
        calibrationMessage.style.color = '#4cd964';

        // Reset offset since RTC is now synced
        rtcTimeOffset = 0;
        rtcTimeReceived = new Date();
        rtcTimeReceivedAt = new Date();

        console.log('✅ RTC Calibration successful');
    } else {
        calibrationMessage.textContent = '❌ RTC calibration failed. Please try again.';
        calibrationMessage.style.color = '#ff3b30';
        console.error('❌ RTC Calibration failed:', data.error);
    }

    calibrateButton.disabled = false;

    // Clear message after 3 seconds
    setTimeout(() => {
        calibrationMessage.textContent = '';
    }, 3000);
}

/**
 * Calculate current RTC time based on offset
 */
function getCurrentRTCTime() {
    if (!rtcTimeReceived || !rtcTimeReceivedAt) {
        return new Date(); // Fallback to browser time
    }

    const now = new Date();
    const elapsedSinceReceived = now.getTime() - rtcTimeReceivedAt.getTime();
    const estimatedRTCTime = new Date(rtcTimeReceived.getTime() + elapsedSinceReceived);

    return estimatedRTCTime;
}

/**
 * Update RTC time display in UI
 */
function updateRTCTimeDisplay() {
    const rtcTime = getCurrentRTCTime();
    const timeStr = rtcTime.toTimeString().slice(0, 8);
    const rtcTimeValue = document.getElementById('rtc-time-value');

    if (rtcTimeValue) rtcTimeValue.textContent = timeStr;
}

/**
 * Update time comparison display
 */
function updateTimeComparison() {
    const browserTime = new Date();
    const rtcTime = getCurrentRTCTime();

    const browserTimeElement = document.getElementById('browser-time');
    const rtcTimeComparisonElement = document.getElementById('rtc-time-comparison');

    if (browserTimeElement) {
        browserTimeElement.textContent = browserTime.toTimeString().slice(0, 8);
    }

    if (rtcTimeComparisonElement) {
        rtcTimeComparisonElement.textContent = rtcTime.toTimeString().slice(0, 8);
    }
}

/**
 * Start RTC time display updates
 */
function startRTCTimeUpdates() {
    // Update displays every second
    setInterval(() => {
        updateRTCTimeDisplay();
        updateTimeComparison();
    }, 1000);

    // Request RTC time update every 30 seconds to maintain accuracy
    setInterval(() => {
        if (websocket && websocket.readyState === WebSocket.OPEN) {
            sendCommand('getRTC');
        }
    }, 30000);
}

/**
 * Setup RTC calibration button
 */
function setupRTCCalibration() {
    const calibrateButton = document.getElementById('rtc-calibrate-button');
    const calibrationMessage = document.getElementById('calibration-message');

    if (calibrateButton) {
        calibrateButton.addEventListener('click', function () {
            // Show calibration in progress
            calibrationMessage.textContent = '🔄 Calibrating RTC...';
            calibrationMessage.style.color = '#f7a325';
            calibrateButton.disabled = true;

            // Get current browser time
            const browserTime = new Date();
            const calibrationData = {
                year: browserTime.getFullYear(),
                month: browserTime.getMonth() + 1, // JS months are 0-based, ESP32 expects 1-based
                day: browserTime.getDate(),
                dayOfWeek: browserTime.getDay() + 1, // JS: 0=Sunday, ESP32: 1=Sunday
                hour: browserTime.getHours(),
                minute: browserTime.getMinutes(),
                second: browserTime.getSeconds()
            };

            console.log('🕐 Sending RTC calibration data:', calibrationData);
            sendCommand('rtc-calibrate', calibrationData);
        });
    }
}

/** 
 * ⭐ NEW FUNCTION: Populate dropdown dengan updated fixed playlist
 */
function populateSongDropdownWithFixedPlaylist() {
    const songSelect = document.getElementById('song-select');
    if (!songSelect) return;

    // Clear existing options
    songSelect.innerHTML = '';

    // Add updated tracks dari FIXED_RELAX_PLAYLIST
    FIXED_RELAX_PLAYLIST.forEach(track => {
        const option = document.createElement('option');
        option.value = track.trackNumber;
        option.textContent = track.title;
        songSelect.appendChild(option);
    });

    console.log(`📻 Dropdown populated with ${FIXED_RELAX_PLAYLIST.length} updated tracks`);
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

/** Menyiapkan semua event listener untuk elemen UI yang interaktif. */
function setupEventListeners() {
    // Listener untuk semua toggle
    document.querySelectorAll('.toggle').forEach(toggle => {
        toggle.addEventListener('click', (e) => {
            e.stopPropagation();
            const toggleId = e.currentTarget.id;
            const feature = toggleId.replace('-toggle', '');
            const isActive = !e.currentTarget.classList.contains('active');

            console.log(`🔄 Toggle clicked: ${toggleId}, feature: ${feature}, will be: ${isActive}`);

            // ⭐ IMPROVED: Better dependency check dengan debugging
            if (e.currentTarget.classList.contains('dependent-disabled')) {
                console.log(`❌ Toggle ${toggleId} blocked - timer not confirmed yet`);
                showTemporaryMessage('Aktifkan dan konfirmasi Timer terlebih dahulu!');
                return;
            }

            console.log(`✅ Sending command: ${feature}-toggle with value: ${isActive}`);
            sendCommand(`${feature}-toggle`, isActive);
        });
    });

    // Intensity slider with 10% steps
    document.getElementById('intensity-slider').addEventListener('input', e => {
        let value = parseInt(e.currentTarget.value);
        value = Math.round(value / 10) * 10;
        e.currentTarget.value = value;
        e.currentTarget.nextElementSibling.textContent = `${value}%`;
        sendCommand('light-intensity', value);
    });

    document.getElementById('intensity-slider').addEventListener('change', e => {
        let value = parseInt(e.currentTarget.value);
        value = Math.round(value / 10) * 10;
        if (e.currentTarget.value != value) {
            e.currentTarget.value = value;
            e.currentTarget.nextElementSibling.textContent = `${value}%`;
            sendCommand('light-intensity', value);
        }
    });

    // Light card click handler
    document.getElementById('light-card').addEventListener('click', function () {
        if (!this.classList.contains('feature-dependent')) {
            this.classList.toggle('expanded');
            document.getElementById('light-controls').classList.toggle('expanded');
        }
    });

    // Volume slider
    document.getElementById('volume-slider').addEventListener('input', e => {
        let value = parseInt(e.currentTarget.value);
        value = Math.round(value / 10) * 10;  // Round ke kelipatan 10
        e.currentTarget.value = value;
        e.currentTarget.nextElementSibling.textContent = `${value}%`;
        sendCommand('music-volume', value);
    });

    // ⭐ UPDATED: Music track selection menggunakan fixed playlist track numbers
    document.getElementById('song-select').addEventListener('change', e => {
        const track = parseInt(e.currentTarget.value);
        console.log(`🎵 Music track selected: ${track} (NO REPEAT MODE)`);
        sendCommand('music-track', track);
    });

    // Timer confirm button
    document.getElementById('timer-confirm-button').addEventListener('click', () => {
        const startTime = document.getElementById('start-time').value;
        const endTime = document.getElementById('end-time').value;
        sendCommand('timer-confirm', { start: startTime, end: endTime });
    });

    // Collapsible cards
    document.querySelectorAll('.collapsible').forEach(card => {
        card.addEventListener('click', (e) => {
            if (card.id === 'light-card') {
                return;
            }

            const toggle = card.querySelector('.toggle');
            if (toggle && toggle.classList.contains('active')) {
                card.classList.toggle('expanded');
                const controlsId = card.id.replace('-card', '-controls');
                const controlsElement = document.getElementById(controlsId);
                if (controlsElement) {
                    controlsElement.classList.toggle('expanded');
                }
            }
        });
    });
}

/** Memperbarui seluruh UI berdasarkan satu objek 'state' dari ESP32. */
function updateUIFromState(state) {
    if (!state) return;

    console.log('🔄 Updating UI from state:', state);

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
        console.log('✅ Timer confirmed - enabling dependent features');
        enableDependentFeatures(true);
    } else {
        confirmBtn.classList.remove('confirmed');
        confirmBtn.textContent = 'Konfirmasi Timer';
        confirmBtn.disabled = !state.timer.on;
        timerCard.classList.remove('timer-confirmed');
        console.log('❌ Timer not confirmed - disabling dependent features');
        enableDependentFeatures(false);
    }

    // --- Update Light ---
    const lightCard = document.getElementById('light-card');
    const lightControls = document.getElementById('light-controls');
    const intensitySlider = document.getElementById('intensity-slider');
    const intensityValue = document.querySelector('.intensity-value');

    if (intensitySlider && intensityValue && state.light) {
        let intensity = Math.round(state.light.intensity / 10) * 10;
        intensitySlider.value = intensity;
        intensityValue.textContent = `${intensity}%`;
    }

    if (state.timer.confirmed) {
        lightCard.classList.remove('feature-dependent');
        lightCard.classList.add('expanded');
        lightControls.classList.add('expanded');
    } else {
        lightCard.classList.add('feature-dependent');
        lightCard.classList.remove('expanded');
        lightControls.classList.remove('expanded');
    }

    // ⭐ UPDATED: Update Aromatherapy dengan logging
    const aromaToggle = document.getElementById('aroma-toggle');
    const aromaStatus = document.getElementById('aroma-status');
    if (aromaToggle && aromaStatus && state.aromatherapy) {
        aromaToggle.classList.toggle('active', state.aromatherapy.on);
        aromaStatus.textContent = state.aromatherapy.on ? 'ON' : 'OFF';
        console.log('🌿 Aromatherapy toggle updated:', state.aromatherapy.on);
    }

    // ⭐ UPDATED: Update Alarm dengan logging
    const alarmToggle = document.getElementById('alarm-toggle');
    const alarmStatus = document.getElementById('alarm-status');
    if (alarmToggle && alarmStatus && state.alarm) {
        alarmToggle.classList.toggle('active', state.alarm.on);
        alarmStatus.textContent = state.alarm.on ? 'ON' : 'OFF';
        console.log('⏰ Alarm toggle updated:', state.alarm.on);
    }

    // ⭐ UPDATED: Update Music dengan logging dan updated playlist
    const musicToggle = document.getElementById('music-toggle');
    const musicStatus = document.getElementById('music-status');
    const songSelect = document.getElementById('song-select');
    const volumeSlider = document.getElementById('volume-slider');

    if (musicToggle && musicStatus && state.music) {
        musicToggle.classList.toggle('active', state.music.on);
        musicStatus.textContent = state.music.on ? 'ON (1h max)' : 'OFF'; // ⭐ UPDATED: Show 1h max indicator
        console.log('🎵 Music toggle updated:', state.music.on, '(NO REPEAT MODE)');

        // ⭐ UPDATED: Set dropdown value dari updated playlist
        if (songSelect && state.music.track) {
            // Pastikan track yang dipilih ada di updated fixed playlist
            const trackExists = FIXED_RELAX_PLAYLIST.some(track => track.trackNumber === state.music.track);
            if (trackExists) {
                songSelect.value = state.music.track;
                console.log(`🎵 Music track set to: ${state.music.track} (NO REPEAT)`);
            } else {
                console.warn(`⚠️ Invalid track from device: ${state.music.track}, using first track`);
                songSelect.value = FIXED_RELAX_PLAYLIST[0].trackNumber;
            }
        }

        if (volumeSlider) {
            volumeSlider.value = state.music.volume;
            const volumeValue = volumeSlider.nextElementSibling;
            if (volumeValue) volumeValue.textContent = `${state.music.volume}%`;
        }
    }
}

/** Mengunci atau membuka fitur-fitur yang bergantung pada Timer. */
function enableDependentFeatures(isEnabled) {
    console.log(`🔓 ${isEnabled ? 'Enabling' : 'Disabling'} dependent features`);

    document.querySelectorAll('.feature-dependent').forEach(card => {
        if (isEnabled) {
            card.classList.remove('feature-dependent');
            console.log(`  ✅ Removed feature-dependent from ${card.id}`);

            if (card.id === 'light-card') {
                card.classList.add('expanded');
                document.getElementById('light-controls').classList.add('expanded');
            }

            const toggle = card.querySelector('.toggle');
            if (toggle) {
                toggle.classList.remove('dependent-disabled');
                console.log(`  ✅ Removed dependent-disabled from ${toggle.id}`);
            }
        } else {
            card.classList.add('feature-dependent');
            console.log(`  ❌ Added feature-dependent to ${card.id}`);

            if (card.id === 'light-card') {
                card.classList.remove('expanded');
                document.getElementById('light-controls').classList.remove('expanded');
            }

            const toggle = card.querySelector('.toggle');
            if (toggle) {
                toggle.classList.add('dependent-disabled');
                console.log(`  ❌ Added dependent-disabled to ${toggle.id}`);
            }
        }
    });

    // ⭐ ADDITIONAL: Manually ensure specific toggles are enabled when timer confirmed
    if (isEnabled) {
        const criticalToggles = ['aroma-toggle', 'alarm-toggle', 'music-toggle'];
        criticalToggles.forEach(toggleId => {
            const toggle = document.getElementById(toggleId);
            if (toggle) {
                toggle.classList.remove('dependent-disabled');
                console.log(`  🔧 Force-enabled ${toggleId}`);
            }
        });
    }
}

function updateConnectionStatus(isConnected) {
    const statusEl = document.querySelector('.device-status');
    const imgEl = document.querySelector('.device-img');
    if (!statusEl || !imgEl) return;

    statusEl.textContent = isConnected ? 'Connected' : 'Disconnected. Reconnecting...';
    imgEl.src = 'media/logo.png';
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