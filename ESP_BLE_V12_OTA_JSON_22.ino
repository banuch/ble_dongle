#include <dummy.h>

//dev on 26-09-2024 OTA/14 DIGIT HPL/13DIGIT HPLWITH END CHARACTOR,sleepmodes,irda enable,LAN 3PH
//OTA rewritten to GitHub-based version-checked update (HTTPClient+Update, ref: github_ota)
#include <Preferences.h>
#include "BluetoothSerial.h"
#include <HardwareSerial.h>
#include <WiFiMulti.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

#define irdaen 12
#define extsw 33
#define extpw 32
#define MAX_BT_NAME_LENGTH 20

#define RXD2 16
#define TXD2 17
#define RXD1 18
#define TXD1 19

char bluetooth_name[MAX_BT_NAME_LENGTH];
Preferences preferences;

String readString;
String readpacket;
String rs = "";
String sno = "";
String mfid, kwh, kvah, rmd, data6m;
uint32_t y, yb;
float yx = 0;
long stime = 0;

uint8_t a, a1, a2;

const int BUFFER_SIZE = 50;
char buf[BUFFER_SIZE];
int batt_val = 0;
int meter_flag = 0;
int len = 0, i = 0, nbytes = 0, ndigits = 0;
const int ledPin = 23;
const int buz = 22;
const int buz1 = 19;
const int led = 2;
const int freq = 38000;
const int ledChannel = 0;
const int resolution = 8;

byte message1[] = { 0x95, 0x95, 0xFF, 0xFF, 0xFF, 0x0B, 0x96, 0x31, 0x11, 0x05, 0x00 };
byte message2[] = { 0x95, 0x95, 0xFF, 0xFF, 0xFF, 0x0B, 0x00, 0x31, 0x11, 0x05, 0x00 };
byte message5[] = { 0x95, 0x95, 0xFF, 0xFF, 0xFF, 0x0B, 0x01, 0x31, 0x11, 0x05, 0x00 };
byte message6[] = { 0x95, 0x95, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x96, 0x31, 0x11, 0x05, 0x00 };
byte message7[] = { 0x95, 0x95, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x00, 0x31, 0x11, 0x05, 0x00 };
byte message3[] = { 0xB9, 0x9E, 0x8E, 0x7E, 0x1E };
byte message4[] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

WiFiMulti WiFiMulti;

// ── OTA CONFIG ───────────────────────────────────────────────────────────────
const char* currentFirmwareVersion = "V12.21";
const char* versionUrl  = "https://raw.githubusercontent.com/banuch/gitbux_ota/refs/heads/master/frimware/version.txt";
const char* firmwareUrl = "https://github.com/banuch/gitbux_ota/releases/download/esp32_firmware/frimware.ino.bin";
// ─────────────────────────────────────────────────────────────────────────────

String ssid;
String password;
String ipaddress;
String port;
String blename = "PTA-DEFAULT";

HardwareSerial SerialPort2(2);
HardwareSerial SerialPort1(1);
BluetoothSerial SerialBT;

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif


// ═══════════════════════════════════════════════════════════════════════════
//  OTA FUNCTIONS  (GitHub-based, version-checked)
// ═══════════════════════════════════════════════════════════════════════════

String ota_fetchLatestVersion() {
  HTTPClient http;
  http.begin(versionUrl);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String ver = http.getString();
    ver.trim();
    http.end();
    return ver;
  } else {
    Serial.printf("[OTA] Version fetch failed. HTTP: %d\n", httpCode);
    http.end();
    return "";
  }
}

bool ota_startUpdate(WiFiClient* client, int contentLength) {
  Serial.println("[OTA] Initializing Update...");
  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
    SerialBT.println("OTA ERR: " + String(Update.errorString()));
    return false;
  }

  size_t written = 0;
  int lastProgress = -1;
  const unsigned long timeoutDuration = 120000UL;
  unsigned long lastDataTime = millis();

  while (written < (size_t)contentLength) {
    if (client->available()) {
      uint8_t buffer[128];
      size_t readLen = client->read(buffer, sizeof(buffer));
      if (readLen > 0) {
        Update.write(buffer, readLen);
        written += readLen;
        lastDataTime = millis();
        int progress = (written * 100) / contentLength;
        if (progress != lastProgress) {
          Serial.printf("[OTA] Progress: %d%%\n", progress);
          if (progress % 10 == 0) {
            SerialBT.println("OTA: " + String(progress) + "%");
          }
          lastProgress = progress;
        }
      }
    }
    if (millis() - lastDataTime > timeoutDuration) {
      Serial.println("[OTA] Timeout. Aborting.");
      SerialBT.println("OTA ERR: Timeout");
      Update.abort();
      return false;
    }
    yield();
  }

  if (written != (size_t)contentLength) {
    Serial.printf("[OTA] Write incomplete: %d / %d\n", written, contentLength);
    SerialBT.println("OTA ERR: Incomplete write");
    Update.abort();
    return false;
  }

  if (!Update.end()) {
    Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
    SerialBT.println("OTA ERR: " + String(Update.errorString()));
    return false;
  }

  return true;
}

