/**
 * @file main.cpp
 * @brief Swell Smart Lamp - Complete Smart Lamp IoT System
 * @version 2.0
 * @date 2025
 * @author Swell Team
 *
 * =================================================================
 * PROJECT OVERVIEW
 * =================================================================
 *
 * Swell Smart Lamp adalah sistem smart lamp IoT yang terintegrasi dengan:
 * - ESP32 sebagai microcontroller utama
 * - RTC DS3231 untuk manajemen waktu real-time
 * - DFPlayer Mini untuk audio/music playback
 * - LED strips (putih & kuning) untuk pencahayaan adaptif
 * - Aromatherapy diffuser untuk relaksasi
 * - WebSocket-based web interface untuk kontrol
 *
 * =================================================================
 * SYSTEM FEATURES
 * =================================================================
 *
 * 1. TIMER SYSTEM
 *    - Schedulable start/end time
 *    - Automatic phase switching (sleep → wake)
 *    - Confirmation-based activation
 *
 * 2. ADAPTIVE LIGHTING
 *    - Yellow light: Sleep phase (adjustable intensity 0-100%)
 *    - White light: Wake phase (full brightness)
 *    - Automatic switching based on timer
 *
 * 3. RELAX MUSIC SYSTEM
 *    - Fixed playlist: 7 nature sounds (NO REPEAT MODE)
 *    - Maximum duration: 1 hour per session
 *    - Auto-stop when timer window expires or 1-hour limit reached
 *    - Volume control (0-100% mapped to DFPlayer 0-30)
 *
 * 4. AROMATHERAPY SYSTEM
 *    - Active only in first hour of sleep phase
 *    - Spray pattern: 5 seconds ON, 5 minutes OFF
 *    - Auto-disable after 1 hour
 *
 * 5. ALARM SYSTEM
 *    - Fixed alarm sound at timer end time
 *    - 5-minute duration with full volume
 *    - Automatic fallback to relax music if enabled
 *
 * 6. WEB INTERFACE
 *    - Real-time WebSocket communication
 *    - Device discovery and connection
 *    - Live status updates and control
 *
 * =================================================================
 * HARDWARE CONFIGURATION
 * =================================================================
 *
 * ESP32 Connections:
 * - GPIO 12: White LED strip (PWM)
 * - GPIO 14: Yellow LED strip (PWM)
 * - GPIO 4:  Aromatherapy relay
 * - GPIO 16: DFPlayer RX (ESP32 TX2)
 * - GPIO 17: DFPlayer TX (ESP32 RX2)
 * - I2C:     RTC DS3231 (SDA/SCL)
 *
 * =================================================================
 * MUSIC SYSTEM DETAILS
 * =================================================================
 *
 * Fixed Playlist Structure:
 * Track 1: AYAT KURSI (0001_Relax_AYAT_KURSI.mp3)
 * Track 2: FAN (0002_Relax_FAN.mp3)
 * Track 3: FROG (0003_Relax_FROG.mp3)
 * Track 4: OCEAN WAVES (0004_Relax_OCEAN_WAVES.mp3)
 * Track 5: ALARM SOUND (0005_Alarm_sound_alarm.mp3) [Alarm only]
 * Track 6: RAINDROP (0006_Relax_RAINDROP.mp3)
 * Track 7: RIVER (0007_Relax_RIVER.mp3)
 * Track 8: VACUUM CLEANER (0008_Relax_VACUM_CLEANER.mp3)
 *
 * Music Logic:
 * - NO enableLoop() = Tracks play once without repeat
 * - 1-hour maximum duration regardless of track length
 * - Auto-disable when timer window expires
 * - Real-time duration monitoring and logging
 *
 * =================================================================
 * TIMING LOGIC
 * =================================================================
 *
 * Timer Phases:
 * 1. SLEEP PHASE (Start Time → End Time)
 *    - Yellow light active (adjustable intensity)
 *    - Relax music available (first hour only)
 *    - Aromatherapy active (first hour only)
 *
 * 2. WAKE PHASE (End Time → Start Time)
 *    - White light active (full brightness)
 *    - All other features disabled
 *    - Alarm triggers at End Time
 *
 * Grace Periods:
 * - 5-second grace period for music transitions
 * - Midnight wrap-around support for timer calculations
 *
 * =================================================================
 */

// =================================================================
// PUSTAKA DAN DEPENDENCIES
// =================================================================

// Pustaka Inti ESP32
#include <WiFi.h>              // WiFi connectivity
#include <ESPAsyncWebServer.h> // Async web server & WebSocket
#include <ArduinoJson.h>       // JSON parsing untuk WebSocket messages
#include <Preferences.h>       // Non-volatile storage untuk settings
#include <SPIFFS.h>            // File system untuk web assets

// Pustaka Hardware Peripheral
#include <DFRobotDFPlayerMini.h> // MP3 player control
#include <uRTCLib.h>             // Real-time clock DS3231
#include <Wire.h>                // I2C communication untuk RTC

// =================================================================
// KONFIGURASI SISTEM
// =================================================================

// WiFi credentials
const char *ssid = "hosssposs";
const char *password = "semogalancarTA";

// Hardware pin assignments
const int WHITE_LED_PIN = 12;   // White LED strip untuk wake phase
const int YELLOW_LED_PIN = 14;  // Yellow LED strip untuk sleep phase
const int AROMATHERAPY_PIN = 4; // Relay control untuk aromatherapy
const int DFPLAYER_RX_PIN = 16; // DFPlayer RX (connect ke ESP32 TX2)
const int DFPLAYER_TX_PIN = 17; // DFPlayer TX (connect ke ESP32 RX2)

// PWM configuration untuk LED control
const int PWM_CHANNEL_WHITE = 0;  // PWM channel untuk white LED
const int PWM_CHANNEL_YELLOW = 1; // PWM channel untuk yellow LED
const int PWM_FREQUENCY = 100;    // PWM frequency (Hz)
const int PWM_RESOLUTION = 8;     // PWM resolution (8-bit = 0-255)

// =================================================================
// FIXED PLAYLIST CONFIGURATION
// =================================================================

/**
 * @struct MusicTrack
 * @brief Structure untuk menyimpan informasi track musik
 */
struct MusicTrack
{
    int trackNumber; // Nomor track di DFPlayer (1-8)
    String title;    // Display title untuk frontend
    String filename; // Actual filename di SD card
};

