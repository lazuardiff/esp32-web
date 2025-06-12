/**
 * @file main.cpp
 * @brief Swell Smart Lamp - FIXED Music Logic
 * @details Perbaikan pada logika musik agar berfungsi dengan benar dalam 1 jam pertama timer
 */

// Pustaka Inti
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>

// Pustaka Fitur
#include <DFRobotDFPlayerMini.h>
#include <uRTCLib.h>
#include <Wire.h>

// =================================================================
// KONFIGURASI
// =================================================================

const char *ssid = "hosssposs";
const char *password = "semogalancarTA";

// --- Konfigurasi Pin ---
const int WHITE_LED_PIN = 12;
const int YELLOW_LED_PIN = 14;
const int AROMATHERAPY_PIN = 4;
const int DFPLAYER_RX_PIN = 16; // RX2 ESP32
const int DFPLAYER_TX_PIN = 17; // TX2 ESP32

// --- Konfigurasi PWM ---
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
    {1, "Relax Music - Lesgo", "0001_Relax_lesgo.mp3"},
    {2, "Relax Music - Lesgo 2", "0002_Relax_lesgo2.mp3"},
    {3, "Relax Music - Lesgo 3", "0003_Relax_lesgo3.mp3"},
    {4, "Relax Music - Lesgo 4", "0004_Relax_lesgo4.mp3"},
    {6, "Relax Music - Lesgo 5", "0006_Relax_lesgo5.mp3"},
    {7, "Relax Music - Lesgo 6", "0007_Relax_lesgo6.mp3"}};

const int RELAX_PLAYLIST_SIZE = sizeof(RELAX_PLAYLIST) / sizeof(RELAX_PLAYLIST[0]);
const int ALARM_TRACK_NUMBER = 5; // 0005_Alarm_mari.mp3

// =================================================================
// OBJEK & VARIABEL GLOBAL
// =================================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
uRTCLib rtc(0x68);

HardwareSerial myDFPlayerSerial(2);
DFRobotDFPlayerMini myDFPlayer;
bool dfPlayerInitialized = false;

// --- Variabel untuk manajemen waktu non-blocking ---
unsigned long lastAromatherapyCheck = 0;
unsigned long aromatherapyOnTime = 0;
bool isAromatherapySpraying = false;
unsigned long lastTimeCheck = 0;
unsigned long alarmStopTime = 0;
bool isAlarmPlaying = false;
bool isMusicPaused = false;
unsigned long lastAromatherapySprayStart = 0;
bool aromatherapyScheduleActive = false;
unsigned long lastStatusBroadcast = 0;
const unsigned long STATUS_BROADCAST_INTERVAL = 60000;
bool previousMusicState = false;

// ⭐ NEW: Variabel untuk mencegah musik langsung dimatikan setelah dinyalakan
unsigned long musicStartTime = 0;
const unsigned long MUSIC_GRACE_PERIOD = 5000; // 5 detik grace period

// ⭐ NEW: Variabel untuk 30-second looping relax music
unsigned long lastMusicRestartTime = 0;
const unsigned long MUSIC_RESTART_INTERVAL = 30000; // 30 detik restart interval

// Struct untuk menyimpan semua pengaturan
struct Settings
{
    struct Timer
    {
        bool on = false;
        bool confirmed = false;
        int startHour = 21;
        int startMinute = 0;
        int endHour = 4;
        int endMinute = 0;
    } timer;

    struct Light
    {
        int intensity = 50;
    } light;

    struct Aromatherapy
    {
        bool on = false;
    } aromatherapy;

    struct Alarm
    {
        bool on = false;
        const int track = ALARM_TRACK_NUMBER;
    } alarm;

    struct Music
    {
        bool on = false;
        int track = 1;
        int volume = 15;
    } music;
};
Settings currentSettings;

