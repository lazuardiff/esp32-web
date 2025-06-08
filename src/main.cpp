/**
 * @file main.cpp
 * @brief Swell Smart Lamp - Firmware ESP32 (Standalone & Scheduled Workflow)
 * @details
 * - FIXED VERSION: Error syntax dan struktur diperbaiki
 * - Algoritma aromatherapy yang disempurnakan
 * - Volume Alarm diatur ke maksimal, volume musik dikontrol dari web
 * - Logika tidak lagi bergantung pada input Serial, sepenuhnya dikontrol via WebSocket
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
const int DFPLAYER_RX_PIN = 16;
const int DFPLAYER_TX_PIN = 17;

// --- Konfigurasi PWM ---
const int PWM_CHANNEL_WHITE = 0;
const int PWM_CHANNEL_YELLOW = 1;
const int PWM_FREQUENCY = 100;
const int PWM_RESOLUTION = 8;

// =================================================================
// OBJEK & VARIABEL GLOBAL
// =================================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
uRTCLib rtc(0x68);
HardwareSerial myDFPlayerSerial(2);
DFRobotDFPlayerMini myDFPlayer;

// --- Variabel untuk manajemen waktu non-blocking ---
unsigned long lastAromatherapyCheck = 0;
unsigned long aromatherapyOnTime = 0;
bool isAromatherapySpraying = false;
unsigned long lastTimeCheck = 0;
unsigned long alarmStopTime = 0;
bool isAlarmPlaying = false;
unsigned long lastAromatherapySprayStart = 0; // Kapan semprotan terakhir dimulai
bool aromatherapyScheduleActive = false;      // Flag untuk tracking jadwal aktif
unsigned long lastStatusBroadcast = 0;
const unsigned long STATUS_BROADCAST_INTERVAL = 60000; // 60 detik
bool previousMusicState = false;

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
        int intensity = 50; // 0-100
    } light;

    struct Aromatherapy
    {
        bool on = false;
    } aromatherapy;

    struct Alarm
    {
        bool on = false;
        const int track = 5;
    } alarm;

    struct Music
    {
        bool on = false;
        int track = 1;
        int volume = 50; // 0-100
    } music;
};
Settings currentSettings;

// =================================================================
// FUNGSI & MAKRO WAKTU KOMPILASI (FIXED)
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
// ⭐ FIXED: Overflow issue dengan year
#define __TIME_YEARS__ (2000 + CONV_STR2DEC_2(__DATE__, 9))

// =================================================================
// DEKLARASI FUNGSI (FIXED: untuk mengatasi error undefined)
// =================================================================
void saveSettings();
void loadSettings();
void checkAndSetRTC();
void generateAndSendPlaylist();
void checkAndApplySchedules();
void checkAromatherapyLogic(int currentTimeInMinutes, int startTimeInMinutes);
void resetAromatherapy();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void notifyClients();

// =================================================================
// FUNGSI INTI
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

    // ⭐ FIXED: Casting untuk menghindari overflow warning
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

void generateAndSendPlaylist()
{
    int totalFiles = myDFPlayer.readFileCounts();
    if (totalFiles < 0)
    {
        Serial.println("Gagal membaca jumlah file dari SD Card.");
        totalFiles = 5;
    }
    Serial.printf("Terdeteksi %d file musik di SD Card.\n", totalFiles);
    JsonDocument playlistDoc;
    playlistDoc["type"] = "playlist";
    JsonArray playlistArray = playlistDoc["playlist"].to<JsonArray>();
    for (int i = 1; i <= totalFiles; i++)
    {
        JsonObject trackObject = playlistArray.add<JsonObject>();
        trackObject["trackNumber"] = i;
        char title[30];
        sprintf(title, "Track %02d", i);
        trackObject["title"] = title;
    }
    String response;
    serializeJson(playlistDoc, response);
    ws.textAll(response);
}

void checkAndApplySchedules()
{
    if (!currentSettings.timer.confirmed)
    {
        ledcWrite(PWM_CHANNEL_WHITE, 0);
        ledcWrite(PWM_CHANNEL_YELLOW, 0);
        // Reset aromatherapy jika timer tidak confirmed
        if (digitalRead(AROMATHERAPY_PIN) == LOW)
        {
            digitalWrite(AROMATHERAPY_PIN, HIGH);
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

    // Light control (sama seperti sebelumnya)
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

    // ⭐ STORE: Previous state untuk detection
    bool aromaStateBefore = currentSettings.aromatherapy.on;

    // Aromatherapy logic (sama seperti sebelumnya)
    checkAromatherapyLogic(currentTimeInMinutes, startTimeInMinutes);

    // ⭐ DETECT: Aromatherapy auto-disabled dan notify frontend
    if (aromaStateBefore && !currentSettings.aromatherapy.on)
    {
        Serial.println("🔔 NOTIFICATION: Aromatherapy auto-disabled, notifying frontend...");
        saveSettings();  // Save state change
        notifyClients(); // Immediate update ke frontend
    }

    // ⭐ STORE: Previous music state
    previousMusicState = currentSettings.music.on;

    // Music logic (sama seperti sebelumnya)
    int musicEndTimeInMinutes = (startTimeInMinutes + 60) % 1440;
    bool inMusicTime = (startTimeInMinutes + 60 > 1440) ? (currentTimeInMinutes >= startTimeInMinutes || currentTimeInMinutes < musicEndTimeInMinutes) : (currentTimeInMinutes >= startTimeInMinutes && currentTimeInMinutes < musicEndTimeInMinutes);

    if (currentSettings.music.on && !inMusicTime && !isAlarmPlaying)
    {
        myDFPlayer.stop();
        Serial.println("🎵 Relax Music: Waktu habis.");
        currentSettings.music.on = false;
        saveSettings();

        // ⭐ IMMEDIATE: Notify frontend about music auto-stop
        Serial.println("🔔 NOTIFICATION: Music auto-stopped, notifying frontend...");
        notifyClients(); // Immediate update ke frontend
    }

    // Alarm logic (sama seperti sebelumnya)
    if (currentSettings.alarm.on && !isAlarmPlaying && !currentSettings.music.on)
    {
        if (rtc.hour() == currentSettings.timer.endHour && rtc.minute() == currentSettings.timer.endMinute)
        {
            Serial.println("🔔 ALARM: Waktunya bangun!");
            myDFPlayer.volume(30);
            myDFPlayer.play(currentSettings.alarm.track);
            isAlarmPlaying = true;
            alarmStopTime = millis() + 300000;
        }
    }
    if (isAlarmPlaying && millis() >= alarmStopTime)
    {
        Serial.println("🔔 ALARM: Durasi selesai.");
        myDFPlayer.stop();
        isAlarmPlaying = false;
        if (currentSettings.music.on)
        {
            myDFPlayer.volume(map(currentSettings.music.volume, 0, 100, 0, 30));
            myDFPlayer.play(currentSettings.music.track);
        }
    }
}

/**
 * ⭐ FUNGSI AROMATHERAPY YANG DIPERBAIKI
 * Requirement:
 * 1. Aktif hanya jika toggle ON
 * 2. Aktif hanya di jam pertama timer
 * 3. Semprot 5 detik setiap 5 menit
 * 4. Menggunakan relay (HIGH/LOW)
 */
