/**
 * @file main.cpp
 * @brief Swell Smart Lamp - FIXED VERSION dengan User Settings terpisah dari Execution State
 * @version 2.2 FIXED
 * @date 2025
 * @author Swell Team
 *
 * =================================================================
 * PROJECT OVERVIEW - FIXED VERSION
 * =================================================================
 *
 * ⭐ MAJOR FIX: Memisahkan User Settings (persistent) dari Execution State (runtime)
 * - UserSettings: Konfigurasi user yang disimpan di flash (enabled/disabled)
 * - ExecutionState: Status hardware real-time (active/inactive)
 * - Website sebagai setting skenario, bukan trigger sensor
 * - User bisa set toggle kapan saja, hardware mengikuti schedule otomatis
 *
 * =================================================================
 */

// =================================================================
// PUSTAKA DAN DEPENDENCIES
// =================================================================

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <DFRobotDFPlayerMini.h>
#include <uRTCLib.h>
#include <Wire.h>

// =================================================================
// KONFIGURASI SISTEM
// =================================================================

const char *ssid = "hosssposs";
const char *password = "semogalancarTA";

const int WHITE_LED_PIN = 12;
const int YELLOW_LED_PIN = 14;
const int AROMATHERAPY_PIN = 4;
const int DFPLAYER_RX_PIN = 16;
const int DFPLAYER_TX_PIN = 17;

const int PWM_CHANNEL_WHITE = 0;
const int PWM_CHANNEL_YELLOW = 1;
const int PWM_FREQUENCY = 100;
const int PWM_RESOLUTION = 8;

// =================================================================
// FIXED PLAYLIST CONFIGURATION
// =================================================================

struct MusicTrack
{
    int trackNumber;
    String title;
    String filename;
};

const MusicTrack RELAX_PLAYLIST[] = {
    {1, "AYAT KURSI", "0001_Relax_AYAT_KURSI.mp3"},
    {2, "FAN", "0002_Relax_FAN.mp3"},
    {3, "FROG", "0003_Relax_FROG.mp3"},
    {4, "OCEAN WAVES", "0004_Relax_OCEAN_WAVES.mp3"},
    {6, "RAINDROP", "0006_Relax_RAINDROP.mp3"},
    {7, "RIVER", "0007_Relax_RIVER.mp3"},
    {8, "VACUUM CLEANER", "0008_Relax_VACUM_CLEANER.mp3"},
};

const int RELAX_PLAYLIST_SIZE = sizeof(RELAX_PLAYLIST) / sizeof(RELAX_PLAYLIST[0]);
const int ALARM_TRACK_NUMBER = 5;

// =================================================================
// GLOBAL OBJECTS & VARIABLES
// =================================================================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
uRTCLib rtc(0x68);

HardwareSerial myDFPlayerSerial(2);
DFRobotDFPlayerMini myDFPlayer;
bool dfPlayerInitialized = false;

// =================================================================
// ⭐ FIXED: NEW SETTINGS STRUCTURE - SEPARATED USER CONFIG & EXECUTION STATE
// =================================================================

/**
 * @brief User Settings - Persistent configuration yang disimpan di flash
 * @note Ini adalah konfigurasi user yang TIDAK berubah otomatis oleh scheduler
 */
struct UserSettings
{
    struct Timer
    {
        bool on = false;        // Timer toggle ON/OFF
        bool confirmed = false; // Timer sudah dikonfirmasi
        int startHour = 21;     // Jam mulai sleep phase
        int startMinute = 0;    // Menit mulai sleep phase
        int endHour = 4;        // Jam selesai sleep phase
        int endMinute = 0;      // Menit selesai sleep phase
    } timer;

    struct Light
    {
        int intensity = 50; // Yellow light intensity 0-100%
    } light;

    // ⭐ FIXED: User scenario settings (TIDAK auto-disable)
    struct Aromatherapy
    {
        bool enabled = false; // User setting: apakah aromatherapy akan aktif saat timer?
    } aromatherapy;

    struct Alarm
    {
        bool enabled = false;                 // User setting: apakah alarm akan aktif?
        const int track = ALARM_TRACK_NUMBER; // Fixed ke track 5
    } alarm;

    struct Music
    {
        bool enabled = false; // User setting: apakah music akan aktif saat timer?
        int track = 1;        // Selected track
        int volume = 15;      // Volume level 0-30
    } music;
};

/**
 * @brief Execution State - Runtime status hardware (TIDAK disimpan di flash)
 * @note Ini adalah status real-time hardware yang berubah sesuai schedule
 */
struct ExecutionState
{
    // Hardware execution status (real-time, tidak persistent)
    bool aromatherapyActive = false; // Apakah aromatherapy sedang jalan sekarang?
    bool musicActive = false;        // Apakah music sedang play sekarang?
    bool alarmActive = false;        // Apakah alarm sedang bunyi sekarang?