// =================================================================
// FUNGSI WAKTU KOMPILASI (unchanged)
// =================================================================
int getCompileDayOfWeek(const char *ts)
{
    if (ts[0] == 'S' && ts[1] == 'u')
        return 1;
    if (ts[0] == 'M' && ts[1] == 'o')
        return 2;
    if (ts[0] == 'T' && ts[1] == 'u')
        return 3;
    if (ts[0] == 'W' && ts[1] == 'e')
        return 4;
    if (ts[0] == 'T' && ts[1] == 'h')
        return 5;
    if (ts[0] == 'F' && ts[1] == 'r')
        return 6;
    if (ts[0] == 'S' && ts[1] == 'a')
        return 7;
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
// DEKLARASI FUNGSI
// =================================================================
void saveSettings();
void loadSettings();
void checkAndSetRTC();
bool initializeDFPlayer();
void generateAndSendPlaylist();
void checkAndApplySchedules();
void checkAromatherapyLogic(int currentTimeInMinutes, int startTimeInMinutes);
void resetAromatherapy();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void notifyClients();
int getValidMusicTrackNumber(int requestedTrack);
void playMusicTrack(int trackNumber);
void stopMusic();
void setMusicVolume(int volume);
bool isInMusicTimeWindow(int currentTimeInMinutes, int startTimeInMinutes);

// =================================================================
// FUNGSI INTI (unchanged functions)
// =================================================================

void saveSettings()
{
    preferences.begin("swell-app", false);
    preferences.putBytes("settings", &currentSettings, sizeof(currentSettings));
    preferences.end();
    Serial.println("Pengaturan disimpan.");
}

void loadSettings()
{
    preferences.begin("swell-app", true);
    if (preferences.isKey("settings"))
    {
        preferences.getBytes("settings", &currentSettings, sizeof(currentSettings));
        Serial.println("Pengaturan dimuat dari memori.");
        currentSettings.music.track = getValidMusicTrackNumber(currentSettings.music.track);
        if (currentSettings.music.volume > 30)
            currentSettings.music.volume = 15;
        if (currentSettings.music.volume < 0)
            currentSettings.music.volume = 5;
    }
    else
    {
        Serial.println("Tidak ada pengaturan tersimpan, menggunakan default.");
    }
    preferences.end();
}

void checkAndSetRTC()
{
    rtc.refresh();
    bool needsSetting = false;
    if (rtc.lostPower())
    {
        Serial.println("INFO: RTC kehilangan daya. Waktu akan diatur ulang.");
        rtc.lostPowerClear();
        needsSetting = true;
    }

    uint8_t compileYear = (uint8_t)(__TIME_YEARS__ - 2000);
    if (rtc.day() != __TIME_DAYS__ || rtc.month() != __TIME_MONTH__ || rtc.year() != compileYear)
    {
        Serial.println("INFO: Tanggal RTC tidak akurat. Waktu akan dikalibrasi.");
        needsSetting = true;
    }
    if (needsSetting)
    {
        rtc.set(__TIME_SECONDS__, __TIME_MINUTES__, __TIME_HOURS__, __TIME_DOW__, __TIME_DAYS__, __TIME_MONTH__, compileYear);
        Serial.println("OK: Waktu RTC berhasil disetel.");
    }
    else
    {
        Serial.println("OK: Waktu RTC sudah akurat.");
    }
}

bool initializeDFPlayer()
{
    Serial.println("=== DFPlayer Mini Initialization ===");
    myDFPlayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
    Serial.println("Menginisialisasi DFPlayer Mini... Mohon tunggu!");

    int retryCount = 0;
    while (retryCount < 3)
    {
        if (myDFPlayer.begin(myDFPlayerSerial))
        {
            Serial.println("✅ DFPlayer Mini berhasil diinisialisasi!");
            myDFPlayer.setTimeOut(500);
            myDFPlayer.volume(currentSettings.music.volume);
            myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);

            Serial.printf("📻 DFPlayer Settings:\n");
            Serial.printf("   - Timeout: 500ms\n");
            Serial.printf("   - Volume: %d/30\n", currentSettings.music.volume);
            Serial.printf("   - EQ: Normal\n");
            Serial.printf("   - Fixed playlist: %d relax tracks + 1 alarm track\n", RELAX_PLAYLIST_SIZE);

            dfPlayerInitialized = true;
            return true;
        }

        retryCount++;
        Serial.printf("❌ Percobaan %d gagal, coba lagi...\n", retryCount);
        delay(1000);
    }

    Serial.println("❌ KRITIS: DFPlayer Mini gagal diinisialisasi!");
    dfPlayerInitialized = false;
    return false;
}