void checkAromatherapyLogic(int currentTimeInMinutes, int startTimeInMinutes)
{
    // ===== CEK APAKAH AROMATHERAPY HARUS AKTIF =====
    if (!currentSettings.aromatherapy.on)
    {
        if (digitalRead(AROMATHERAPY_PIN) == LOW)
        {
            digitalWrite(AROMATHERAPY_PIN, HIGH);
            isAromatherapySpraying = false;
            aromatherapyScheduleActive = false;
            Serial.println("💨 Aromatherapy: Dimatikan (toggle OFF)");
        }
        return;
    }

    // ===== CEK APAKAH MASIH DALAM JAM PERTAMA =====
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
        // ⭐ AUTO-DISABLE: Set state dan akan di-detect di checkAndApplySchedules()
        if (digitalRead(AROMATHERAPY_PIN) == LOW)
        {
            digitalWrite(AROMATHERAPY_PIN, HIGH);
            isAromatherapySpraying = false;
            Serial.println("💨 Aromatherapy: Jam pertama selesai, dimatikan");
        }
        aromatherapyScheduleActive = false;

        // ⭐ IMPORTANT: Auto-disable aromatherapy state
        if (currentSettings.aromatherapy.on)
        {
            currentSettings.aromatherapy.on = false; // State change akan di-detect
            Serial.println("💨 Aromatherapy: Auto-disabled setelah 1 jam");
        }
        return;
    }

    // Rest of aromatherapy logic sama seperti sebelumnya...
    aromatherapyScheduleActive = true;
    unsigned long currentMillis = millis();

    if (isAromatherapySpraying)
    {
        if (currentMillis - aromatherapyOnTime >= 5000)
        {
            digitalWrite(AROMATHERAPY_PIN, HIGH);
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
        Serial.printf("💨 Aromatherapy: 5 menit berlalu (%.1f menit), semprot lagi\n",
                      timeSinceLastSpray / 60000.0);
    }

    if (timeToSpray)
    {
        digitalWrite(AROMATHERAPY_PIN, LOW);
        isAromatherapySpraying = true;
        aromatherapyOnTime = currentMillis;
        Serial.printf("💨 Aromatherapy: Mulai semprot (menit ke-%d dari timer)\n", timerElapsedMinutes);
    }
}