/**
 * @brief Fixed playlist untuk relax music
 * @note Track 5 reserved untuk alarm, tracks 1,2,3,4,6,7,8 untuk relax music
 */
const MusicTrack RELAX_PLAYLIST[] = {
    {1, "AYAT KURSI", "0001_Relax_AYAT_KURSI.mp3"},
    {2, "FAN", "0002_Relax_FAN.mp3"},
    {3, "FROG", "0003_Relax_FROG.mp3"},
    {4, "OCEAN WAVES", "0004_Relax_OCEAN_WAVES.mp3"},
    {6, "RAINDROP", "0006_Relax_RAINDROP.mp3"},
    {7, "RIVER", "0007_Relax_RIVER.mp3"},
    {8, "VACUM CLEANER", "0008_Relax_VACUM_CLEANER.mp3"},
};

const int RELAX_PLAYLIST_SIZE = sizeof(RELAX_PLAYLIST) / sizeof(RELAX_PLAYLIST[0]);
const int ALARM_TRACK_NUMBER = 5; // Fixed track untuk alarm sound

// =================================================================
// GLOBAL OBJECTS & VARIABLES
// =================================================================

// Network & server objects
AsyncWebServer server(80); // HTTP server di port 80
AsyncWebSocket ws("/ws");  // WebSocket endpoint "/ws"
Preferences preferences;   // Non-volatile storage
uRTCLib rtc(0x68);         // RTC DS3231 dengan I2C address 0x68

// DFPlayer objects
HardwareSerial myDFPlayerSerial(2); // Hardware serial 2 untuk DFPlayer
DFRobotDFPlayerMini myDFPlayer;     // DFPlayer control object
bool dfPlayerInitialized = false;   // Status inisialisasi DFPlayer

// =================================================================
// TIMING & STATE MANAGEMENT VARIABLES
// =================================================================

// Aromatherapy timing variables
unsigned long lastAromatherapyCheck = 0;      // Last aromatherapy check time
unsigned long aromatherapyOnTime = 0;         // Start time saat aromatherapy ON
bool isAromatherapySpraying = false;          // Status sedang spray atau tidak
unsigned long lastAromatherapySprayStart = 0; // Waktu spray terakhir dimulai
bool aromatherapyScheduleActive = false;      // Status schedule aromatherapy aktif

// System timing variables
unsigned long lastTimeCheck = 0;                       // Last schedule check time
unsigned long lastStatusBroadcast = 0;                 // Last status broadcast time
const unsigned long STATUS_BROADCAST_INTERVAL = 60000; // Broadcast setiap 60 detik

// Alarm timing variables
unsigned long alarmStopTime = 0; // Waktu alarm harus berhenti
bool isAlarmPlaying = false;     // Status alarm sedang berbunyi

// Music timing variables
bool isMusicPaused = false;                       // Status musik pause
bool previousMusicState = false;                  // Previous state untuk change detection
unsigned long musicStartTime = 0;                 // Start time untuk grace period
const unsigned long MUSIC_GRACE_PERIOD = 5000;    // Grace period 5 detik
unsigned long musicPlayStartTime = 0;             // Start time untuk 1-hour limit tracking
const unsigned long MUSIC_MAX_DURATION = 3600000; // Maximum 1 jam = 3600000 ms

// =================================================================
// SETTINGS STRUCTURE
// =================================================================

/**
 * @struct Settings
 * @brief Structure untuk menyimpan semua pengaturan sistem
 * @note Disimpan di non-volatile storage menggunakan Preferences
 */
struct Settings
{
    // Timer configuration
    struct Timer
    {
        bool on = false;        // Timer toggle ON/OFF
        bool confirmed = false; // Timer sudah dikonfirmasi atau belum
        int startHour = 21;     // Jam mulai sleep phase (default 21:00)
        int startMinute = 0;    // Menit mulai sleep phase
        int endHour = 4;        // Jam selesai sleep phase (default 04:00)
        int endMinute = 0;      // Menit selesai sleep phase
    } timer;

    // Light configuration
    struct Light
    {
        int intensity = 50; // Yellow light intensity 0-100% (default 50%)
    } light;

    // Aromatherapy configuration
    struct Aromatherapy
    {
        bool on = false; // Aromatherapy toggle ON/OFF
    } aromatherapy;

    // Alarm configuration
    struct Alarm
    {
        bool on = false;                      // Alarm toggle ON/OFF
        const int track = ALARM_TRACK_NUMBER; // Fixed ke track 5
    } alarm;

    // Music configuration
    struct Music
    {
        bool on = false; // Music toggle ON/OFF
        int track = 1;   // Selected track (default track 1)
        int volume = 15; // Volume level 0-30 untuk DFPlayer (default 15)
    } music;
};

Settings currentSettings; // Global settings instance

// =================================================================
// COMPILE-TIME FUNCTIONS UNTUK RTC SETUP
// =================================================================

/**
 * @brief Konversi compile timestamp hari ke nomor hari dalam minggu
 * @param ts Compile timestamp string
 * @return int Nomor hari (1=Sunday, 2=Monday, etc.)
 */
int getCompileDayOfWeek(const char *ts)
{
    if (ts[0] == 'S' && ts[1] == 'u')
        return 1; // Sunday
    if (ts[0] == 'M' && ts[1] == 'o')
        return 2; // Monday
    if (ts[0] == 'T' && ts[1] == 'u')
        return 3; // Tuesday
    if (ts[0] == 'W' && ts[1] == 'e')
        return 4; // Wednesday
    if (ts[0] == 'T' && ts[1] == 'h')
        return 5; // Thursday
    if (ts[0] == 'F' && ts[1] == 'r')
        return 6; // Friday
    if (ts[0] == 'S' && ts[1] == 'a')
        return 7; // Saturday
    return 0;
}

/**
 * @brief Konversi compile date bulan ke nomor bulan
 * @param date Compile date string
 * @return int Nomor bulan (1-12)
 */