void playMusicTrack(int trackNumber)
{
    if (!dfPlayerInitialized)
    {
        Serial.println("❌ DFPlayer tidak tersedia untuk memutar musik");
        return;
    }

    Serial.printf("🎵 Playing track: %d\n", trackNumber);
    myDFPlayer.enableLoop();
    myDFPlayer.play(trackNumber);

    // ⭐ NEW: Set music start time untuk grace period dan restart tracking
    musicStartTime = millis();
    lastMusicRestartTime = millis(); // Track untuk 30-second restart
    Serial.printf("🎵 Music start time recorded: %lu\n", musicStartTime);
    Serial.printf("🎵 30-second restart timer started\n");
}

void stopMusic()
{
    if (!dfPlayerInitialized)
        return;

    Serial.println("🎵 Music stopped");
    myDFPlayer.stop();
    isMusicPaused = false;
    musicStartTime = 0;       // Reset start time
    lastMusicRestartTime = 0; // Reset restart timer
}

void setMusicVolume(int volume)
{
    if (!dfPlayerInitialized)
        return;
    volume = constrain(volume, 0, 30);
    Serial.printf("🎵 Volume set to: %d/30\n", volume);
    myDFPlayer.volume(volume);
}

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
    Serial.printf("⚠️ Track %d tidak valid, menggunakan track %d\n", requestedTrack, RELAX_PLAYLIST[0].trackNumber);
    return RELAX_PLAYLIST[0].trackNumber;
}