void ota_downloadAndApply() {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // required: GitHub Releases redirects to S3
  http.begin(firmwareUrl);

  int httpCode = http.GET();
  Serial.printf("[OTA] Firmware GET: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("[OTA] Firmware size: %d bytes\n", contentLength);

    if (contentLength <= 0) {
      SerialBT.println("OTA ERR: Invalid size");
      http.end();
      return;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (ota_startUpdate(stream, contentLength)) {
      SerialBT.println("OTA OK! Restarting...");
      Serial.println("[OTA] Success. Restarting.");
      http.end();
      delay(2000);
      ESP.restart();
    } else {
      SerialBT.println("OTA FAILED");
    }
  } else {
    Serial.printf("[OTA] Firmware download failed. HTTP: %d\n", httpCode);
    SerialBT.println("OTA ERR: Download failed HTTP " + String(httpCode));
  }
  http.end();
}

void update_firmware() {
  char SSID[32];
  char PASSWORD[32];
  ssid.toCharArray(SSID, 32);
  password.toCharArray(PASSWORD, 32);

  SerialBT.println("OTA: Connecting WiFi...");
  Serial.println("[OTA] Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(SSID, PASSWORD);

  unsigned long wifiStart = millis();
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - wifiStart > 15000UL) {
      SerialBT.println("OTA ERR: WiFi timeout");
      Serial.println("\n[OTA] WiFi timeout");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return;
    }
  }
  Serial.println("\n[OTA] WiFi OK: " + WiFi.localIP().toString());
  SerialBT.println("OTA: WiFi OK");

  // Step 1 - version check
  SerialBT.println("OTA: Checking version...");
  String latestVersion = ota_fetchLatestVersion();

  if (latestVersion == "") {
    SerialBT.println("OTA ERR: Version check failed");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  SerialBT.println("OTA CUR: " + String(currentFirmwareVersion));
  SerialBT.println("OTA NEW: " + latestVersion);

  if (latestVersion == String(currentFirmwareVersion)) {
    SerialBT.println("OTA: Already up to date");
    Serial.println("[OTA] Already up to date.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  // Step 2 - download and flash
  SerialBT.println("OTA: Starting update...");
  ota_downloadAndApply();

  // Step 3 - cleanup (only reached if update failed; success does ESP.restart())
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}


// ═══════════════════════════════════════════════════════════════════════════
//  NVS / CONFIG FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void ReadAllValues() {
  preferences.begin("credentials", false);

  blename = preferences.getString("blename", "");
  if (blename.length() == 0) blename = "PTA-DEFAULT";

  ssid = preferences.getString("ssid", "");
  if (ssid.length() == 0) ssid = "Default-WIFI";

  password = preferences.getString("password", "");
  if (password.length() == 0) password = "password";

  ipaddress = preferences.getString("ipaddress", "");
  if (ipaddress.length() == 0) ipaddress = "122.169.206.214";

  port = preferences.getString("port", "");
  if (port.length() == 0) port = "3000";

  Serial.println("blename: " + blename);
  Serial.println("ssid: " + ssid);
  Serial.println("ipaddress: " + ipaddress);
  Serial.println("port: " + port);
  Serial.println("password: " + password);

  SerialBT.println("blename: " + blename);
  SerialBT.println("ssid: " + ssid);
  SerialBT.println("ipaddress: " + ipaddress);
  SerialBT.println("port: " + port);
  SerialBT.println("password: " + password);

  preferences.end();
}

void update_bname() {
  Serial.println("Request Receved");
  String ble_name = readString.substring(14, readString.length());
  preferences.begin("credentials", false);
  preferences.putString("blename", ble_name);
  preferences.end();
}

void update_ssid() {
  Serial.println("Request Receved");
  String ssid = readString.substring(13, readString.length());
  preferences.begin("credentials", false);
  preferences.putString("ssid", ssid);
  preferences.end();
}

void update_password() {
  Serial.println("Request Receved");
  String password = readString.substring(17, readString.length());
  preferences.begin("credentials", false);
  preferences.putString("password", password);
  preferences.end();
}

void update_ipaddress() {
  Serial.println("Request Receved");
  String ipaddress = readString.substring(18, readString.length());
  preferences.begin("credentials", false);
  preferences.putString("ipaddress", ipaddress);
  preferences.end();
}

void update_port() {
  Serial.println("Request Receved");
  String port = readString.substring(13, readString.length());
  preferences.begin("credentials", false);
  preferences.putString("port", port);
  preferences.end();
}


// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  pinMode(led, OUTPUT);
  pinMode(buz, OUTPUT);
  pinMode(buz1, OUTPUT);

  Serial.begin(115200);
  Serial.println("Getting Values.....");
  ReadAllValues();
  delay(500);

  SerialBT.begin(blename);
  SerialPort2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  SerialPort1.begin(2400, SERIAL_8N1, RXD1, TXD1);
  Serial.setTimeout(100);
  SerialPort1.setTimeout(2000);
  SerialBT.setTimeout(100);

  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 10));
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 9));

  // ESP32 Arduino core 3.x: ledcSetup+ledcAttachPin replaced by single ledcAttach call
  ledcAttach(ledPin, freq, resolution);
  ledcWrite(ledChannel, 85);

  pinMode(irdaen, OUTPUT);
  pinMode(extpw, OUTPUT);
  pinMode(extsw, INPUT);
  digitalWrite(extpw, 1);
  digitalWrite(irdaen, 1);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);

  delay(5);
  ledon();
  beep2();
  ledoff();
  IRDA2400();
  delay(1000);
  IRDA9600();
  delay(100);
  digitalWrite(irdaen, 1);
}


// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  readString = " ";
  readString = "";
  while (SerialBT.available()) {
    char c = SerialBT.read();
    readString += c;
    stime = 0;
  }

  if (readString.length() > 0) {
    SerialBT.println(readString);

    if (readString.indexOf("update_bname") != -1)     { ledon(); beep1(); update_bname();     ledoff(); }
    if (readString.indexOf("update_ssid") != -1)      { ledon(); beep1(); update_ssid();      ledoff(); }
    if (readString.indexOf("update_password") != -1)  { ledon(); beep1(); update_password();  ledoff(); }
    if (readString.indexOf("update_ipaddress") != -1) { ledon(); beep1(); update_ipaddress(); ledoff(); }
    if (readString.indexOf("update_port") != -1)      { ledon(); beep1(); update_port();      ledoff(); }
    if (readString.indexOf("update_firmware") != -1)  { ledon(); beep1(); update_firmware();  ledoff(); }
    if (readString.indexOf("get_config") != -1)       { ledon(); beep1(); ReadAllValues();    ledoff(); }

    if (readString == "#IRDA1*") {
      ledon(); beep1(); meter_flag = 1;
      delay(2); delay(10); IRDA2400(); IRDA1_PHASE();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA1P*") {
      ledon(); beep1(); meter_flag = 2;
      IRDA2400(); IRDA1_PHASE_P();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA3*") {
      IRDA9600(); ledon(); beep1(); meter_flag = 3;
      IRDA3_PHASE();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA3P*") {
      IRDA9600(); ledon(); beep1(); meter_flag = 4;
      IRDA3_PHASE_P();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA3P14HP*") {
      IRDA9600(); ledon(); beep1(); meter_flag = 4; ndigits = 8;
      IRDA3_PHASE_P14HP();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA3P13HP*") {
      IRDA9600(); ledon(); beep1(); meter_flag = 4; ndigits = 7;
      IRDA3_PHASE_P14HP();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA3SR*") {
      IRDA9600(); ledon(); beep1(); meter_flag = 5;
      IRDA3_PHASE();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRDA3SP*") {
      IRDA9600(); ledon(); beep1(); meter_flag = 6;
      IRDA3_PHASE_P();
      ledoff(); beep2(); digitalWrite(irdaen, 1);
    }
    if (readString == "#IRIR1*") {
      SerialPort1.begin(2400, SERIAL_8N1, RXD1, TXD1);
      delay(1000); ledon(); beep1(); meter_flag = 7; delay(10);
      IRIR1_PHASE(); ledoff(); beep2();
    }
    if (readString == "#IRIR1P*") {
      SerialPort1.begin(2400, SERIAL_8N1, RXD1, TXD1);
      delay(1000); ledon(); beep1(); meter_flag = 8; delay(10);
      IRIR1_PHASE_P(); ledoff(); beep2();
    }
    if (readString == "#IRIR3*") {
      SerialPort1.begin(2400, SERIAL_8N1, RXD1, TXD1);
      delay(1000); ledon(); beep1(); meter_flag = 9; delay(10);
      IRIR3_PHASE(); ledoff(); beep2();
    }
    if (readString == "#IRIR3P*") {
      SerialPort1.begin(2400, SERIAL_8N1, RXD1, TXD1);
      delay(1000); ledon(); beep1(); meter_flag = 10; delay(10);
      IRIR3_PHASE_P(); ledoff(); beep2();
    }
    if (readString == "#BATTV*") {
      ledon(); beep1(); meter_flag = 5;
      batt_val = 0;
      for (i = 0; i < 11; i++) { batt_val += analogRead(34); delay(20); }
      batt_val = batt_val / 10;
      batt_val = (batt_val - 2000) / 4;
      if (batt_val < 0)   batt_val = 0;
      if (batt_val > 100) batt_val = 100;
      SerialBT.print("BATTERY CHARGE: ");
      SerialBT.print(batt_val);
      SerialBT.println(" %");
      ledoff(); beep2();
    }
    if (readString == "#VER*") {
      ledon(); beep1();
      SerialBT.println("VER:" + String(currentFirmwareVersion));
      ledoff(); beep2();
    }
    if (readString == "   ") {
      ledon(); SerialBT.println("PT-OK "); ledoff();
    }
  }

  // ── SLEEP ──────────────────────────────────────────────────────────────
  if (digitalRead(extsw) == 0) {
    delay(2000);
    if (digitalRead(extsw) == 0) {
      beep1(); beep1(); beep1(); beep1(); beep1();
      digitalWrite(extpw, 0);
      while (digitalRead(extsw) == 0) {}
      digitalWrite(extpw, 0);
      esp_deep_sleep_start();
      delay(1000);
      beep1(); delay(300); beep1(); delay(300);
      beep1(); delay(300); beep1(); delay(300);
    }
  }

  stime = stime + 1;
  delay(1);
  if (stime > 210000) {
    beep1(); beep1(); beep1(); beep1();
    digitalWrite(extpw, 0);
    delay(5);
    esp_deep_sleep_start();
    delay(1000);
    beep1(); delay(300); beep1(); delay(300);
    beep1(); delay(300); beep1(); delay(300);
  }

  readString = "";
}