int getCompileMonth(const char *date)
{
    if (date[0] == 'J' && date[1] == 'a' && date[2] == 'n')
        return 1; // Jan
    if (date[0] == 'F' && date[1] == 'e' && date[2] == 'b')
        return 2; // Feb
    if (date[0] == 'M' && date[1] == 'a' && date[2] == 'r')
        return 3; // Mar
    if (date[0] == 'A' && date[1] == 'p' && date[2] == 'r')
        return 4; // Apr
    if (date[0] == 'M' && date[1] == 'a' && date[2] == 'y')
        return 5; // May
    if (date[0] == 'J' && date[1] == 'u' && date[2] == 'n')
        return 6; // Jun
    if (date[0] == 'J' && date[1] == 'u' && date[2] == 'l')
        return 7; // Jul
    if (date[0] == 'A' && date[1] == 'u' && date[2] == 'g')
        return 8; // Aug
    if (date[0] == 'S' && date[1] == 'e' && date[2] == 'p')
        return 9; // Sep
    if (date[0] == 'O' && date[1] == 'c' && date[2] == 't')
        return 10; // Oct
    if (date[0] == 'N' && date[1] == 'o' && date[2] == 'v')
        return 11; // Nov
    if (date[0] == 'D' && date[1] == 'e' && date[2] == 'c')
        return 12; // Dec
    return 0;
}

// Macro untuk extract compile time information
#define CONV_STR2DEC_2(str, i) ((str[i] - '0') * 10 + (str[i + 1] - '0'))
#define __TIME_SECONDS__ CONV_STR2DEC_2(__TIME__, 6)
#define __TIME_MINUTES__ CONV_STR2DEC_2(__TIME__, 3)
#define __TIME_HOURS__ CONV_STR2DEC_2(__TIME__, 0)
#define __TIME_DOW__ getCompileDayOfWeek(__TIMESTAMP__)
#define __TIME_DAYS__ CONV_STR2DEC_2(__DATE__, 4)
#define __TIME_MONTH__ getCompileMonth(__DATE__)
#define __TIME_YEARS__ (2000 + CONV_STR2DEC_2(__DATE__, 9))

// =================================================================
// FUNCTION DECLARATIONS
// =================================================================

// Settings management
void saveSettings(); // Simpan settings ke non-volatile storage
void loadSettings(); // Load settings dari non-volatile storage

// Hardware initialization
void checkAndSetRTC();     // Check dan set RTC time
bool initializeDFPlayer(); // Initialize DFPlayer Mini

// Music system
void generateAndSendPlaylist();                   // Kirim fixed playlist ke frontend
int getValidMusicTrackNumber(int requestedTrack); // Validate track number
void playMusicTrack(int trackNumber);             // Play musik track (NO REPEAT MODE)
void stopMusic();                                 // Stop musik dan reset timers
void setMusicVolume(int volume);                  // Set volume musik (0-30)

// Scheduling system
void checkAndApplySchedules();                                              // Main scheduler function
bool isInMusicTimeWindow(int currentTimeInMinutes, int startTimeInMinutes); // Check music time window

// Aromatherapy system
void checkAromatherapyLogic(int currentTimeInMinutes, int startTimeInMinutes); // Aromatherapy scheduler
void resetAromatherapy();                                                      // Reset aromatherapy state

// WebSocket communication
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);                                                           // Handle incoming WebSocket messages
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len); // WebSocket event handler
void notifyClients();                                                                                                        // Broadcast status ke semua connected clients

// =================================================================
// SETTINGS MANAGEMENT FUNCTIONS
// =================================================================

/**
 * @brief Simpan current settings ke non-volatile storage
 * @note Menggunakan ESP32 Preferences library untuk persistensi
 */
void saveSettings()
{
    preferences.begin("swell-app", false); // Open namespace dengan write access
    preferences.putBytes("settings", &currentSettings, sizeof(currentSettings));
    preferences.end();
    Serial.println("📁 Pengaturan disimpan ke flash memory.");
}

/**
 * @brief Load settings dari non-volatile storage
 * @note Jika tidak ada settings tersimpan, gunakan default values
 */
void loadSettings()
{
    preferences.begin("swell-app", true); // Open namespace dengan read-only access

    if (preferences.isKey("settings"))
    {
        // Load existing settings
        preferences.getBytes("settings", &currentSettings, sizeof(currentSettings));
        Serial.println("📁 Pengaturan dimuat dari flash memory.");

        // Validate dan fix settings jika perlu
        currentSettings.music.track = getValidMusicTrackNumber(currentSettings.music.track);
        if (currentSettings.music.volume > 30)
            currentSettings.music.volume = 15;
        if (currentSettings.music.volume < 0)
            currentSettings.music.volume = 5;
    }
    else
    {
        // Tidak ada settings tersimpan, gunakan default values dari struct
        Serial.println("📁 Tidak ada pengaturan tersimpan, menggunakan default values.");
    }

    preferences.end();
}

// =================================================================
// HARDWARE INITIALIZATION FUNCTIONS
// =================================================================

/**
 * @brief Check dan set RTC time berdasarkan compile time
 * @note Otomatis set RTC ke compile time jika RTC kehilangan power atau tidak akurat
 */
void checkAndSetRTC()
{
    rtc.refresh(); // Baca data terbaru dari RTC
    bool needsSetting = false;

    // Check apakah RTC kehilangan power
    if (rtc.lostPower())
    {
        Serial.println("⚠️ INFO: RTC kehilangan daya. Waktu akan diatur ulang ke compile time.");
        rtc.lostPowerClear(); // Clear lost power flag
        needsSetting = true;
    }

    // Compare dengan compile time untuk validasi akurasi
    uint8_t compileYear = (uint8_t)(__TIME_YEARS__ - 2000);
    if (rtc.day() != __TIME_DAYS__ || rtc.month() != __TIME_MONTH__ || rtc.year() != compileYear)
    {
        Serial.println("⚠️ INFO: Tanggal RTC tidak akurat. Waktu akan dikalibrasi ke compile time.");
        needsSetting = true;
    }

    // Set RTC jika diperlukan
    if (needsSetting)
    {
        rtc.set(__TIME_SECONDS__, __TIME_MINUTES__, __TIME_HOURS__, __TIME_DOW__,
                __TIME_DAYS__, __TIME_MONTH__, compileYear);
        Serial.println("✅ OK: Waktu RTC berhasil disetel ke compile time.");
    }
    else
    {
        Serial.println("✅ OK: Waktu RTC sudah akurat.");
    }
}