// ⭐ NEW: Fungsi terpisah untuk mengecek apakah sedang dalam window waktu musik
bool isInMusicTimeWindow(int currentTimeInMinutes, int startTimeInMinutes)
{
    // Musik aktif dalam 1 jam pertama timer (startTime sampai startTime + 60 menit)
    int musicEndTimeInMinutes = (startTimeInMinutes + 60) % 1440;

    Serial.printf("🎵 Music Time Check:\n");
    Serial.printf("   Current: %02d:%02d (%d min)\n",
                  currentTimeInMinutes / 60, currentTimeInMinutes % 60, currentTimeInMinutes);
    Serial.printf("   Start: %02d:%02d (%d min)\n",
                  startTimeInMinutes / 60, startTimeInMinutes % 60, startTimeInMinutes);
    Serial.printf("   End: %02d:%02d (%d min)\n",
                  musicEndTimeInMinutes / 60, musicEndTimeInMinutes % 60, musicEndTimeInMinutes);

    bool inMusicTime;

    // ⭐ FIXED: Perbaikan logika untuk handle midnight wrap-around
    if (startTimeInMinutes + 60 >= 1440)
    {
        // Jika music window melewati midnight (contoh: 23:30-00:30)
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

void checkAndApplySchedules()
{
    if (!currentSettings.timer.confirmed)
    {
        ledcWrite(PWM_CHANNEL_WHITE, 0);
        ledcWrite(PWM_CHANNEL_YELLOW, 0);
        if (digitalRead(AROMATHERAPY_PIN) == HIGH)
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            aromatherapyScheduleActive = false;
        }
        return;
    }

    rtc.refresh();
    int currentTimeInMinutes = rtc.hour() * 60 + rtc.minute();
    int startTimeInMinutes = currentSettings.timer.startHour * 60 + currentSettings.timer.startMinute;
    int endTimeInMinutes = currentSettings.timer.endHour * 60 + currentSettings.timer.endMinute;

    bool isTimerActivePhase = (startTimeInMinutes > endTimeInMinutes) ? (currentTimeInMinutes >= startTimeInMinutes || currentTimeInMinutes < endTimeInMinutes) : (currentTimeInMinutes >= startTimeInMinutes && currentTimeInMinutes < endTimeInMinutes);

    // Light control
    if (isTimerActivePhase)
    {
        int yellowBrightness = map(currentSettings.light.intensity, 0, 100, 0, 10);
        ledcWrite(PWM_CHANNEL_YELLOW, yellowBrightness);
        ledcWrite(PWM_CHANNEL_WHITE, 0);
    }
    else
    {
        ledcWrite(PWM_CHANNEL_WHITE, 10);
        ledcWrite(PWM_CHANNEL_YELLOW, 0);
    }

    bool aromaStateBefore = currentSettings.aromatherapy.on;
    checkAromatherapyLogic(currentTimeInMinutes, startTimeInMinutes);

    if (aromaStateBefore && !currentSettings.aromatherapy.on)
    {
        Serial.println("🔔 NOTIFICATION: Aromatherapy auto-disabled, notifying frontend...");
        saveSettings();
        notifyClients();
    }

    // ⭐ FIXED: Music logic dengan grace period dan debugging
    bool inMusicTime = isInMusicTimeWindow(currentTimeInMinutes, startTimeInMinutes);
    unsigned long currentMillis = millis();
    bool hasGracePeriod = (musicStartTime > 0) && (currentMillis - musicStartTime < MUSIC_GRACE_PERIOD);

    // ⭐ IMPROVED: Force disable music jika waktu habis (lebih aggressive)
    if (currentSettings.music.on && !inMusicTime && !isAlarmPlaying && !hasGracePeriod)
    {
        stopMusic();
        Serial.println("🎵 Relax Music: Waktu habis.");
        currentSettings.music.on = false; // Auto-disable toggle
        Serial.printf("🎵 Music auto-disabled - Toggle now: %s\n", currentSettings.music.on ? "ON" : "OFF");
        saveSettings();
        notifyClients();
    }
    else if (currentSettings.music.on && hasGracePeriod)
    {
        Serial.printf("🎵 Music in grace period (%.1fs remaining)\n",
                      (MUSIC_GRACE_PERIOD - (currentMillis - musicStartTime)) / 1000.0);
    }
    else if (currentSettings.music.on && !inMusicTime && !hasGracePeriod)
    {
        // ⭐ DEBUGGING: Show why music might not be auto-disabled
        Serial.printf("🎵 Music should be disabled but conditions not met:\n");
        Serial.printf("   - Music ON: %s\n", currentSettings.music.on ? "YES" : "NO");
        Serial.printf("   - In Music Time: %s\n", inMusicTime ? "YES" : "NO");
        Serial.printf("   - Alarm Playing: %s\n", isAlarmPlaying ? "YES" : "NO");
        Serial.printf("   - Has Grace Period: %s\n", hasGracePeriod ? "YES" : "NO");
    }

    // ⭐ NEW: 30-Second Restart Logic untuk Relax Music
    if (currentSettings.music.on && !isAlarmPlaying && inMusicTime && dfPlayerInitialized)
    {
        if (lastMusicRestartTime > 0 && (currentMillis - lastMusicRestartTime >= MUSIC_RESTART_INTERVAL))
        {
            int currentTrack = getValidMusicTrackNumber(currentSettings.music.track);
            Serial.printf("🎵 30-second restart: Restarting track %d\n", currentTrack);

            // Restart lagu yang sama
            myDFPlayer.play(currentTrack);
            lastMusicRestartTime = currentMillis;

            Serial.printf("🎵 Next restart in 30 seconds\n");
        }
        else if (lastMusicRestartTime > 0)
        {
            // Show countdown untuk debugging (setiap 5 detik)
            unsigned long timeUntilRestart = MUSIC_RESTART_INTERVAL - (currentMillis - lastMusicRestartTime);
            if ((currentMillis / 5000) != ((currentMillis - 1000) / 5000)) // Every 5 seconds
            {
                Serial.printf("🎵 Next restart in %.1f seconds\n", timeUntilRestart / 1000.0);
            }
        }
    }

    // Alarm logic (unchanged)
    if (currentSettings.alarm.on && !isAlarmPlaying && !currentSettings.music.on)
    {
        if (rtc.hour() == currentSettings.timer.endHour && rtc.minute() == currentSettings.timer.endMinute)
        {
            Serial.printf("🔔 ALARM: Waktunya bangun! Memutar track %d\n", ALARM_TRACK_NUMBER);
            if (dfPlayerInitialized)
            {
                myDFPlayer.volume(30);
                playMusicTrack(ALARM_TRACK_NUMBER);
                isAlarmPlaying = true;
                alarmStopTime = millis() + 300000;
            }
            else
            {
                Serial.println("❌ Alarm: DFPlayer tidak tersedia");
            }
        }
    }

    if (isAlarmPlaying && millis() >= alarmStopTime)
    {
        Serial.println("🔔 ALARM: Durasi selesai.");
        stopMusic();
        isAlarmPlaying = false;

        if (currentSettings.music.on && dfPlayerInitialized)
        {
            int validTrack = getValidMusicTrackNumber(currentSettings.music.track);
            setMusicVolume(currentSettings.music.volume);
            playMusicTrack(validTrack);
        }
    }
}

// ⭐ AROMATHERAPY FUNCTION (unchanged)
void checkAromatherapyLogic(int currentTimeInMinutes, int startTimeInMinutes)
{
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

    int timerElapsedMinutes;
    if (currentTimeInMinutes >= startTimeInMinutes)
    {
        timerElapsedMinutes = currentTimeInMinutes - startTimeInMinutes;
    }
    else
    {
        timerElapsedMinutes = (1440 - startTimeInMinutes) + currentTimeInMinutes;
    }

    bool inFirstHour = (timerElapsedMinutes < 60);

    if (!inFirstHour)
    {
        if (digitalRead(AROMATHERAPY_PIN) == HIGH)
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            Serial.println("💨 Aromatherapy: Jam pertama selesai, dimatikan");
        }
        aromatherapyScheduleActive = false;

        if (currentSettings.aromatherapy.on)
        {
            currentSettings.aromatherapy.on = false;
            Serial.println("💨 Aromatherapy: Auto-disabled setelah 1 jam");
        }
        return;
    }

    aromatherapyScheduleActive = true;
    unsigned long currentMillis = millis();

    if (isAromatherapySpraying)
    {
        if (currentMillis - aromatherapyOnTime >= 5000)
        {
            digitalWrite(AROMATHERAPY_PIN, LOW);
            isAromatherapySpraying = false;
            lastAromatherapySprayStart = currentMillis;
            Serial.println("💨 Aromatherapy: Semprotan selesai (5 detik)");
        }
        return;
    }

    unsigned long timeSinceLastSpray = currentMillis - lastAromatherapySprayStart;
    bool timeToSpray = false;

    if (lastAromatherapySprayStart == 0)
    {
        timeToSpray = true;
        Serial.println("💨 Aromatherapy: Semprotan pertama kali");
    }
    else if (timeSinceLastSpray >= 300000)
    {
        timeToSpray = true;
        Serial.printf("💨 Aromatherapy: 5 menit berlalu (%.1f menit), semprot lagi\n", timeSinceLastSpray / 60000.0);
    }

    if (timeToSpray)
    {
        digitalWrite(AROMATHERAPY_PIN, HIGH);
        isAromatherapySpraying = true;
        aromatherapyOnTime = currentMillis;
        Serial.printf("💨 Aromatherapy: Mulai semprot (menit ke-%d dari timer)\n", timerElapsedMinutes);
    }
}