    // Timing states
    bool inTimerWindow = false;       // Apakah sekarang dalam timer window?
    bool inMusicWindow = false;       // Apakah sekarang dalam music window (1 jam pertama)?
    unsigned long musicStartTime = 0; // Kapan music mulai play
    unsigned long aromaStartTime = 0; // Kapan aromatherapy mulai
};

// ⭐ FIXED: Global instances
UserSettings userSettings;     // User configuration (persistent)
ExecutionState executionState; // Hardware state (runtime only)

// =================================================================
// TIMING & STATE MANAGEMENT VARIABLES
// =================================================================

unsigned long lastAromatherapyCheck = 0;
unsigned long aromatherapyOnTime = 0;
bool isAromatherapySpraying = false;
unsigned long lastAromatherapySprayStart = 0;

unsigned long lastTimeCheck = 0;
unsigned long lastStatusBroadcast = 0;
const unsigned long STATUS_BROADCAST_INTERVAL = 60000;

unsigned long lastRTCBroadcast = 0;
const unsigned long RTC_BROADCAST_INTERVAL = 30000;

unsigned long alarmStopTime = 0;
bool isAlarmPlaying = false;

bool isMusicPaused = false;
bool previousMusicState = false;
unsigned long musicStartTime = 0;
const unsigned long MUSIC_GRACE_PERIOD = 5000;
unsigned long musicPlayStartTime = 0;
const unsigned long MUSIC_MAX_DURATION = 3600000;

// RTC timing variables
unsigned long rtcTimeOffset = 0;
unsigned long rtcTimeReceived = 0;
unsigned long rtcTimeReceivedAt = 0;

// =================================================================
// COMPILE-TIME FUNCTIONS UNTUK RTC SETUP
// =================================================================

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

int getCompileMonth(const char *date)
{
    if (date[0] == 'J' && date[1] == 'a' && date[2] == 'n')
        return 1;
    if (date[0] == 'F' && date[1] == 'e' && date[2] == 'b')
        return 2;
    if (date[0] == 'M' && date[1] == 'a' && date[2] == 'r')
        return 3;
    if (date[0] == 'A' && date[1] == 'p' && date[2] == 'r')
        return 4;
    if (date[0] == 'M' && date[1] == 'a' && date[2] == 'y')
        return 5;
    if (date[0] == 'J' && date[1] == 'u' && date[2] == 'n')
        return 6;
    if (date[0] == 'J' && date[1] == 'u' && date[2] == 'l')
        return 7;
    if (date[0] == 'A' && date[1] == 'u' && date[2] == 'g')
        return 8;
    if (date[0] == 'S' && date[1] == 'e' && date[2] == 'p')
        return 9;
    if (date[0] == 'O' && date[1] == 'c' && date[2] == 't')
        return 10;
    if (date[0] == 'N' && date[1] == 'o' && date[2] == 'v')
        return 11;
    if (date[0] == 'D' && date[1] == 'e' && date[2] == 'c')
        return 12;
    return 0;
}

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

// ⭐ FIXED: Settings management dengan user settings
void saveUserSettings();
void loadUserSettings();

// Hardware initialization
void checkAndSetRTC();
bool initializeDFPlayer();

// Music system
void generateAndSendPlaylist();
int getValidMusicTrackNumber(int requestedTrack);
void playMusicTrack(int trackNumber);
void stopMusic();
void setMusicVolume(int volume);

// ⭐ FIXED: Scheduling system dengan execution state
void checkAndApplySchedules();
bool calculateInTimerWindow(int currentTime, int startTime, int endTime);
bool isInMusicTimeWindow(int currentTimeInMinutes, int startTimeInMinutes);
bool hasReached1HourLimit();

// Aromatherapy system
void handleAromatherapyExecution();
void resetAromatherapy();

// RTC system
void sendRTCTime();
bool calibrateRTC(JsonObject calibrationData);
void broadcastRTCTime();

// WebSocket communication
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void notifyClients();

// File serving
String getContentType(String filename);
void serveFileFromSPIFFS(AsyncWebServerRequest *request, String filename);
void setupWebServerRoutes();
void initializeSPIFFS();

// =================================================================
// ⭐ FIXED: USER SETTINGS MANAGEMENT FUNCTIONS
// =================================================================

void saveUserSettings()
{
    preferences.begin("swell-app", false);
    preferences.putBytes("userSettings", &userSettings, sizeof(userSettings));
    preferences.end();
    Serial.println("📁 User settings saved to flash memory.");
}