/**
 * @brief Initialize DFPlayer Mini dengan retry mechanism
 * @return bool True jika berhasil, false jika gagal
 * @note Mencoba 3 kali sebelum menyerah
 */
bool initializeDFPlayer()
{
    Serial.println("=== DFPlayer Mini Initialization ===");

    // Setup hardware serial untuk DFPlayer
    myDFPlayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
    Serial.println("🔊 Menginisialisasi DFPlayer Mini... Mohon tunggu!");

    // Retry mechanism (maksimal 3 attempts)
    int retryCount = 0;
    while (retryCount < 3)
    {
        if (myDFPlayer.begin(myDFPlayerSerial))
        {
            // Berhasil initialize
            Serial.println("✅ DFPlayer Mini berhasil diinisialisasi!");

            // Setup DFPlayer configuration
            myDFPlayer.setTimeOut(500);                      // Set timeout 500ms
            myDFPlayer.volume(currentSettings.music.volume); // Set volume dari settings
            myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);               // Set equalizer normal

            // Log configuration info
            Serial.printf("📻 DFPlayer Settings:\n");
            Serial.printf("   - Timeout: 500ms\n");
            Serial.printf("   - Volume: %d/30\n", currentSettings.music.volume);
            Serial.printf("   - EQ: Normal\n");
            Serial.printf("   - Fixed playlist: %d relax tracks + 1 alarm track\n", RELAX_PLAYLIST_SIZE);
            Serial.printf("   - Mode: NO REPEAT, MAX 1 HOUR\n");

            dfPlayerInitialized = true;
            return true;
        }

        // Gagal, coba lagi
        retryCount++;
        Serial.printf("❌ Percobaan %d gagal, coba lagi...\n", retryCount);
        delay(1000);
    }

    // Gagal setelah 3 attempts
    Serial.println("❌ KRITIS: DFPlayer Mini gagal diinisialisasi setelah 3 percobaan!");
    dfPlayerInitialized = false;
    return false;
}

// =================================================================
// MUSIC SYSTEM FUNCTIONS (NO REPEAT MODE)
// =================================================================

/**
 * @brief Play musik track dengan NO REPEAT MODE dan 1-hour limit
 * @param trackNumber Nomor track yang akan dimainkan (1-8)
 * @note Tidak menggunakan enableLoop(), music akan play sekali saja
 * @note Auto-stop setelah 1 jam meskipun file lebih panjang
 */
void playMusicTrack(int trackNumber)
{
    if (!dfPlayerInitialized)
    {
        Serial.println("❌ DFPlayer tidak tersedia untuk memutar musik");
        return;
    }

    Serial.printf("🎵 Playing track %d (NO REPEAT - Max 1 hour)\n", trackNumber);

    // ⭐ CRITICAL: NO enableLoop() = musik tidak akan repeat otomatis
    // myDFPlayer.enableLoop(); // TIDAK DIGUNAKAN untuk mencegah repeat

    myDFPlayer.play(trackNumber); // Play track sekali saja

    // Start timing untuk grace period dan 1-hour duration limit
    musicStartTime = millis();     // Untuk grace period 5 detik
    musicPlayStartTime = millis(); // Untuk tracking 1-hour limit

    Serial.printf("🎵 Music started at: %lu ms\n", musicStartTime);
    Serial.printf("🎵 Will auto-stop after 1 hour (3600 seconds)\n");
}

/**
 * @brief Stop musik dan reset semua music-related timers
 * @note Mereset musicStartTime dan musicPlayStartTime ke 0
 */
void stopMusic()
{
    if (!dfPlayerInitialized)
        return;

    Serial.println("🎵 Music stopped");
    myDFPlayer.stop();      // Stop DFPlayer
    isMusicPaused = false;  // Reset pause state
    musicStartTime = 0;     // Reset grace period timer
    musicPlayStartTime = 0; // Reset 1-hour duration timer
}

/**
 * @brief Set volume musik dengan bounds checking
 * @param volume Volume level (0-30 untuk DFPlayer)
 * @note Volume di-constrain ke range 0-30 sesuai spec DFPlayer
 */
void setMusicVolume(int volume)
{
    if (!dfPlayerInitialized)
        return;

    volume = constrain(volume, 0, 30); // Ensure volume dalam range valid
    Serial.printf("🎵 Volume set to: %d/30\n", volume);
    myDFPlayer.volume(volume);
}

// =================================================================
// PLAYLIST MANAGEMENT FUNCTIONS
// =================================================================

/**
 * @brief Generate dan kirim fixed playlist ke frontend via WebSocket
 * @note Playlist sudah fixed di kode, tidak dibaca dari SD card
 */
void generateAndSendPlaylist()
{
    Serial.printf("📻 Mengirim fixed playlist dengan %d lagu relax music.\n", RELAX_PLAYLIST_SIZE);

    // Create JSON response
    JsonDocument playlistDoc;
    playlistDoc["type"] = "playlist";
    JsonArray playlistArray = playlistDoc["playlist"].to<JsonArray>();

    // Add semua tracks dari RELAX_PLAYLIST ke JSON
    for (int i = 0; i < RELAX_PLAYLIST_SIZE; i++)
    {
        JsonObject trackObject = playlistArray.add<JsonObject>();
        trackObject["trackNumber"] = RELAX_PLAYLIST[i].trackNumber;
        trackObject["title"] = RELAX_PLAYLIST[i].title;
        trackObject["filename"] = RELAX_PLAYLIST[i].filename;
    }

    // Send JSON ke semua connected WebSocket clients
    String response;
    serializeJson(playlistDoc, response);
    ws.textAll(response);
    Serial.println("✅ Fixed playlist berhasil dikirim ke frontend.");
}

/**
 * @brief Validate track number dan return valid track
 * @param requestedTrack Track number yang diminta frontend
 * @return int Valid track number dari RELAX_PLAYLIST
 * @note Jika track tidak valid, return track pertama sebagai fallback
 */
int getValidMusicTrackNumber(int requestedTrack)
{
    // Check apakah requested track ada di RELAX_PLAYLIST
    for (int i = 0; i < RELAX_PLAYLIST_SIZE; i++)
    {
        if (RELAX_PLAYLIST[i].trackNumber == requestedTrack)
        {
            return requestedTrack; // Valid track
        }
    }

    // Track tidak valid, gunakan track pertama sebagai fallback
    Serial.printf("⚠️ Track %d tidak valid, menggunakan track %d\n",
                  requestedTrack, RELAX_PLAYLIST[0].trackNumber);
    return RELAX_PLAYLIST[0].trackNumber;
}