void resetAromatherapy()
{
    if (digitalRead(AROMATHERAPY_PIN) == HIGH)
    {
        digitalWrite(AROMATHERAPY_PIN, LOW);
    }
    isAromatherapySpraying = false;
    aromatherapyScheduleActive = false;
    lastAromatherapySprayStart = 0;
    Serial.println("💨 Aromatherapy: Reset semua state");
}

// ⭐ WEBSOCKET HANDLERS (unchanged)
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    JsonDocument doc;
    deserializeJson(doc, (char *)data);
    String command = doc["command"];
    Serial.printf("📨 Command diterima: %s\n", command.c_str());
    bool changed = false;

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

    if (command == "timer-toggle")
    {
        currentSettings.timer.on = doc["value"];
        if (!currentSettings.timer.on)
        {
            currentSettings.timer.confirmed = false;
            resetAromatherapy();
        }
        changed = true;
    }
    else if (command == "timer-confirm")
    {
        if (currentSettings.timer.on)
        {
            currentSettings.timer.confirmed = true;
            sscanf(doc["value"]["start"], "%d:%d", &currentSettings.timer.startHour, &currentSettings.timer.startMinute);
            sscanf(doc["value"]["end"], "%d:%d", &currentSettings.timer.endHour, &currentSettings.timer.endMinute);
            changed = true;
        }
    }
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
            Serial.printf("🔔 Alarm toggle: %s (fixed track %d)\n", currentSettings.alarm.on ? "ON" : "OFF", ALARM_TRACK_NUMBER);
            changed = true;
        }
        else if (command == "music-toggle")
        {
            currentSettings.music.on = doc["value"];
            if (currentSettings.music.on && dfPlayerInitialized)
            {
                int validTrack = getValidMusicTrackNumber(currentSettings.music.track);
                setMusicVolume(currentSettings.music.volume);
                playMusicTrack(validTrack);
                Serial.printf("🎵 Music ON: Playing track %d\n", validTrack);
            }
            else if (!isAlarmPlaying)
            {
                stopMusic();
                Serial.println("🎵 Music OFF: Stopped");
            }
            changed = true;
        }
        else if (command == "music-track")
        {
            int requestedTrack = doc["value"];
            int validTrack = getValidMusicTrackNumber(requestedTrack);
            currentSettings.music.track = validTrack;

            if (currentSettings.music.on && dfPlayerInitialized)
            {
                playMusicTrack(validTrack);
                Serial.printf("🎵 Music track changed to: %d\n", validTrack);
            }
            changed = true;
        }
        else if (command == "music-volume")
        {
            int frontendVolume = doc["value"];
            frontendVolume = (frontendVolume / 10) * 10;
            int dfPlayerVolume = map(frontendVolume, 0, 100, 0, 30);
            currentSettings.music.volume = dfPlayerVolume;

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
        Serial.printf("Perintah '%s' diabaikan, timer belum dikonfirmasi.\n", command.c_str());
    }

    if (changed)
    {
        Serial.printf("Perintah '%s' diterima dan diproses.\n", command.c_str());
        saveSettings();
        checkAndApplySchedules();
        notifyClients();
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.printf("WebSocket klien #%u terhubung\n", client->id());
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.printf("WebSocket klien #%u terputus\n", client->id());
    }
    else if (type == WS_EVT_DATA)
    {
        handleWebSocketMessage(arg, data, len);
    }
}