void loadUserSettings()
{
    preferences.begin("swell-app", true);

    if (preferences.isKey("userSettings"))
    {
        preferences.getBytes("userSettings", &userSettings, sizeof(userSettings));
        Serial.println("📁 User settings loaded from flash memory.");

        // Validate settings
        userSettings.music.track = getValidMusicTrackNumber(userSettings.music.track);
        if (userSettings.music.volume > 30)
            userSettings.music.volume = 15;
        if (userSettings.music.volume < 0)
            userSettings.music.volume = 5;
    }
    else
    {
        Serial.println("📁 No saved settings found, using defaults.");
    }

    preferences.end();
}

// =================================================================
// HARDWARE INITIALIZATION FUNCTIONS
// =================================================================

void checkAndSetRTC()
{
    rtc.refresh();
    bool needsSetting = false;

    if (rtc.lostPower())
    {
        Serial.println("⚠️ INFO: RTC kehilangan daya. Waktu akan diatur ulang ke compile time.");
        rtc.lostPowerClear();
        needsSetting = true;
    }

    uint8_t compileYear = (uint8_t)(__TIME_YEARS__ - 2000);
    if (rtc.day() != __TIME_DAYS__ || rtc.month() != __TIME_MONTH__ || rtc.year() != compileYear)
    {
        Serial.println("⚠️ INFO: Tanggal RTC tidak akurat. Waktu akan dikalibrasi ke compile time.");
        needsSetting = true;
    }

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

bool initializeDFPlayer()
{
    Serial.println("=== DFPlayer Mini Initialization ===");
    myDFPlayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
    Serial.println("🔊 Menginisialisasi DFPlayer Mini... Mohon tunggu!");

    int retryCount = 0;
    while (retryCount < 3)
    {
        if (myDFPlayer.begin(myDFPlayerSerial))
        {
            Serial.println("✅ DFPlayer Mini berhasil diinisialisasi!");
            myDFPlayer.setTimeOut(500);
            myDFPlayer.volume(userSettings.music.volume);
            myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);

            Serial.printf("📻 DFPlayer Settings:\n");
            Serial.printf("   - Volume: %d/30\n", userSettings.music.volume);
            Serial.printf("   - Fixed playlist: %d relax tracks + 1 alarm track\n", RELAX_PLAYLIST_SIZE);
            Serial.printf("   - Mode: NO REPEAT, MAX 1 HOUR\n");

            dfPlayerInitialized = true;
            return true;
        }

        retryCount++;
        Serial.printf("❌ Percobaan %d gagal, coba lagi...\n", retryCount);
        delay(1000);
    }

    Serial.println("❌ KRITIS: DFPlayer Mini gagal diinisialisasi setelah 3 percobaan!");
    dfPlayerInitialized = false;
    return false;
}

// =================================================================
// RTC SYSTEM FUNCTIONS
// =================================================================

void sendRTCTime()
{
    rtc.refresh();

    JsonDocument rtcDoc;
    rtcDoc["type"] = "rtcTime";
    rtcDoc["rtc"]["year"] = rtc.year() + 2000;
    rtcDoc["rtc"]["month"] = rtc.month();
    rtcDoc["rtc"]["day"] = rtc.day();
    rtcDoc["rtc"]["dayOfWeek"] = rtc.dayOfWeek();
    rtcDoc["rtc"]["hour"] = rtc.hour();
    rtcDoc["rtc"]["minute"] = rtc.minute();
    rtcDoc["rtc"]["second"] = rtc.second();

    String response;
    serializeJson(rtcDoc, response);
    ws.textAll(response);

    Serial.printf("🕐 RTC Time sent: %02d/%02d/%04d %02d:%02d:%02d\n",
                  rtc.day(), rtc.month(), rtc.year() + 2000,
                  rtc.hour(), rtc.minute(), rtc.second());
}

bool calibrateRTC(JsonObject calibrationData)
{
    try
    {
        int year = calibrationData["year"];
        int month = calibrationData["month"];
        int day = calibrationData["day"];
        int dayOfWeek = calibrationData["dayOfWeek"];
        int hour = calibrationData["hour"];
        int minute = calibrationData["minute"];
        int second = calibrationData["second"];

        if (year < 2020 || year > 2099 ||
            month < 1 || month > 12 ||
            day < 1 || day > 31 ||
            dayOfWeek < 1 || dayOfWeek > 7 ||
            hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 59)
        {
            Serial.println("❌ RTC Calibration: Invalid time data received");
            return false;
        }

        Serial.printf("🕐 RTC Calibration: Setting time to %02d/%02d/%04d %02d:%02d:%02d\n",
                      day, month, year, hour, minute, second);

        rtc.set(second, minute, hour, dayOfWeek, day, month, year - 2000);

        delay(100);
        rtc.refresh();
        Serial.println("✅ RTC Calibration: Successfully calibrated with browser time");
        return true;
    }
    catch (...)
    {
        Serial.println("❌ RTC Calibration: Exception occurred during calibration");
        return false;
    }
}