// =================================================================
// TIMING & SCHEDULING FUNCTIONS
// =================================================================

/**
 * @brief Check apakah current time masuk dalam music time window
 * @param currentTimeInMinutes Current time dalam menit (0-1439)
 * @param startTimeInMinutes Timer start time dalam menit (0-1439)
 * @return bool True jika dalam music window (1 jam pertama timer)
 * @note Music aktif hanya dalam 1 jam pertama dari timer start time
 * @note Handle midnight wrap-around untuk timer yang melewati tengah malam
 */
bool isInMusicTimeWindow(int currentTimeInMinutes, int startTimeInMinutes)
{
    // Music window = 1 jam pertama dari timer start time
    int musicEndTimeInMinutes = (startTimeInMinutes + 60) % 1440;

    Serial.printf("🎵 Music Time Check:\n");
    Serial.printf("   Current: %02d:%02d (%d min)\n",
                  currentTimeInMinutes / 60, currentTimeInMinutes % 60, currentTimeInMinutes);
    Serial.printf("   Start: %02d:%02d (%d min)\n",
                  startTimeInMinutes / 60, startTimeInMinutes % 60, startTimeInMinutes);
    Serial.printf("   End: %02d:%02d (%d min)\n",
                  musicEndTimeInMinutes / 60, musicEndTimeInMinutes % 60, musicEndTimeInMinutes);

    bool inMusicTime;

    // Handle midnight wrap-around
    if (startTimeInMinutes + 60 >= 1440)
    {
        // Music window melewati midnight (contoh: 23:30-00:30)
        inMusicTime = (currentTimeInMinutes >= startTimeInMinutes) || (currentTimeInMinutes < musicEndTimeInMinutes);
        Serial.printf("   Mode: Midnight wrap-around\n");
    }
    else
    {
        // Normal case (contoh: 21:00-22:00)
        inMusicTime = (currentTimeInMinutes >= startTimeInMinutes) && (currentTimeInMinutes < musicEndTimeInMinutes);
        Serial.printf("   Mode: Normal range\n");
    }

    Serial.printf("   Result: %s\n", inMusicTime ? "IN MUSIC TIME" : "OUTSIDE MUSIC TIME");
    return inMusicTime;
}

/**
 * @brief Main scheduler function - mengatur semua fitur berdasarkan timer
 * @note Dipanggil setiap detik dari main loop
 * @note Mengatur: lighting, aromatherapy, music, alarm
 */
void checkAndApplySchedules()
{
    // Jika timer belum dikonfirmasi, matikan semua fitur
    if (!currentSettings.timer.confirmed)
    {
        ledcWrite(PWM_CHANNEL_WHITE, 0);  // Matikan white LED
        ledcWrite(PWM_CHANNEL_YELLOW, 0); // Matikan yellow LED

        // Matikan aromatherapy jika masih aktif
        if (digitalRead(AROMATHERAPY_PIN) == HIGH)
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            aromatherapyScheduleActive = false;
        }
        return;
    }

    // Get current time dan convert ke minutes since midnight
    rtc.refresh();
    int currentTimeInMinutes = rtc.hour() * 60 + rtc.minute();
    int startTimeInMinutes = currentSettings.timer.startHour * 60 + currentSettings.timer.startMinute;
    int endTimeInMinutes = currentSettings.timer.endHour * 60 + currentSettings.timer.endMinute;

    // Determine apakah sedang dalam timer active phase (sleep phase)
    bool isTimerActivePhase;
    if (startTimeInMinutes > endTimeInMinutes)
    {
        // Timer melewati midnight (contoh: 22:00 - 06:00)
        isTimerActivePhase = (currentTimeInMinutes >= startTimeInMinutes) || (currentTimeInMinutes < endTimeInMinutes);
    }
    else
    {
        // Timer dalam hari yang sama (contoh: 09:00 - 17:00)
        isTimerActivePhase = (currentTimeInMinutes >= startTimeInMinutes) && (currentTimeInMinutes < endTimeInMinutes);
    }

    // =================================================================
    // ADAPTIVE LIGHTING CONTROL
    // =================================================================
    if (isTimerActivePhase)
    {
        // SLEEP PHASE: Yellow light dengan intensity yang bisa diatur
        int yellowBrightness = map(currentSettings.light.intensity, 0, 100, 0, 10);
        ledcWrite(PWM_CHANNEL_YELLOW, yellowBrightness);
        ledcWrite(PWM_CHANNEL_WHITE, 0); // White light OFF
    }
    else
    {
        // WAKE PHASE: White light full brightness
        ledcWrite(PWM_CHANNEL_WHITE, 10); // White light ON (full)
        ledcWrite(PWM_CHANNEL_YELLOW, 0); // Yellow light OFF
    }

    // =================================================================
    // AROMATHERAPY SCHEDULING
    // =================================================================
    bool aromaStateBefore = currentSettings.aromatherapy.on;
    checkAromatherapyLogic(currentTimeInMinutes, startTimeInMinutes);

    // Notify frontend jika aromatherapy auto-disabled
    if (aromaStateBefore && !currentSettings.aromatherapy.on)
    {
        Serial.println("🔔 NOTIFICATION: Aromatherapy auto-disabled, notifying frontend...");
        saveSettings();
        notifyClients();
    }

    // =================================================================
    // MUSIC SCHEDULING (NO REPEAT, 1-HOUR MAX)
    // =================================================================
    bool inMusicTime = isInMusicTimeWindow(currentTimeInMinutes, startTimeInMinutes);
    unsigned long currentMillis = millis();
    bool hasGracePeriod = (musicStartTime > 0) && (currentMillis - musicStartTime < MUSIC_GRACE_PERIOD);
    bool musicTimeExpired = (musicPlayStartTime > 0) && (currentMillis - musicPlayStartTime >= MUSIC_MAX_DURATION);

    // Auto-stop music jika: waktu timer habis ATAU sudah bermain 1 jam
    if (currentSettings.music.on && (!inMusicTime || musicTimeExpired) && !isAlarmPlaying && !hasGracePeriod)
    {
        stopMusic();

        // Log reason untuk stop
        if (musicTimeExpired)
        {
            Serial.println("🎵 Relax Music: 1 jam selesai - musik dihentikan otomatis");
        }
        else
        {
            Serial.println("🎵 Relax Music: Waktu timer habis");
        }

        // Auto-disable toggle dan notify frontend
        currentSettings.music.on = false;
        Serial.printf("🎵 Music auto-disabled - Toggle now: OFF\n");
        saveSettings();
        notifyClients();
    }
    else if (currentSettings.music.on && hasGracePeriod)
    {
        // Dalam grace period, log remaining time
        Serial.printf("🎵 Music in grace period (%.1fs remaining)\n",
                      (MUSIC_GRACE_PERIOD - (currentMillis - musicStartTime)) / 1000.0);
    }

    // =================================================================
    // MUSIC DURATION MONITORING (setiap 5 menit)
    // =================================================================
    if (currentSettings.music.on && musicPlayStartTime > 0)
    {
        unsigned long playedDuration = currentMillis - musicPlayStartTime;
        unsigned long remainingTime = MUSIC_MAX_DURATION - playedDuration;

        // Log progress setiap 5 menit
        if ((playedDuration / 300000) != ((playedDuration - 1000) / 300000)) // Setiap 5 menit
        {
            Serial.printf("🎵 Music Duration: %.1f/60 minutes played, %.1f minutes remaining\n",
                          playedDuration / 60000.0, remainingTime / 60000.0);
        }
    }

    // =================================================================
    // ALARM SCHEDULING
    // =================================================================

    // Trigger alarm di end time jika alarm enabled dan tidak ada musik
    if (currentSettings.alarm.on && !isAlarmPlaying && !currentSettings.music.on)
    {
        if (rtc.hour() == currentSettings.timer.endHour && rtc.minute() == currentSettings.timer.endMinute)
        {
            Serial.printf("🔔 ALARM: Waktunya bangun! Memutar track %d\n", ALARM_TRACK_NUMBER);

            if (dfPlayerInitialized)
            {
                myDFPlayer.volume(30);              // Full volume untuk alarm
                playMusicTrack(ALARM_TRACK_NUMBER); // Play alarm sound
                isAlarmPlaying = true;
                alarmStopTime = millis() + 300000; // 5 menit = 300000 ms
            }
            else
            {
                Serial.println("❌ Alarm: DFPlayer tidak tersedia");
            }
        }
    }

    // Stop alarm setelah 5 menit
    if (isAlarmPlaying && millis() >= alarmStopTime)
    {
        Serial.println("🔔 ALARM: Durasi 5 menit selesai.");
        stopMusic();
        isAlarmPlaying = false;

        // Resume relax music jika masih enabled
        if (currentSettings.music.on && dfPlayerInitialized)
        {
            int validTrack = getValidMusicTrackNumber(currentSettings.music.track);
            setMusicVolume(currentSettings.music.volume); // Restore music volume
            playMusicTrack(validTrack);
        }
    }
}