/**
 * ⭐ FUNGSI UNTUK RESET AROMATHERAPY (dipanggil saat timer OFF)
 */
void resetAromatherapy()
{
    if (digitalRead(AROMATHERAPY_PIN) == LOW)
    {
        digitalWrite(AROMATHERAPY_PIN, HIGH);
    }
    isAromatherapySpraying = false;
    aromatherapyScheduleActive = false;
    lastAromatherapySprayStart = 0; // Reset counter
    Serial.println("💨 Aromatherapy: Reset semua state");
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    JsonDocument doc;
    deserializeJson(doc, (char *)data);
    String command = doc["command"];
    Serial.printf("📨 Command diterima: %s\n", command.c_str());
    Serial.printf("⏰ Timer ON: %s, Confirmed: %s\n",
                  currentSettings.timer.on ? "YES" : "NO",
                  currentSettings.timer.confirmed ? "YES" : "NO");
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
            Serial.printf("Light intensity: %d%%\n", currentSettings.light.intensity);
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
            changed = true;
        }
        else if (command == "music-toggle")
        {
            currentSettings.music.on = doc["value"];
            if (currentSettings.music.on)
            {
                myDFPlayer.volume(map(currentSettings.music.volume, 0, 100, 0, 30));
                myDFPlayer.play(currentSettings.music.track);
            }
            else if (!isAlarmPlaying)
            {
                myDFPlayer.stop();
            }
            changed = true;
        }
        else if (command == "music-track")
        {
            currentSettings.music.track = doc["value"];
            if (currentSettings.music.on)
                myDFPlayer.play(currentSettings.music.track);
            changed = true;
        }
        else if (command == "music-volume")
        {
            currentSettings.music.volume = doc["value"];
            myDFPlayer.volume(map(currentSettings.music.volume, 0, 100, 0, 30));
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
    doc["state"]["music"]["volume"] = currentSettings.music.volume;
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

// =================================================================
// FUNGSI SETUP & LOOP
// =================================================================
void setup()
{
    Serial.begin(115200);
    Wire.begin();
    pinMode(AROMATHERAPY_PIN, OUTPUT);
    digitalWrite(AROMATHERAPY_PIN, HIGH);
    ledcSetup(PWM_CHANNEL_WHITE, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcSetup(PWM_CHANNEL_YELLOW, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(WHITE_LED_PIN, PWM_CHANNEL_WHITE);
    ledcAttachPin(YELLOW_LED_PIN, PWM_CHANNEL_YELLOW);

    if (!rtc.refresh())
    {
        Serial.println("KRITIS: RTC Gagal!");
    }
    checkAndSetRTC();

    // Inisialisasi DFPlayer yang disempurnakan
    myDFPlayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
    Serial.println("Menginisialisasi DFPlayer Mini...");
    if (!myDFPlayer.begin(myDFPlayerSerial, true, false))
    {
        Serial.println("KRITIS: DFPlayer Mini Gagal! Cek koneksi atau SD Card.");
    }
    else
    {
        Serial.println("OK: DFPlayer Mini terdeteksi.");
        myDFPlayer.setTimeOut(500);
        myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
    }

    loadSettings();

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
    Serial.println("\nTerhubung! IP: " + WiFi.localIP().toString());

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html"); });
    server.serveStatic("/", SPIFFS, "/");
    server.begin();
}

// ⭐ TAMBAH: Periodic status broadcast di loop()
void loop()
{
    ws.cleanupClients();

    // Check schedules every second
    if (millis() - lastTimeCheck >= 1000)
    {
        checkAndApplySchedules();
        lastTimeCheck = millis();
    }

    // ⭐ NEW: Periodic status broadcast every 60 seconds
    if (millis() - lastStatusBroadcast >= STATUS_BROADCAST_INTERVAL)
    {
        Serial.println("📡 Periodic status broadcast to frontend...");
        notifyClients(); // Kirim status update berkala
        lastStatusBroadcast = millis();
    }

    // Music next track logic
    if (currentSettings.music.on && myDFPlayer.available())
    {
        if (myDFPlayer.readType() == DFPlayerPlayFinished)
        {
            myDFPlayer.next();
        }
    }

    delay(10);
}