void broadcastRTCTime()
{
    unsigned long currentMillis = millis();
    if (currentMillis - lastRTCBroadcast >= RTC_BROADCAST_INTERVAL)
    {
        sendRTCTime();
        lastRTCBroadcast = currentMillis;
    }
}

// =================================================================
// MUSIC SYSTEM FUNCTIONS
// =================================================================

void playMusicTrack(int trackNumber)
{
    if (!dfPlayerInitialized)
    {
        Serial.println("❌ DFPlayer tidak tersedia untuk memutar musik");
        return;
    }

    Serial.printf("🎵 Playing track %d (NO REPEAT - Max 1 hour)\n", trackNumber);
    myDFPlayer.play(trackNumber);

    musicStartTime = millis();
    musicPlayStartTime = millis();
    Serial.printf("🎵 Music started at: %lu ms\n", musicStartTime);
}

void stopMusic()
{
    if (!dfPlayerInitialized)
        return;

    Serial.println("🎵 Music stopped");
    myDFPlayer.stop();
    isMusicPaused = false;
    musicStartTime = 0;
    musicPlayStartTime = 0;
}

void setMusicVolume(int volume)
{
    if (!dfPlayerInitialized)
        return;

    volume = constrain(volume, 0, 30);
    Serial.printf("🎵 Volume set to: %d/30\n", volume);
    myDFPlayer.volume(volume);
}

// =================================================================
// PLAYLIST MANAGEMENT FUNCTIONS
// =================================================================

void generateAndSendPlaylist()
{
    Serial.printf("📻 Mengirim fixed playlist dengan %d lagu relax music.\n", RELAX_PLAYLIST_SIZE);

    JsonDocument playlistDoc;
    playlistDoc["type"] = "playlist";
    JsonArray playlistArray = playlistDoc["playlist"].to<JsonArray>();

    for (int i = 0; i < RELAX_PLAYLIST_SIZE; i++)
    {
        JsonObject trackObject = playlistArray.add<JsonObject>();
        trackObject["trackNumber"] = RELAX_PLAYLIST[i].trackNumber;
        trackObject["title"] = RELAX_PLAYLIST[i].title;
        trackObject["filename"] = RELAX_PLAYLIST[i].filename;
    }

    String response;
    serializeJson(playlistDoc, response);
    ws.textAll(response);
    Serial.println("✅ Fixed playlist berhasil dikirim ke frontend.");
}

int getValidMusicTrackNumber(int requestedTrack)
{
    for (int i = 0; i < RELAX_PLAYLIST_SIZE; i++)
    {
        if (RELAX_PLAYLIST[i].trackNumber == requestedTrack)
        {
            return requestedTrack;
        }
    }

    Serial.printf("⚠️ Track %d tidak valid, menggunakan track %d\n",
                  requestedTrack, RELAX_PLAYLIST[0].trackNumber);
    return RELAX_PLAYLIST[0].trackNumber;
}

// =================================================================
// ⭐ FIXED: TIMING & SCHEDULING FUNCTIONS
// =================================================================

bool calculateInTimerWindow(int currentTime, int startTime, int endTime)
{
    if (startTime > endTime)
    {
        // Timer melewati midnight
        return (currentTime >= startTime) || (currentTime < endTime);
    }
    else
    {
        // Timer dalam hari yang sama
        return (currentTime >= startTime) && (currentTime < endTime);
    }
}

bool isInMusicTimeWindow(int currentTimeInMinutes, int startTimeInMinutes)
{
    int musicEndTimeInMinutes = (startTimeInMinutes + 60) % 1440;

    bool inMusicTime;
    if (startTimeInMinutes + 60 >= 1440)
    {
        inMusicTime = (currentTimeInMinutes >= startTimeInMinutes) || (currentTimeInMinutes < musicEndTimeInMinutes);
    }
    else
    {
        inMusicTime = (currentTimeInMinutes >= startTimeInMinutes) && (currentTimeInMinutes < musicEndTimeInMinutes);
    }

    return inMusicTime;
}

bool hasReached1HourLimit()
{
    if (musicPlayStartTime == 0)
        return false;
    return (millis() - musicPlayStartTime >= MUSIC_MAX_DURATION);
}

/**
 * ⭐ FIXED: Main scheduler function - TIDAK MENGUBAH USER SETTINGS
 */