// =================================================================
// AROMATHERAPY SYSTEM FUNCTIONS
// =================================================================

/**
 * @brief Aromatherapy scheduling logic dengan spray pattern
 * @param currentTimeInMinutes Current time dalam menit
 * @param startTimeInMinutes Timer start time dalam menit
 * @note Aktif hanya dalam 1 jam pertama timer
 * @note Spray pattern: 5 detik ON, 5 menit OFF, repeat
 * @note Auto-disable setelah 1 jam
 */
void checkAromatherapyLogic(int currentTimeInMinutes, int startTimeInMinutes)
{
    // Jika aromatherapy toggle OFF, pastikan hardware juga OFF
    if (!currentSettings.aromatherapy.on)
    {
        if (digitalRead(AROMATHERAPY_PIN) == HIGH)
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            aromatherapyScheduleActive = false;
            Serial.println("💨 Aromatherapy: Dimatikan (toggle OFF)");
        }
        return;
    }

    // Calculate berapa menit sudah berlalu sejak timer start
    int timerElapsedMinutes;
    if (currentTimeInMinutes >= startTimeInMinutes)
    {
        timerElapsedMinutes = currentTimeInMinutes - startTimeInMinutes;
    }
    else
    {
        // Handle midnight wrap-around
        timerElapsedMinutes = (1440 - startTimeInMinutes) + currentTimeInMinutes;
    }

    bool inFirstHour = (timerElapsedMinutes < 60);

    // Auto-disable setelah 1 jam
    if (!inFirstHour)
    {
        // Matikan hardware jika masih ON
        if (digitalRead(AROMATHERAPY_PIN) == HIGH)
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            Serial.println("💨 Aromatherapy: Jam pertama selesai, dimatikan");
        }
        aromatherapyScheduleActive = false;

        // Auto-disable toggle
        if (currentSettings.aromatherapy.on)
        {
            currentSettings.aromatherapy.on = false;
            Serial.println("💨 Aromatherapy: Auto-disabled setelah 1 jam");
        }
        return;
    }

    // Dalam first hour, activate scheduling
    aromatherapyScheduleActive = true;
    unsigned long currentMillis = millis();

    // Handle spray duration (5 detik ON)
    if (isAromatherapySpraying)
    {
        if (currentMillis - aromatherapyOnTime >= 5000) // 5 detik spray
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            lastAromatherapySprayStart = currentMillis;
            Serial.println("💨 Aromatherapy: Semprotan selesai (5 detik)");
        }
        return; // Masih dalam spray period
    }

    // Handle spray interval (5 menit OFF)
    unsigned long timeSinceLastSpray = currentMillis - lastAromatherapySprayStart;
    bool timeToSpray = false;

    if (lastAromatherapySprayStart == 0)
    {
        // First spray
        timeToSpray = true;
        Serial.println("💨 Aromatherapy: Semprotan pertama kali");
    }
    else if (timeSinceLastSpray >= 300000) // 5 menit = 300000 ms
    {
        // Time untuk spray lagi
        timeToSpray = true;
        Serial.printf("💨 Aromatherapy: 5 menit berlalu (%.1f menit), semprot lagi\n",
                      timeSinceLastSpray / 60000.0);
    }

    // Start spray jika waktunya
    if (timeToSpray)
    {
        digitalWrite(AROMATHERAPY_PIN, HIGH);
        isAromatherapySpraying = true;
        aromatherapyOnTime = currentMillis;
        Serial.printf("💨 Aromatherapy: Mulai semprot (menit ke-%d dari timer)\n", timerElapsedMinutes);
    }
}