void notifyClients()
{
    JsonDocument doc;
    doc["type"] = "statusUpdate";
    doc["state"]["timer"]["on"] = currentSettings.timer.on;
    doc["state"]["timer"]["confirmed"] = currentSettings.timer.confirmed;
    char startTimeStr[6];
    char endTimeStr[6];
    sprintf(startTimeStr, "%02d:%02d", currentSettings.timer.startHour, currentSettings.timer.startMinute);
    sprintf(endTimeStr, "%02d:%02d", currentSettings.timer.endHour, currentSettings.timer.endMinute);
    doc["state"]["timer"]["start"] = startTimeStr;
    doc["state"]["timer"]["end"] = endTimeStr;
    doc["state"]["light"]["intensity"] = currentSettings.light.intensity;
    doc["state"]["aromatherapy"]["on"] = currentSettings.aromatherapy.on;
    doc["state"]["alarm"]["on"] = currentSettings.alarm.on;
    doc["state"]["music"]["on"] = currentSettings.music.on;
    doc["state"]["music"]["track"] = currentSettings.music.track;
    doc["state"]["music"]["volume"] = map(currentSettings.music.volume, 0, 30, 0, 100);
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

// =================================================================
// SETUP & LOOP (unchanged)
// =================================================================
void setup()
{
    Serial.begin(115200);
    Wire.begin();

    pinMode(AROMATHERAPY_PIN, OUTPUT);
    digitalWrite(AROMATHERAPY_PIN, LOW);
    ledcSetup(PWM_CHANNEL_WHITE, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(PWM_CHANNEL_YELLOW, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(WHITE_LED_PIN, PWM_CHANNEL_WHITE);
    ledcAttachPin(YELLOW_LED_PIN, PWM_CHANNEL_YELLOW);

    if (!rtc.refresh())
    {
        Serial.println("KRITIS: RTC Gagal!");
    }
    checkAndSetRTC();

    loadSettings();
    initializeDFPlayer();

    if (!SPIFFS.begin(true))
    {
        Serial.println("KRITIS: Gagal me-mount SPIFFS!");
        return;
    }

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi terhubung! IP: " + WiFi.localIP().toString());

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html"); });
    server.serveStatic("/", SPIFFS, "/");
    server.begin();

    Serial.println("=== SWELL SMART LAMP READY ===");
    Serial.printf("📻 Relax Music: %d tracks available\n", RELAX_PLAYLIST_SIZE);
    Serial.printf("🔔 Alarm Track: Fixed to #%d\n", ALARM_TRACK_NUMBER);
    Serial.printf("🎵 DFPlayer Status: %s\n", dfPlayerInitialized ? "OK" : "ERROR");
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

    delay(10);
}