void checkAndApplySchedules()
{
    // Jika timer belum dikonfirmasi, reset execution state saja
    if (!userSettings.timer.confirmed)
    {
        executionState.aromatherapyActive = false;
        executionState.musicActive = false;
        executionState.inTimerWindow = false;
        executionState.inMusicWindow = false;

        // Matikan hardware
        ledcWrite(PWM_CHANNEL_WHITE, 0);
        ledcWrite(PWM_CHANNEL_YELLOW, 0);
        digitalWrite(AROMATHERAPY_PIN, LOW);
        stopMusic();
        return;
    }

    // Get current time
    rtc.refresh();
    int currentTimeInMinutes = rtc.hour() * 60 + rtc.minute();
    int startTimeInMinutes = userSettings.timer.startHour * 60 + userSettings.timer.startMinute;
    int endTimeInMinutes = userSettings.timer.endHour * 60 + userSettings.timer.endMinute;

    // ⭐ FIXED: Update execution state flags saja
    executionState.inTimerWindow = calculateInTimerWindow(currentTimeInMinutes, startTimeInMinutes, endTimeInMinutes);
    executionState.inMusicWindow = isInMusicTimeWindow(currentTimeInMinutes, startTimeInMinutes);

    // =================================================================
    // ADAPTIVE LIGHTING
    // =================================================================
    if (executionState.inTimerWindow)
    {
        int yellowBrightness = map(userSettings.light.intensity, 0, 100, 0, 10);
        ledcWrite(PWM_CHANNEL_YELLOW, yellowBrightness);
        ledcWrite(PWM_CHANNEL_WHITE, 0);
    }
    else
    {
        ledcWrite(PWM_CHANNEL_WHITE, 10);
        ledcWrite(PWM_CHANNEL_YELLOW, 0);
    }

    // =================================================================
    // ⭐ FIXED: AROMATHERAPY EXECUTION - TIDAK MENGUBAH USER SETTING
    // =================================================================
    if (userSettings.aromatherapy.enabled && executionState.inMusicWindow)
    {
        // User enabled + in window → execute
        if (!executionState.aromatherapyActive)
        {
            executionState.aromatherapyActive = true;
            executionState.aromaStartTime = millis();
            Serial.println("💨 Aromatherapy: EXECUTION started (user enabled + in window)");
        }
        handleAromatherapyExecution();
    }
    else
    {
        // Outside window or disabled → stop execution BUT DON'T CHANGE USER SETTING
        if (executionState.aromatherapyActive)
        {
            executionState.aromatherapyActive = false;
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            Serial.println("💨 Aromatherapy: EXECUTION stopped (outside window or user disabled)");
            // ⭐ CRITICAL: userSettings.aromatherapy.enabled TIDAK DIUBAH
        }
    }

    // =================================================================
    // ⭐ FIXED: MUSIC EXECUTION - TIDAK MENGUBAH USER SETTING
    // =================================================================
    bool shouldMusicPlay = userSettings.music.enabled &&
                           executionState.inMusicWindow &&
                           !hasReached1HourLimit();

    if (shouldMusicPlay && !executionState.musicActive)
    {
        // Start music execution
        executionState.musicActive = true;
        if (dfPlayerInitialized)
        {
            setMusicVolume(userSettings.music.volume);
            playMusicTrack(userSettings.music.track);
            Serial.println("🎵 Music: EXECUTION started (user enabled + in window)");
        }
    }
    else if (!shouldMusicPlay && executionState.musicActive)
    {
        // Stop music execution
        executionState.musicActive = false;
        stopMusic();
        if (hasReached1HourLimit())
        {
            Serial.println("🎵 Music: EXECUTION stopped (1 hour limit reached)");
        }
        else
        {
            Serial.println("🎵 Music: EXECUTION stopped (outside window)");
        }
        // ⭐ CRITICAL: userSettings.music.enabled TIDAK DIUBAH
    }

    // =================================================================
    // ALARM EXECUTION
    // =================================================================
    if (userSettings.alarm.enabled && !isAlarmPlaying && !executionState.musicActive)
    {
        if (rtc.hour() == userSettings.timer.endHour && rtc.minute() == userSettings.timer.endMinute)
        {
            Serial.printf("🔔 ALARM: Waktunya bangun! Memutar track %d\n", ALARM_TRACK_NUMBER);

            if (dfPlayerInitialized)
            {
                myDFPlayer.volume(30);
                playMusicTrack(ALARM_TRACK_NUMBER);
                isAlarmPlaying = true;
                alarmStopTime = millis() + 300000; // 5 minutes
            }
        }
    }

    // Stop alarm setelah 5 menit
    if (isAlarmPlaying && millis() >= alarmStopTime)
    {
        Serial.println("🔔 ALARM: Durasi 5 menit selesai.");
        stopMusic();
        isAlarmPlaying = false;

        // Resume relax music jika user enabled
        if (userSettings.music.enabled && dfPlayerInitialized)
        {
            int validTrack = getValidMusicTrackNumber(userSettings.music.track);
            setMusicVolume(userSettings.music.volume);
            playMusicTrack(validTrack);
        }
    }
}