/**
 * @brief Reset semua aromatherapy state dan hardware
 * @note Dipanggil saat timer di-disable atau system reset
 */
void resetAromatherapy()
{
    // Matikan hardware
    if (digitalRead(AROMATHERAPY_PIN) == HIGH)
    {
        digitalWrite(AROMATHERAPY_PIN, LOW);
    }

    // Reset semua state variables
    isAromatherapySpraying = false;
    aromatherapyScheduleActive = false;
    lastAromatherapySprayStart = 0;

    Serial.println("💨 Aromatherapy: Reset semua state");
}

// =================================================================
// WEBSOCKET COMMUNICATION FUNCTIONS
// =================================================================

/**
 * @brief Handle incoming WebSocket messages dari frontend
 * @param arg WebSocket argument (tidak digunakan)
 * @param data Message data dalam bytes
 * @param len Length data dalam bytes
 * @note Parse JSON commands dan update settings accordingly
 */
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    // Parse JSON message
    JsonDocument doc;
    deserializeJson(doc, (char *)data);
    String command = doc["command"];
    Serial.printf("📨 Command diterima: %s\n", command.c_str());

    bool changed = false; // Track apakah ada perubahan settings

    // =================================================================
    // QUERY COMMANDS (tidak mengubah settings)
    // =================================================================
    if (command == "getStatus")
    {
        notifyClients(); // Send current status
        return;
    }
    if (command == "getPlaylist")
    {
        generateAndSendPlaylist(); // Send fixed playlist
        return;
    }

    // =================================================================
    // TIMER COMMANDS
    // =================================================================
    if (command == "timer-toggle")
    {
        currentSettings.timer.on = doc["value"];

        // Jika timer di-disable, reset confirmation dan aromatherapy
        if (!currentSettings.timer.on)
        {
            currentSettings.timer.confirmed = false;
            resetAromatherapy();
        }
        changed = true;
    }
    else if (command == "timer-confirm")
    {
        // Confirm timer hanya jika timer sudah di-enable
        if (currentSettings.timer.on)
        {
            currentSettings.timer.confirmed = true;

            // Parse start dan end time dari frontend
            sscanf(doc["value"]["start"], "%d:%d", &currentSettings.timer.startHour, &currentSettings.timer.startMinute);
            sscanf(doc["value"]["end"], "%d:%d", &currentSettings.timer.endHour, &currentSettings.timer.endMinute);
            changed = true;
        }
    }

    // =================================================================
    // FEATURE COMMANDS (hanya jika timer sudah confirmed)
    // =================================================================
    else if (currentSettings.timer.confirmed)
    {
        if (command == "light-intensity")
        {
            currentSettings.light.intensity = doc["value"];
            Serial.printf("💡 Light intensity: %d%%\n", currentSettings.light.intensity);
            changed = true;
        }
        else if (command == "aroma-toggle")
        {
            currentSettings.aromatherapy.on = doc["value"];

            // Reset aromatherapy state jika di-disable
            if (!currentSettings.aromatherapy.on)
            {
                resetAromatherapy();
            }
            Serial.printf("💨 Aromatherapy toggle: %s\n", currentSettings.aromatherapy.on ? "ON" : "OFF");
            changed = true;
        }
        else if (command == "alarm-toggle")
        {
            currentSettings.alarm.on = doc["value"];
            Serial.printf("🔔 Alarm toggle: %s (fixed track %d)\n",
                          currentSettings.alarm.on ? "ON" : "OFF", ALARM_TRACK_NUMBER);
            changed = true;
        }
        else if (command == "music-toggle")
        {
            currentSettings.music.on = doc["value"];

            if (currentSettings.music.on && dfPlayerInitialized)
            {
                // Start music
                int validTrack = getValidMusicTrackNumber(currentSettings.music.track);
                setMusicVolume(currentSettings.music.volume);
                playMusicTrack(validTrack);
                Serial.printf("🎵 Music ON: Playing track %d (NO REPEAT, MAX 1 HOUR)\n", validTrack);
            }
            else if (!isAlarmPlaying)
            {
                // Stop music (kecuali sedang alarm)
                stopMusic();
                Serial.println("🎵 Music OFF: Stopped");
            }
            changed = true;
        }
        else if (command == "music-track")
        {
            // Change track
            int requestedTrack = doc["value"];
            int validTrack = getValidMusicTrackNumber(requestedTrack);
            currentSettings.music.track = validTrack;

            // Jika musik sedang ON, ganti track langsung
            if (currentSettings.music.on && dfPlayerInitialized)
            {
                playMusicTrack(validTrack);
                Serial.printf("🎵 Music track changed to: %d (NO REPEAT)\n", validTrack);
            }
            changed = true;
        }
        else if (command == "music-volume")
        {
            // Change volume
            int frontendVolume = doc["value"];
            frontendVolume = (frontendVolume / 10) * 10;             // Round ke kelipatan 10%
            int dfPlayerVolume = map(frontendVolume, 0, 100, 0, 30); // Map ke DFPlayer range
            currentSettings.music.volume = dfPlayerVolume;

            // Apply volume change jika DFPlayer ready
            if (dfPlayerInitialized)
            {
                setMusicVolume(dfPlayerVolume);
            }
            Serial.printf("🎵 Music volume: %d%% (DFPlayer: %d/30)\n", frontendVolume, dfPlayerVolume);
            changed = true;
        }
    }
    else
    {
        // Command diabaikan karena timer belum confirmed
        Serial.printf("⚠️ Perintah '%s' diabaikan, timer belum dikonfirmasi.\n", command.c_str());
    }

    // =================================================================
    // SAVE & NOTIFY JIKA ADA PERUBAHAN
    // =================================================================
    if (changed)
    {
        Serial.printf("✅ Perintah '%s' diterima dan diproses.\n", command.c_str());
        saveSettings();           // Simpan ke non-volatile storage
        checkAndApplySchedules(); // Apply changes ke hardware
        notifyClients();          // Notify semua connected clients
    }
}