// ═══════════════════════════════════════════════════════════════════════════
//  SUB-PROGRAMS
// ═══════════════════════════════════════════════════════════════════════════

void BATT_STATUS() {
  batt_val = 0;
  for (i = 0; i < 11; i++) { batt_val += analogRead(34); delay(2); }
  batt_val = batt_val / 10;
  batt_val = (batt_val - 2000) / 4;
  if (batt_val < 0)   batt_val = 0;
  if (batt_val > 100) batt_val = 100;
  SerialBT.print("BATTERY CHARGE: ");
  SerialBT.print(batt_val);
  SerialBT.println(" %");
  SerialBT.print("VERSION: ");
  SerialBT.println(currentFirmwareVersion);
}

void IRDA2400() {
  digitalWrite(irdaen, 1);
  SerialPort2.begin(2400);
  delay(50);
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 10));
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 9));
  delay(500);
  SerialPort2.flush();
  delay(50);
  SerialPort2.println();
  delay(200);
  digitalWrite(irdaen, 0);
}

void IRDA9600() {
  digitalWrite(irdaen, 1);
  SerialPort2.begin(9600);
  delay(50);
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 10));
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 9));
  delay(500);
  SerialPort2.flush();
  delay(50);
  SerialPort2.println();
  delay(100);
  digitalWrite(irdaen, 0);
}

void beep1() {
  for (i = 1; i < 500; i++) {
    digitalWrite(buz, 1); digitalWrite(buz, 1); delayMicroseconds(175);
    digitalWrite(buz, 0); digitalWrite(buz, 0); delayMicroseconds(175);
  }
}
void beep2() { beep1(); delay(100); beep1(); }
void xbeep1() { digitalWrite(buz, 1); delay(100); digitalWrite(buz, 0); }
void xbeep2() {
  digitalWrite(buz, 1); delay(100); digitalWrite(buz, 0);
  delay(50);
  digitalWrite(buz, 1); delay(100); digitalWrite(buz, 0);
}
void ledon()  { digitalWrite(led, 1); delay(2); }
void ledoff() { digitalWrite(led, 0); delay(2); }


// ═══════════════════════════════════════════════════════════════════════════
//  IRDA 1 PH RAW
// ═══════════════════════════════════════════════════════════════════════════