// =================================================================
// AROMATHERAPY SYSTEM FUNCTIONS
// =================================================================

void handleAromatherapyExecution()
{
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
        return;
    }

    // Handle spray interval (5 menit OFF)
    unsigned long timeSinceLastSpray = currentMillis - lastAromatherapySprayStart;
    bool timeToSpray = false;

    if (lastAromatherapySprayStart == 0)
    {
        timeToSpray = true;
        Serial.println("💨 Aromatherapy: Semprotan pertama kali");
    }
    else if (timeSinceLastSpray >= 300000) // 5 menit
    {
        timeToSpray = true;
        Serial.printf("💨 Aromatherapy: 5 menit berlalu (%.1f menit), semprot lagi\n",
                      timeSinceLastSpray / 60000.0);
    }

    if (timeToSpray)
    {
        digitalWrite(AROMATHERAPY_PIN, HIGH);
        isAromatherapySpraying = true;
        aromatherapyOnTime = currentMillis;
        Serial.println("💨 Aromatherapy: Mulai semprot");
    }
}

void resetAromatherapy()
{
    if (digitalRead(AROMATHERAPY_PIN) == HIGH)
    {
        digitalWrite(AROMATHERAPY_PIN, LOW);
    }

    isAromatherapySpraying = false;
    lastAromatherapySprayStart = 0;
    Serial.println("💨 Aromatherapy: Reset semua state");
}

// =================================================================
// ⭐ FIXED: WEBSOCKET COMMUNICATION FUNCTIONS
// =================================================================

/**
 * ⭐ FIXED: Handle WebSocket messages - UPDATE USER SETTINGS SAJA
 */
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    JsonDocument doc;
    deserializeJson(doc, (char *)data);
    String command = doc["command"];
    Serial.printf("📨 Command diterima: %s\n", command.c_str());

    bool changed = false;

    // =================================================================
    // QUERY COMMANDS
    // =================================================================
    if (command == "getStatus")
    {
        notifyClients();
        return;
    }
    if (command == "getPlaylist")
    {
        generateAndSendPlaylist();
        return;
    }
    if (command == "getRTC")
    {
        sendRTCTime();
        return;
    }

    // =================================================================
    // RTC CALIBRATION COMMAND
    // =================================================================
    if (command == "rtc-calibrate")
    {
        JsonObject calibrationData = doc["value"];
        bool calibrationSuccess = calibrateRTC(calibrationData);

        JsonDocument responseDoc;
        responseDoc["type"] = "rtcCalibrated";
        responseDoc["success"] = calibrationSuccess;
        if (!calibrationSuccess)
        {
            responseDoc["error"] = "Failed to calibrate RTC";
        }

        String response;
        serializeJson(responseDoc, response);
        ws.textAll(response);

        if (calibrationSuccess)
        {
            delay(200);
            sendRTCTime();
        }
        return;
    }

    // =================================================================
    // TIMER COMMANDS
    // =================================================================
    if (command == "timer-toggle")
    {
        userSettings.timer.on = doc["value"];

        if (!userSettings.timer.on)
        {
            userSettings.timer.confirmed = false;
            // Reset execution state, not user settings
            executionState.aromatherapyActive = false;
            executionState.musicActive = false;
            resetAromatherapy();
        }
        changed = true;
    }
    else if (command == "timer-confirm")
    {
        if (userSettings.timer.on)
        {
            userSettings.timer.confirmed = true;
            sscanf(doc["value"]["start"], "%d:%d", &userSettings.timer.startHour, &userSettings.timer.startMinute);
            sscanf(doc["value"]["end"], "%d:%d", &userSettings.timer.endHour, &userSettings.timer.endMinute);
            changed = true;
        }
    }

    // =================================================================
    // ⭐ FIXED: FEATURE COMMANDS - UPDATE USER SETTINGS SAJA
    // =================================================================
    else if (userSettings.timer.confirmed)
    {
        if (command == "light-intensity")
        {
            userSettings.light.intensity = doc["value"];
            Serial.printf("💡 Light intensity USER SETTING: %d%%\n", userSettings.light.intensity);
            changed = true;
        }
        else if (command == "aroma-toggle")
        {
            // ⭐ FIXED: Update user setting, bukan execution state
            userSettings.aromatherapy.enabled = doc["value"];
            Serial.printf("💨 Aromatherapy USER SETTING: %s\n",
                          userSettings.aromatherapy.enabled ? "ENABLED" : "DISABLED");

            // Reset execution state jika user disable
            if (!userSettings.aromatherapy.enabled && executionState.aromatherapyActive)
            {
                executionState.aromatherapyActive = false;
                resetAromatherapy();
            }
            changed = true;
        }
        else if (command == "alarm-toggle")
        {
            userSettings.alarm.enabled = doc["value"];
            Serial.printf("🔔 Alarm USER SETTING: %s (fixed track %d)\n",
                          userSettings.alarm.enabled ? "ENABLED" : "DISABLED", ALARM_TRACK_NUMBER);
            changed = true;
        }
        else if (command == "music-toggle")
        {
            // ⭐ FIXED: Update user setting, bukan execution state
            userSettings.music.enabled = doc["value"];
            Serial.printf("🎵 Music USER SETTING: %s\n",
                          userSettings.music.enabled ? "ENABLED" : "DISABLED");

            // Reset execution state jika user disable
            if (!userSettings.music.enabled && executionState.musicActive)
            {
                executionState.musicActive = false;
                stopMusic();
            }
            changed = true;
        }
        else if (command == "music-track")
        {
            int requestedTrack = doc["value"];
            int validTrack = getValidMusicTrackNumber(requestedTrack);
            userSettings.music.track = validTrack;

            // Jika musik sedang active, ganti track langsung
            if (executionState.musicActive && dfPlayerInitialized)
            {
                playMusicTrack(validTrack);
                Serial.printf("🎵 Music track changed to: %d (NO REPEAT)\n", validTrack);
            }
            changed = true;
        }
        else if (command == "music-volume")
        {
            int frontendVolume = doc["value"];
            frontendVolume = (frontendVolume / 10) * 10;
            int dfPlayerVolume = map(frontendVolume, 0, 100, 0, 30);
            userSettings.music.volume = dfPlayerVolume;

            if (dfPlayerInitialized)
            {
                setMusicVolume(dfPlayerVolume);
            }
            Serial.printf("🎵 Music volume USER SETTING: %d%% (DFPlayer: %d/30)\n", frontendVolume, dfPlayerVolume);
            changed = true;
        }
    }
    else
    {
        Serial.printf("⚠️ Perintah '%s' diabaikan, timer belum dikonfirmasi.\n", command.c_str());
    }

    // =================================================================
    // SAVE & NOTIFY JIKA ADA PERUBAHAN
    // =================================================================
    if (changed)
    {
        Serial.printf("✅ Perintah '%s' diterima dan diproses.\n", command.c_str());
        saveUserSettings();       // ⭐ Save user settings
        checkAndApplySchedules(); // Apply ke hardware execution
        notifyClients();          // ⭐ Broadcast user settings + execution state
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.printf("🔗 WebSocket klien #%u terhubung\n", client->id());
        sendRTCTime();
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.printf("🔌 WebSocket klien #%u terputus\n", client->id());
    }
    else if (type == WS_EVT_DATA)
    {
        handleWebSocketMessage(arg, data, len);
    }
}