/**
 * @brief WebSocket event handler untuk connection, disconnection, dan data
 * @param server WebSocket server instance
 * @param client WebSocket client instance
 * @param type Event type (connect, disconnect, data, etc.)
 * @param arg Event argument
 * @param data Event data
 * @param len Event data length
 */
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.printf("🔗 WebSocket klien #%u terhubung\n", client->id());
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.printf("🔌 WebSocket klien #%u terputus\n", client->id());
    }
    else if (type == WS_EVT_DATA)
    {
        handleWebSocketMessage(arg, data, len); // Handle incoming message
    }
}

/**
 * @brief Broadcast current system status ke semua connected WebSocket clients
 * @note Dipanggil setiap kali ada perubahan settings atau secara periodik
 */
void notifyClients()
{
    // Create JSON status update
    JsonDocument doc;
    doc["type"] = "statusUpdate";

    // Timer status
    doc["state"]["timer"]["on"] = currentSettings.timer.on;
    doc["state"]["timer"]["confirmed"] = currentSettings.timer.confirmed;

    // Format time strings
    char startTimeStr[6];
    char endTimeStr[6];
    sprintf(startTimeStr, "%02d:%02d", currentSettings.timer.startHour, currentSettings.timer.startMinute);
    sprintf(endTimeStr, "%02d:%02d", currentSettings.timer.endHour, currentSettings.timer.endMinute);
    doc["state"]["timer"]["start"] = startTimeStr;
    doc["state"]["timer"]["end"] = endTimeStr;

    // Feature status
    doc["state"]["light"]["intensity"] = currentSettings.light.intensity;
    doc["state"]["aromatherapy"]["on"] = currentSettings.aromatherapy.on;
    doc["state"]["alarm"]["on"] = currentSettings.alarm.on;
    doc["state"]["music"]["on"] = currentSettings.music.on;
    doc["state"]["music"]["track"] = currentSettings.music.track;
    doc["state"]["music"]["volume"] = map(currentSettings.music.volume, 0, 30, 0, 100); // Convert ke frontend format

    // Send ke semua connected clients
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

// =================================================================
// MAIN SETUP & LOOP FUNCTIONS
// =================================================================

/**
 * @brief Arduino setup function - initialize semua hardware dan network
 * @note Dipanggil sekali saat ESP32 boot
 */
void setup()
{
    // Initialize serial communication
    Serial.begin(115200);
    Serial.println("\n=== SWELL SMART LAMP STARTUP ===");

    // Initialize I2C untuk RTC
    Wire.begin();

    // =================================================================
    // HARDWARE INITIALIZATION
    // =================================================================

    // Setup aromatherapy pin
    pinMode(AROMATHERAPY_PIN, OUTPUT);
    digitalWrite(AROMATHERAPY_PIN, LOW); // Start dengan OFF

    // Setup PWM untuk LED strips
    ledcSetup(PWM_CHANNEL_WHITE, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(PWM_CHANNEL_YELLOW, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(WHITE_LED_PIN, PWM_CHANNEL_WHITE);
    ledcAttachPin(YELLOW_LED_PIN, PWM_CHANNEL_YELLOW);

    // Initialize RTC
    if (!rtc.refresh())
    {
        Serial.println("❌ KRITIS: RTC DS3231 tidak dapat dibaca!");
    }
    checkAndSetRTC();

    // Load settings dari non-volatile storage
    loadSettings();

    // Initialize DFPlayer Mini
    initializeDFPlayer();

    // =================================================================
    // FILE SYSTEM INITIALIZATION
    // =================================================================
    if (!SPIFFS.begin(true))
    {
        Serial.println("❌ KRITIS: Gagal me-mount SPIFFS file system!");
        return;
    }
    Serial.println("✅ SPIFFS file system mounted successfully.");

    // =================================================================
    // NETWORK INITIALIZATION
    // =================================================================
    Serial.printf("📡 Connecting to WiFi: %s", ssid);
    WiFi.begin(ssid, password);

    // Wait untuk WiFi connection dengan timeout
    int wifiTimeout = 20; // 20 detik timeout
    while (WiFi.status() != WL_CONNECTED && wifiTimeout > 0)
    {
        delay(500);
        Serial.print(".");
        wifiTimeout--;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\n✅ WiFi terhubung!");
        Serial.printf("📍 IP Address: %s\n", WiFi.localIP().toString().c_str());
    }
    else
    {
        Serial.println("\n❌ KRITIS: WiFi gagal terhubung!");
        return;
    }

    // =================================================================
    // WEB SERVER SETUP
    // =================================================================

    // Setup WebSocket event handler
    ws.onEvent(onEvent);
    server.addHandler(&ws);

    // Setup HTTP routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html"); });

    // Serve static files dari SPIFFS
    server.serveStatic("/", SPIFFS, "/");

    // Start web server
    server.begin();
    Serial.println("✅ Web server started on port 80");

    // =================================================================
    // STARTUP COMPLETE
    // =================================================================
    Serial.println("\n=== SWELL SMART LAMP READY ===");
    Serial.printf("📻 Relax Music: %d tracks available (NO REPEAT MODE)\n", RELAX_PLAYLIST_SIZE);
    Serial.printf("🔔 Alarm Track: Fixed to #%d\n", ALARM_TRACK_NUMBER);
    Serial.printf("🎵 DFPlayer Status: %s\n", dfPlayerInitialized ? "OK" : "ERROR");
    Serial.printf("⏰ Music Max Duration: 1 Hour (3600 seconds)\n");
    Serial.printf("🌐 Web Interface: http://%s\n", WiFi.localIP().toString().c_str());
}

/**
 * @brief Arduino main loop function - runs continuously
 * @note Handle WebSocket cleanup, scheduling, dan periodic tasks
 */
void loop()
{
    // Cleanup disconnected WebSocket clients
    ws.cleanupClients();

    // Run main scheduler setiap detik
    if (millis() - lastTimeCheck >= 1000)
    {
        checkAndApplySchedules();
        lastTimeCheck = millis();
    }

    // Broadcast status secara periodik (setiap 60 detik)
    if (millis() - lastStatusBroadcast >= STATUS_BROADCAST_INTERVAL)
    {
        Serial.println("📡 Periodic status broadcast to frontend...");
        notifyClients();
        lastStatusBroadcast = millis();
    }

    // Small delay untuk prevent watchdog reset
    delay(10);
}