void IRDA1_PHASE() {
  readString = " "; readString = "";
  SerialPort2.println(":00413BC4"); delay(200); PACKET_READ();
  SerialPort2.println(":00423AC5"); delay(200); PACKET_READ();
  SerialPort2.println(":004339C6"); delay(200); PACKET_READ();
  SerialPort2.println(":004537C8"); delay(200); PACKET_READ();
  SerialPort2.println(":004636C9"); delay(200); PACKET_READ();
  SerialBT.println(readString);
  BATT_STATUS();
  SerialBT.println("DATA RECEIVED: IRDA-1Ph-RAW.");
  SerialBT.print((char)254); delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IRDA 1PH PARSED
// ═══════════════════════════════════════════════════════════════════════════

void IRDA1_PHASE_P() {
  readString = "";
  SerialPort2.println(":00413BC4"); delay(200); readpacket = ""; PACKET_READ();
  rs = readString; sno = "S.NO:";
  readString.remove(0, 16); len = readString.length(); readString.remove(len - 6, 6);
  sno += readString; SerialBT.println(sno);

  readString = "";
  SerialPort2.println(":00423AC5"); delay(800); readpacket = ""; PACKET_READ();
  mfid = "M.ID:";
  readString.remove(0, 16); len = readString.length(); readString.remove(len - 6, 6);
  mfid += readString; SerialBT.println(mfid);

  readString = "";
  SerialPort2.println(":004339C6"); delay(200); readpacket = ""; PACKET_READ();
  kwh = "KWH:";
  readString.remove(0, 16); len = readString.length(); readString.remove(len - 6, 6);
  kwh += readString; SerialBT.println(kwh);

  readString = "";
  SerialPort2.println(":004636C9"); delay(200); readpacket = ""; PACKET_READ();
  rmd = "RMD:";
  readString.remove(0, 16); len = readString.length(); readString.remove(5);
  rmd += readString; SerialBT.println(rmd);

  BATT_STATUS();
  SerialBT.println("DATA RECEIVED: IRDA-1Ph-PARSED.");
  SerialBT.print((char)254); delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IRDA 3 PH RAW
// ═══════════════════════════════════════════════════════════════════════════

void IRDA3_PHASE() {
  rs = "";
  SerialPort2.write(message1, sizeof(message1));
  int len = rs.length(); rs.remove(0, len); rs = "";
  nbytes = 30; PACKET_READ3X();
  delay(50);
  message2[2] = rs[18]; message2[3] = rs[19]; message2[4] = rs[20];
  SerialPort2.write(message2, sizeof(message2));
  readString = ""; nbytes = 79; PACKET_READ3();
  SerialBT.println(readString); delay(10);

  if (meter_flag == 3) {
    BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-3Ph-RAW.");
    SerialBT.print((char)254); delay(10);
  }
  if (meter_flag == 5) {
    SerialBT.println("                 ");
    SerialBT.println("** EXPORT DATA **");
    message2[6] = 0x01;
    SerialPort2.write(message2, sizeof(message2));
    readString = ""; nbytes = 79; PACKET_READ3();
    SerialBT.println(readString);
    BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-3Ph-SOLAR-RAW.");
    SerialBT.print((char)254); delay(10);
    message2[6] = 0x00;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  IRDA 3 PH PARSED (HPL 14/13 digit)
// ═══════════════════════════════════════════════════════════════════════════

void IRDA3_PHASE_P14HP() {
  beep1();
  rs = "";
  SerialPort2.write(message6, sizeof(message6));
  len = rs.length(); rs.remove(0, len); rs = "";
  delay(1); nbytes = 45; PACKET_READ3X();
  len = rs.length(); delay(1500);
  message7[2]=rs[32]; message7[3]=rs[33]; message7[4]=rs[34]; message7[5]=rs[35];
  message7[6]=rs[36]; message7[7]=rs[37]; message7[8]=rs[38]; message7[9]=rs[39];
  SerialPort2.write(message7, sizeof(message7));
  PACKET_READ_PARSEY();
  if (meter_flag == 4) {
    BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-3Ph-PARSED.");
    SerialBT.print((char)254); delay(10);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  IRDA 3 PH PARSED
// ═══════════════════════════════════════════════════════════════════════════

void IRDA3_PHASE_P() {
  rs = "";
  SerialPort2.write(message1, sizeof(message1));
  len = rs.length(); rs.remove(0, len); rs = "";
  delay(1); nbytes = 30; PACKET_READ3X();
  len = rs.length();

  if (len >= 30) {
    delay(1500);
    message2[2]=rs[22]; message2[3]=rs[23]; message2[4]=rs[24];
    SerialPort2.write(message2, sizeof(message2));
    PACKET_READ_PARSE();
  } else {
    beep1();
    rs = "";
    SerialPort2.write(message6, sizeof(message6));
    len = rs.length(); rs.remove(0, len); rs = "";
    delay(1); nbytes = 45; PACKET_READ3X();
    len = rs.length(); delay(1500);
    message7[2]=rs[32]; message7[3]=rs[33]; message7[4]=rs[34]; message7[5]=rs[35];
    message7[6]=rs[36]; message7[7]=rs[37]; message7[8]=rs[38]; message7[9]=rs[39];
    SerialPort2.write(message7, sizeof(message7));
    ndigits = 8; PACKET_READ_PARSEY();
  }

  if (meter_flag == 4) {
    BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-3Ph-PARSED.");
    SerialBT.print((char)254); delay(10);
  }
  if (meter_flag == 6) {
    SerialBT.println("                 ");
    SerialBT.println("** EXPORT DATA **");
    message2[6] = 0x01;
    SerialPort2.write(message2, sizeof(message2));
    PACKET_READ_PARSE();
    BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-3Ph-SOLAR-PARSED.");
    SerialBT.print((char)254); delay(10);
    message2[6] = 0x00;
  }
}

void PACKET_READ_PARSEY() {
  readString = ""; nbytes = 71; PACKET_READ3();

  SerialBT.print("MF.ID:");
  SerialBT.print(readString[30]); SerialBT.print(readString[29]);
  a=readString[28]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1);
  a=readString[27]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1);
  message4[0]=readString[26]; message4[1]=readString[25]; message4[2]=readString[24]; message4[3]=readString[23];
  str_hexto_dec();
  int digits=0; yb=y;
  while (yb>0) { digits++; yb/=10; }
  for (i=0; i<ndigits-digits; i++) { SerialBT.print('0'); }
  SerialBT.println(y);

  SerialBT.print("TIME:");
  a=readString[31]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[32]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[33]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("DATE:");
  a=readString[34]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[35]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[36]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);

  message4[0]=0; message4[1]=0; message4[2]=readString[38]; message4[3]=readString[37];
  str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-R:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[40]; message4[3]=readString[39];
  str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-Y:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[42]; message4[3]=readString[41];
  str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-B:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[44]; message4[3]=readString[43];
  str_hexto_dec(); yx=y; SerialBT.print("CURRENT-R:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[46]; message4[3]=readString[45];
  str_hexto_dec(); yx=y; SerialBT.print("CURRENT-Y:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[48]; message4[3]=readString[47];
  str_hexto_dec(); yx=y; SerialBT.print("CURRENT-B:"); SerialBT.println(yx/100);
  message4[0]=readString[52]; message4[1]=readString[51]; message4[2]=readString[50]; message4[3]=readString[49];
  str_hexto_dec(); yx=y; SerialBT.print("KWh:"); SerialBT.println(yx/100);
  message4[0]=readString[56]; message4[1]=readString[55]; message4[2]=readString[54]; message4[3]=readString[53];
  str_hexto_dec(); yx=y; SerialBT.print("KVAh:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[58]; message4[3]=readString[57];
  str_hexto_dec(); yx=y; SerialBT.print("LBP1KvAMD:"); SerialBT.println(yx/100);

  SerialBT.print("MD TIME:");
  a=readString[59]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[60]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MD DATE:");
  a=readString[61]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[62]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[63]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MAKE:");
  SerialBT.print(readString[64]); SerialBT.print(readString[65]); SerialBT.println(readString[66]);
  SerialBT.print("Phase:"); SerialBT.println(readString[67], DEC);
  message4[0]=0; message4[1]=0; message4[2]=readString[69]; message4[3]=readString[68];
  str_hexto_dec(); yx=y; SerialBT.print("MUL.FACTOR:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[71]; message4[3]=readString[70];
  str_hexto_dec(); yx=y; SerialBT.print("MD RESET COUNT:"); SerialBT.println(yx);
}

void PACKET_READ_PARSE() {
  readString = ""; nbytes = 79; PACKET_READ3();

  message4[0]=0; message4[1]=readString[20]; message4[2]=readString[19]; message4[3]=readString[18];
  str_hexto_dec(); SerialBT.print("MF.ID:"); SerialBT.println(y, DEC);

  SerialBT.print("TIME:");
  a=readString[21]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[22]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[23]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("DATE:");
  a=readString[24]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[25]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[26]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);

  message4[0]=0; message4[1]=0; message4[2]=readString[28]; message4[3]=readString[27];
  str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-R:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[30]; message4[3]=readString[29];
  str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-Y:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[32]; message4[3]=readString[31];
  str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-B:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[34]; message4[3]=readString[33];
  str_hexto_dec(); yx=y; SerialBT.print("CURRENT-R:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[36]; message4[3]=readString[35];
  str_hexto_dec(); yx=y; SerialBT.print("CURRENT-Y:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[38]; message4[3]=readString[37];
  str_hexto_dec(); yx=y; SerialBT.print("CURRENT-B:"); SerialBT.println(yx/100);
  message4[0]=readString[46]; message4[1]=readString[45]; message4[2]=readString[44]; message4[3]=readString[43];
  str_hexto_dec(); yx=y; SerialBT.print("KWh:"); SerialBT.println(yx/100);
  message4[0]=readString[58]; message4[1]=readString[57]; message4[2]=readString[56]; message4[3]=readString[55];
  str_hexto_dec(); yx=y; SerialBT.print("KVAh:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[60]; message4[3]=readString[59];
  str_hexto_dec(); yx=y; SerialBT.print("LBP1KvAMD:"); SerialBT.println(yx/100);

  SerialBT.print("MD TIME:");
  a=readString[61]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[62]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MD DATE:");
  a=readString[63]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[64]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[65]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MAKE:");
  SerialBT.print(readString[66]); SerialBT.print(readString[67]); SerialBT.println(readString[68]);
  SerialBT.print("Phase:"); SerialBT.println(readString[69], DEC);
  message4[0]=0; message4[1]=0; message4[2]=readString[71]; message4[3]=readString[70];
  str_hexto_dec(); yx=y; SerialBT.print("MUL.FACTOR:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[89]; message4[3]=readString[88];
  str_hexto_dec(); yx=y; SerialBT.print("MD RESET COUNT:"); SerialBT.println(yx);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IR 1 PH RAW
// ═══════════════════════════════════════════════════════════════════════════

void IRIR1_PHASE() {
  readString = " "; readString = "";
  SerialPort1.println(":00413BC4"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":00423AC5"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004339C6"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004537C8"); delay(600); PACKETIR_READ1();
  SerialPort1.println(":004636C9"); delay(1000); PACKETIR_READ1();
  SerialBT.println(readString);
  BATT_STATUS();
  SerialBT.println("DATA RECEIVED: IR-1Ph-RAW.");
  SerialBT.print((char)254); delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IR 1 PHASE PARSED
// ═══════════════════════════════════════════════════════════════════════════

void IRIR1_PHASE_P() {
  readString = " "; readString = "";
  SerialPort1.println(":00413BC4"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":00423AC5"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004339C6"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004636C9"); delay(200); PACKETIR_READ1();

  rs = readString;
  sno="S.NO:"; readString.remove(0,16); len=readString.length(); readString.remove(8);
  sno+=readString; SerialBT.println(sno); delay(1);
  readString=rs; mfid="M.ID:"; readString.remove(0,46); len=readString.length(); readString.remove(16);
  mfid+=readString; SerialBT.println(mfid); delay(1);
  readString=rs; kwh="KWH:"; readString.remove(0,84); len=readString.length(); readString.remove(9);
  kwh+=readString; SerialBT.println(kwh); delay(1);
  readString=rs; rmd="RMD:"; readString.remove(0,115); len=readString.length(); readString.remove(5);
  rmd+=readString; SerialBT.println(rmd); delay(1);

  BATT_STATUS();
  SerialBT.println("DATA RECEIVED: IR-1Ph-PARSED.");
  SerialBT.print((char)254); delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IR 3PH RAW
// ═══════════════════════════════════════════════════════════════════════════

void IRIR3_PHASE() {
  readString=" "; readString="";
  SerialPort1.write(message3[0]); delay(2);
  SerialPort1.write(message3[1]); delay(2);
  SerialPort1.write(message3[2]); delay(2);
  SerialPort1.write(message3[3]); delay(2);
  SerialPort1.write(message3[4]); delay(2);
  delay(500);
  readString=" "; readString="";
  PACKETIR_READ3();
  rs=readString;
  SerialBT.println(readString);
  BATT_STATUS();
  SerialBT.println("DATA RECEIVED: IR-3Ph-RAW.");
  SerialBT.print((char)254); delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IR 3PH PARSED
// ═══════════════════════════════════════════════════════════════════════════

void IRIR3_PHASE_P() {
  readString=" "; readString="";
  SerialPort1.write(message3, sizeof(message3));
  readString=""; delay(200);
  IR3_PACKET_READ_PARSE();
  BATT_STATUS();
  SerialBT.println("DATA RECEIVED: IR-3Ph-PARSED.");
  SerialBT.print((char)254); delay(10);
}

void IR3_PACKET_READ_PARSE() {
  readString=" "; readString="";
  PACKETIR_READ3();

  message4[0]=readString[6]; message4[1]=readString[7]; message4[2]=readString[8]; message4[3]=readString[9];
  str_hexto_dec(); SerialBT.print("MF.ID:"); SerialBT.println(y, DEC);

  SerialBT.print("DATE:");
  a=readString[10]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[11]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[12]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("TIME:");
  a=readString[13]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[14]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);

  message4[0]=readString[15]; message4[1]=readString[16]; message4[2]=readString[17]; message4[3]=readString[18];
  str_hexto_dec(); yx=y; SerialBT.print("KWh:"); SerialBT.println(yx/1000);
  message4[0]=readString[19]; message4[1]=readString[20]; message4[2]=readString[21]; message4[3]=readString[22];
  str_hexto_dec(); yx=y; SerialBT.print("KVArh-Lag:"); SerialBT.println(yx/1000);
  message4[0]=readString[23]; message4[1]=readString[24]; message4[2]=readString[25]; message4[3]=readString[26];
  str_hexto_dec(); yx=y; SerialBT.print("KVArh-Lead:"); SerialBT.println(yx/1000);
  message4[0]=readString[27]; message4[1]=readString[28]; message4[2]=readString[29]; message4[3]=readString[30];
  str_hexto_dec(); yx=y; SerialBT.print("KVAh:"); SerialBT.println(yx/1000);
  message4[0]=0; message4[1]=0; message4[2]=0; message4[3]=readString[31];
  str_hexto_dec(); yx=y; SerialBT.print("Avg P.F:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[32]; message4[3]=readString[33];
  str_hexto_dec(); yx=y; SerialBT.print("MD-KWh:"); SerialBT.println(yx/1000);

  SerialBT.print("MD-DATE:");
  a=readString[34]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[35]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[36]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MD-TIME:");
  a=readString[37]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":");
  a=readString[38]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);

  message4[0]=0; message4[1]=0; message4[2]=readString[39]; message4[3]=readString[40];
  str_hexto_dec(); SerialBT.print("TAMPER COUNT:"); SerialBT.println(y);
  message4[0]=0; message4[1]=0; message4[2]=readString[41]; message4[3]=readString[42];
  str_hexto_dec(); SerialBT.print("TAMPER-STATUS:"); SerialBT.println(y);
}

// ═══════════════════════════════════════════════════════════════════════════
//  PACKET READERS
// ═══════════════════════════════════════════════════════════════════════════

void PACKET_READ() {
  readpacket=""; i=0; boolean at_flag=1;
  while (at_flag) {
    while (SerialPort2.available()) { delay(3); char c=SerialPort2.read(); readString+=c; readpacket+=c; }
    delay(1); i++;
    if ((readpacket.length()>26)||(i>2000)) at_flag=0;
  }
  delay(10);
}

void PACKET_READ3() {
  readpacket=""; i=0; boolean at_flag=1;
  while (at_flag) {
    while (SerialPort2.available()) { delay(3); char c=SerialPort2.read(); readString+=c; readpacket+=c; }
    delay(1); i++;
    if ((readpacket.length()>nbytes)||(i>9000)) at_flag=0;
  }
  delay(10);
}

void PACKET_READ3X() {
  readpacket=""; rs=""; i=0; boolean at_flag=1;
  while (at_flag) {
    while (SerialPort2.available()) { delay(3); char c=SerialPort2.read(); rs+=c; readpacket+=c; }
    delay(1); i++;
    if ((readpacket.length()>=nbytes)||(i>4000)) at_flag=0;
  }
  delay(10);
}

void PACKETIR_READ1() {
  delay(500); readpacket=""; i=0; boolean at_flag=1;
  while (at_flag) {
    while (SerialPort1.available()) { delay(3); char c=SerialPort1.read(); readString+=c; readpacket+=c; }
    delay(1); i++;
    if ((readpacket.length()>26)||(i>2000)) at_flag=0;
  }
  delay(10);
}

void PACKETIR_READ3() {
  delay(100); readpacket=""; i=0; boolean at_flag=1;
  while (at_flag) {
    while (SerialPort1.available()) { delay(3); char c=SerialPort1.read(); readString+=c; readpacket+=c; }
    delay(1); i++;
    if ((readpacket.length()>40)||(i>2000)) at_flag=0;
  }
  delay(10);
}

// ═══════════════════════════════════════════════════════════════════════════
//  UTILITY
// ═══════════════════════════════════════════════════════════════════════════

void str_hexto_dec() {
  uint32_t y1=(uint32_t)message4[0]<<24;
  uint32_t y2=(uint32_t)message4[1]<<16;
  uint32_t y3=(uint32_t)message4[2]<<8;
  uint32_t y4=(uint32_t)message4[3]<<0;
  y=y1+y2+y3+y4;
}

void bcd_charx() {
  a1=0; a2=0;
  a1=a&15; a2=a&240; a2=a2>>4;
}

void bcd_char() {
  a1=0; a2=0;
  for (i=0; i<a; i++) { a1++; if (a1==10) { a1=0; a2++; } }
}

void readBluetoothName() {
  preferences.getString("bluetooth_name", bluetooth_name, MAX_BT_NAME_LENGTH);
  if (strlen(bluetooth_name)==0) { strcpy(bluetooth_name,"PTA-DEFAULT"); saveBluetoothName(); }
}

void saveBluetoothName() {
  preferences.putString("bluetooth_name", bluetooth_name);
}

//*********************** END OF PROGRAM ****************************