/**
 * ⭐ FIXED: Notify clients - KIRIM USER SETTINGS + EXECUTION STATE INFO
 */
void notifyClients()
{
    JsonDocument doc;
    doc["type"] = "statusUpdate";

    // ⭐ FIXED: Send user settings (persistent config yang bisa di-set user)
    doc["state"]["timer"]["on"] = userSettings.timer.on;
    doc["state"]["timer"]["confirmed"] = userSettings.timer.confirmed;

    char startTimeStr[6];
    char endTimeStr[6];
    sprintf(startTimeStr, "%02d:%02d", userSettings.timer.startHour, userSettings.timer.startMinute);
    sprintf(endTimeStr, "%02d:%02d", userSettings.timer.endHour, userSettings.timer.endMinute);
    doc["state"]["timer"]["start"] = startTimeStr;
    doc["state"]["timer"]["end"] = endTimeStr;

    // ⭐ FIXED: Feature user settings (yang akan di-reflect di toggle)
    doc["state"]["aromatherapy"]["enabled"] = userSettings.aromatherapy.enabled;
    doc["state"]["music"]["enabled"] = userSettings.music.enabled;
    doc["state"]["alarm"]["enabled"] = userSettings.alarm.enabled;

    doc["state"]["music"]["track"] = userSettings.music.track;
    doc["state"]["music"]["volume"] = map(userSettings.music.volume, 0, 30, 0, 100);
    doc["state"]["light"]["intensity"] = userSettings.light.intensity;

    // ⭐ ADDITIONAL: Send execution state untuk info (optional)
    doc["executionState"]["aromatherapyActive"] = executionState.aromatherapyActive;
    doc["executionState"]["musicActive"] = executionState.musicActive;
    doc["executionState"]["inTimerWindow"] = executionState.inTimerWindow;
    doc["executionState"]["inMusicWindow"] = executionState.inMusicWindow;

    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

// =================================================================
// ⭐ FIXED: FILE SERVING FUNCTIONS - TANPA FALLBACK
// =================================================================

String getContentType(String filename)
{
    if (filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".png"))
        return "image/png";
    else if (filename.endsWith(".jpg"))
        return "image/jpeg";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".gif"))
        return "image/gif";
    return "text/plain";
}

void serveFileFromSPIFFS(AsyncWebServerRequest *request, String filename)
{
    String contentType = getContentType(filename);

    if (SPIFFS.exists(filename))
    {
        Serial.printf("✅ Serving file: %s (Type: %s)\n", filename.c_str(), contentType.c_str());
        request->send(SPIFFS, filename, contentType);
        return;
    }

    // ⭐ FIXED: Tidak ada lagi fallback folder reference
    Serial.printf("❌ File not found: %s\n", filename.c_str());
    request->send(404, "text/plain", "File Not Found");
}

void setupWebServerRoutes()
{
    ws.onEvent(onEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/index.html"); });

    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/index.html"); });

    server.on("/swell-homepage.html", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/swell-homepage.html"); });

    server.on("/swell-device-detail.html", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/swell-device-detail.html"); });

    server.on("/swell-styles.css", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/swell-styles.css"); });

    server.on("/swell-script.js", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/swell-script.js"); });

    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request)
              { serveFileFromSPIFFS(request, "/logo.png"); });

    server.onNotFound([](AsyncWebServerRequest *request)
                      {
        String path = request->url();
        Serial.printf("📄 Requested: %s\n", path.c_str());
        serveFileFromSPIFFS(request, path); });

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    server.begin();
    Serial.println("✅ Web server started successfully");
}

void initializeSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("❌ KRITIS: Gagal me-mount SPIFFS file system!");
        return;
    }
    Serial.println("✅ SPIFFS file system mounted successfully.");

    Serial.println("📁 Available files in SPIFFS root:");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        Serial.printf("   - %s (%d bytes)\n", file.name(), file.size());
        file = root.openNextFile();
    }

    String requiredFiles[] = {
        "/index.html",
        "/swell-homepage.html",
        "/swell-device-detail.html",
        "/swell-styles.css",
        "/swell-script.js",
        "/logo.png"};

    Serial.println("🔍 Checking required files:");
    for (String filename : requiredFiles)
    {
        if (SPIFFS.exists(filename))
        {
            Serial.printf("   ✅ %s\n", filename.c_str());
        }
        else
        {
            Serial.printf("   ❌ %s MISSING!\n", filename.c_str());
        }
    }
}

// =================================================================
// MAIN SETUP & LOOP FUNCTIONS
// =================================================================

void setup()
{
    Serial.begin(115200);
    Serial.println("\n=== SWELL SMART LAMP STARTUP - FIXED VERSION ===");

    Wire.begin();

    // Hardware initialization
    pinMode(AROMATHERAPY_PIN, OUTPUT);
    digitalWrite(AROMATHERAPY_PIN, LOW);

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

    rtc.refresh();
    Serial.printf("🕐 Initial RTC Time: %02d/%02d/%04d %02d:%02d:%02d\n",
                  rtc.day(), rtc.month(), rtc.year() + 2000,
                  rtc.hour(), rtc.minute(), rtc.second());

    // ⭐ FIXED: Load user settings
    loadUserSettings();

    initializeDFPlayer();

    // File system initialization
    initializeSPIFFS();

    // Network initialization
    Serial.printf("📡 Connecting to WiFi: %s", ssid);
    WiFi.begin(ssid, password);

    int wifiTimeout = 20;
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

    // Setup web server
    setupWebServerRoutes();

    Serial.println("\n=== SWELL SMART LAMP READY - FIXED VERSION ===");
    Serial.println("⭐ FIXED: User settings separated from execution state");
    Serial.println("⭐ Users can now configure scenarios anytime!");
    Serial.printf("📻 Relax Music: %d tracks available (NO REPEAT MODE)\n", RELAX_PLAYLIST_SIZE);
    Serial.printf("🔔 Alarm Track: Fixed to #%d\n", ALARM_TRACK_NUMBER);
    Serial.printf("🎵 DFPlayer Status: %s\n", dfPlayerInitialized ? "OK" : "ERROR");
    Serial.printf("🌐 Web Interface: http://%s\n", WiFi.localIP().toString().c_str());
}

void loop()
{
    ws.cleanupClients();

    if (millis() - lastTimeCheck >= 1000)
    {
        checkAndApplySchedules();
        lastTimeCheck = millis();
    }

    if (millis() - lastStatusBroadcast >= STATUS_BROADCAST_INTERVAL)
    {
        Serial.println("📡 Periodic status broadcast to frontend...");
        notifyClients();
        lastStatusBroadcast = millis();
    }

    broadcastRTCTime();

    delay(10);
}