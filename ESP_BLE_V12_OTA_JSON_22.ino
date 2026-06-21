#include <dummy.h>

//dev on 26-09-2024 OTA/14 DIGIT HPL/13DIGIT HPLWITH END CHARACTOR,sleepmodes,irda enable,LAN 3PH
//updated: JSON BT protocol layer added with full backward compatibility (legacy raw strings still work)
//updated: API key security added — JSON commands require {"cmd":"...","key":"SECRET"} (V12.22-SEC)
#include <Preferences.h>
#include "BluetoothSerial.h"
#include <HardwareSerial.h>
#include <WiFiMulti.h>
#include <esp_sleep.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

#define irdaen 12
#define extsw 33
#define extpw 32
#define MAX_BT_NAME_LENGTH 20
#define FW_VERSION "V12.22"

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

const char* ssid1 = "Sahasra";
const char* password1 = "wintek@143";

String ssid;
String password;
String ipaddress;
String port;
String blename = "PTA-DEFAULT";

// ============================================================
//  PATCH 1: API KEY — global storage
//  Loaded from NVS key "apikey" in credentials namespace.
//  Empty string = key check DISABLED (safe default on first boot).
//  Once set via set_key command, all JSON commands must include
//  {"cmd":"...","key":"YOUR_SECRET"} or they get rejected.
// ============================================================
String apiKey = "";   // loaded from NVS at boot

// JSON mode flag: true = JSON protocol, false = legacy raw string protocol
bool jsonMode = false;

HardwareSerial SerialPort2(2);
HardwareSerial SerialPort1(1);
BluetoothSerial SerialBT;

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// ============================================================
//  JSON RESPONSE HELPERS
// ============================================================

void jsonReply(const char* cmd, const char* status, const char* msg) {
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["cmd"]    = cmd;
  doc["msg"]    = msg;
  doc["end"]    = true;
  String out;
  serializeJson(doc, out);
  SerialBT.println(out);
}

// Template accepts any StaticJsonDocument<N> size — fixes type mismatch compile errors
template <size_t N>
void jsonReplyWithDoc(StaticJsonDocument<N>& doc) {
  doc["end"] = true;
  String out;
  serializeJson(doc, out);
  SerialBT.println(out);
}

// Read battery percentage
// Uses GPIO34 (ADC1_CH6) — ADC1 is safe during active Bluetooth.
// ADC2 pins (e.g. GPIO15) return 0 when BT radio is active.
int readBattPct() {
  int val = 0;
  for (int k = 0; k < 11; k++) {
    val += analogRead(34);
    delay(2);
  }
  val = val / 10;
  val = (val - 2000) / 4;
  if (val < 0)   val = 0;
  if (val > 100) val = 100;
  return val;
}

// ============================================================
//  CONFIG FUNCTIONS
// ============================================================

void ReadAllValues() {
  preferences.begin("credentials", false);
  blename   = preferences.getString("blename",   "PTA-DEFAULT");
  ssid      = preferences.getString("ssid",      "Default-WIFI");
  password  = preferences.getString("password",  "password");
  ipaddress = preferences.getString("ipaddress", "122.169.206.214");
  port      = preferences.getString("port",      "3000");
  // PATCH 2: load apiKey from NVS (empty string = key check disabled)
  apiKey    = preferences.getString("apikey",    "");
  preferences.end();

  if (blename.length()   == 0) blename   = "PTA-DEFAULT";
  if (ssid.length()      == 0) ssid      = "Default-WIFI";
  if (password.length()  == 0) password  = "password";
  if (ipaddress.length() == 0) ipaddress = "122.169.206.214";
  if (port.length()      == 0) port      = "3000";
  // apiKey stays "" if not set — intentional, disables key check

  Serial.println("blename: "   + blename);
  Serial.println("ssid: "      + ssid);
  Serial.println("ipaddress: " + ipaddress);
  Serial.println("port: "      + port);
  Serial.println("password: "  + password);
  Serial.println("apikey set: " + String(apiKey.length() > 0 ? "YES" : "NO"));

  if (jsonMode) {
    StaticJsonDocument<512> doc;
    doc["status"] = "ok";
    doc["cmd"]    = "get_config";
    doc["msg"]    = "config ok";
    JsonObject data = doc.createNestedObject("data");
    data["blename"]    = blename;
    data["ssid"]       = ssid;
    data["ipaddress"]  = ipaddress;
    data["port"]       = port;
    data["password"]   = password;
    data["apikey_set"] = (apiKey.length() > 0);  // never expose the actual key
    jsonReplyWithDoc(doc);
  } else {
    SerialBT.println("blename: "   + blename);
    SerialBT.println("ssid: "      + ssid);
    SerialBT.println("ipaddress: " + ipaddress);
    SerialBT.println("port: "      + port);
    SerialBT.println("password: "  + password);
  }
}

void update_bname(String val = "") {
  String v = (val.length() > 0) ? val : readString.substring(14, readString.length());
  v.trim();
  preferences.begin("credentials", false);
  preferences.putString("blename", v);
  preferences.end();
  if (jsonMode) jsonReply("update_bname", "ok", "ble name updated");
}

void update_ssid(String val = "") {
  String v = (val.length() > 0) ? val : readString.substring(13, readString.length());
  v.trim();
  preferences.begin("credentials", false);
  preferences.putString("ssid", v);
  preferences.end();
  if (jsonMode) jsonReply("update_ssid", "ok", "ssid updated");
}

void update_password(String val = "") {
  String v = (val.length() > 0) ? val : readString.substring(17, readString.length());
  v.trim();
  preferences.begin("credentials", false);
  preferences.putString("password", v);
  preferences.end();
  if (jsonMode) jsonReply("update_password", "ok", "password updated");
}

void update_ipaddress(String val = "") {
  String v = (val.length() > 0) ? val : readString.substring(18, readString.length());
  v.trim();
  preferences.begin("credentials", false);
  preferences.putString("ipaddress", v);
  preferences.end();
  if (jsonMode) jsonReply("update_ipaddress", "ok", "ip address updated");
}

void update_port(String val = "") {
  String v = (val.length() > 0) ? val : readString.substring(13, readString.length());
  v.trim();
  preferences.begin("credentials", false);
  preferences.putString("port", v);
  preferences.end();
  if (jsonMode) jsonReply("update_port", "ok", "port updated");
}

// ============================================================
//  PATCH 3: API KEY MANAGEMENT FUNCTIONS
// ============================================================

// set_key — always allowed (no old key required), saves to NVS
// Send: {"cmd":"set_key","params":{"value":"YOUR_NEW_SECRET"}}
// To disable key check: {"cmd":"set_key","params":{"value":""}}
void update_apikey(String newKey) {
  newKey.trim();
  preferences.begin("credentials", false);
  preferences.putString("apikey", newKey);
  preferences.end();
  apiKey = newKey;
  if (newKey.length() > 0) {
    jsonReply("set_key", "ok", "api key set — all future json commands require key field");
  } else {
    jsonReply("set_key", "ok", "api key cleared — key check disabled");
  }
}

// get_key_status — returns whether a key is currently set (never returns the key itself)
void get_key_status() {
  StaticJsonDocument<256> doc;
  doc["status"] = "ok";
  doc["cmd"]    = "get_key_status";
  doc["msg"]    = "key status ok";
  doc.createNestedObject("data")["apikey_set"] = (apiKey.length() > 0);
  jsonReplyWithDoc(doc);
}

// ============================================================
//  OTA
// ============================================================

void update_firmware() {
  char SSID[20]; char PASSWORD[20];
  ssid.toCharArray(SSID, 20);
  password.toCharArray(PASSWORD, 20);
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(SSID, PASSWORD);

  if (WiFiMulti.run() == WL_CONNECTED) {
    WiFiClient client;
    httpUpdate.rebootOnUpdate(false);
    if (jsonMode) jsonReply("update_firmware", "ok", "ota started");
    else          SerialBT.println("Update Started.....");

    t_httpUpdate_return ret = httpUpdate.update(client, ipaddress, port.toInt(), "/firmware/ota.bin");
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        if (jsonMode) jsonReply("update_firmware", "error", "ota failed");
        delay(10000); break;
      case HTTP_UPDATE_NO_UPDATES:
        if (jsonMode) jsonReply("update_firmware", "ok", "ota no updates"); break;
      case HTTP_UPDATE_OK:
        if (jsonMode) jsonReply("update_firmware", "ok", "ota ok");
        delay(1000); ESP.restart(); break;
    }
  } else {
    if (jsonMode) jsonReply("update_firmware", "error", "wifi connect failed");
  }
}

// ============================================================
//  BATT_STATUS — legacy plain-text
// ============================================================

void BATT_STATUS() {
  batt_val = readBattPct();
  if (jsonMode) return;
  SerialBT.print("BATTERY CHARGE: ");
  SerialBT.print(batt_val);
  SerialBT.println(" %");
  SerialBT.print("VERSION: ");
  SerialBT.println(FW_VERSION);
}

// ============================================================
//  JSON METER RESPONSE BUILDERS
// ============================================================

String bcdStr(uint8_t raw) {
  uint8_t lo = 0, hi = 0;
  for (int k = 0; k < raw; k++) { lo++; if (lo == 10) { lo = 0; hi++; } }
  char b[3]; snprintf(b, sizeof(b), "%d%d", hi, lo);
  return String(b);
}

void jsonSend_IRDA1_RAW(String rawData) {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRDA1"; doc["msg"] = "irda 1ph raw ok";
  JsonObject data = doc.createNestedObject("data");
  data["raw"] = rawData; data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRDA1_PARSED(String p_sno, String p_mfid, String p_kwh, String p_rmd) {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRDA1P"; doc["msg"] = "irda 1ph parsed ok";
  JsonObject data = doc.createNestedObject("data");
  data["sno"] = p_sno; data["mfid"] = p_mfid; data["kwh"] = p_kwh; data["rmd"] = p_rmd;
  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRDA3_RAW(String rawData, String exportData = "") {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRDA3"; doc["msg"] = "irda 3ph raw ok";
  JsonObject data = doc.createNestedObject("data");
  data["raw"] = rawData;
  if (exportData.length() > 0) data["export_raw"] = exportData;
  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRDA3SR_RAW(String importRaw, String exportRaw) {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRDA3SR"; doc["msg"] = "irda 3ph solar raw ok";
  JsonObject data = doc.createNestedObject("data");
  data["import_raw"] = importRaw; data["export_raw"] = exportRaw;
  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRDA3P_PARSED(const char* cmdName) {
  StaticJsonDocument<1024> doc;
  doc["status"] = "ok"; doc["cmd"] = cmdName; doc["msg"] = "irda 3ph parsed ok";
  JsonObject data = doc.createNestedObject("data");

  message4[0]=0; message4[1]=readString[20]; message4[2]=readString[19]; message4[3]=readString[18]; str_hexto_dec();
  data["mfid"] = y;
  data["time"] = bcdStr(readString[21]) + ":" + bcdStr(readString[22]) + ":" + bcdStr(readString[23]);
  data["date"] = bcdStr(readString[24]) + ":" + bcdStr(readString[25]) + ":" + bcdStr(readString[26]);

  message4[0]=0; message4[1]=0; message4[2]=readString[28]; message4[3]=readString[27]; str_hexto_dec(); data["voltage_r"] = (float)y/10.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[30]; message4[3]=readString[29]; str_hexto_dec(); data["voltage_y"] = (float)y/10.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[32]; message4[3]=readString[31]; str_hexto_dec(); data["voltage_b"] = (float)y/10.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[34]; message4[3]=readString[33]; str_hexto_dec(); data["current_r"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[36]; message4[3]=readString[35]; str_hexto_dec(); data["current_y"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[38]; message4[3]=readString[37]; str_hexto_dec(); data["current_b"] = (float)y/100.0;

  message4[0]=readString[46]; message4[1]=readString[45]; message4[2]=readString[44]; message4[3]=readString[43]; str_hexto_dec(); data["kwh"]  = (float)y/100.0;
  message4[0]=readString[58]; message4[1]=readString[57]; message4[2]=readString[56]; message4[3]=readString[55]; str_hexto_dec(); data["kvah"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[60]; message4[3]=readString[59]; str_hexto_dec(); data["lbp1kvamd"] = (float)y/100.0;

  data["md_time"] = bcdStr(readString[61]) + ":" + bcdStr(readString[62]);
  data["md_date"] = bcdStr(readString[63]) + ":" + bcdStr(readString[64]) + ":" + bcdStr(readString[65]);

  String makeStr = ""; makeStr += (char)readString[66]; makeStr += (char)readString[67]; makeStr += (char)readString[68];
  data["make"] = makeStr;
  data["phase"] = (int)readString[69];
  message4[0]=0; message4[1]=0; message4[2]=readString[71]; message4[3]=readString[70]; str_hexto_dec(); data["mul_factor"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[89]; message4[3]=readString[88]; str_hexto_dec(); data["md_reset_count"] = y;

  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRDA3P_PARSED_HPL(const char* cmdName) {
  StaticJsonDocument<1024> doc;
  doc["status"] = "ok"; doc["cmd"] = cmdName; doc["msg"] = "irda 3ph parsed ok";
  JsonObject data = doc.createNestedObject("data");

  String mfidStr = "";
  mfidStr += (char)readString[30]; mfidStr += (char)readString[29];
  a = readString[28]; bcd_char(); mfidStr += String(a2); mfidStr += String(a1);
  a = readString[27]; bcd_char(); mfidStr += String(a2); mfidStr += String(a1);
  message4[0]=readString[26]; message4[1]=readString[25]; message4[2]=readString[24]; message4[3]=readString[23]; str_hexto_dec();
  int digits = 0; yb = y;
  while (yb > 0) { digits++; yb /= 10; }
  String padded = "";
  for (int k = 0; k < ndigits - digits; k++) padded += "0";
  padded += String(y);
  mfidStr += padded;
  data["mfid"] = mfidStr;

  data["time"] = bcdStr(readString[31]) + ":" + bcdStr(readString[32]) + ":" + bcdStr(readString[33]);
  data["date"] = bcdStr(readString[34]) + ":" + bcdStr(readString[35]) + ":" + bcdStr(readString[36]);

  message4[0]=0; message4[1]=0; message4[2]=readString[38]; message4[3]=readString[37]; str_hexto_dec(); data["voltage_r"] = (float)y/10.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[40]; message4[3]=readString[39]; str_hexto_dec(); data["voltage_y"] = (float)y/10.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[42]; message4[3]=readString[41]; str_hexto_dec(); data["voltage_b"] = (float)y/10.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[44]; message4[3]=readString[43]; str_hexto_dec(); data["current_r"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[46]; message4[3]=readString[45]; str_hexto_dec(); data["current_y"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[48]; message4[3]=readString[47]; str_hexto_dec(); data["current_b"] = (float)y/100.0;

  message4[0]=readString[52]; message4[1]=readString[51]; message4[2]=readString[50]; message4[3]=readString[49]; str_hexto_dec(); data["kwh"]  = (float)y/100.0;
  message4[0]=readString[56]; message4[1]=readString[55]; message4[2]=readString[54]; message4[3]=readString[53]; str_hexto_dec(); data["kvah"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[58]; message4[3]=readString[57]; str_hexto_dec(); data["lbp1kvamd"] = (float)y/100.0;

  data["md_time"] = bcdStr(readString[59]) + ":" + bcdStr(readString[60]);
  data["md_date"] = bcdStr(readString[61]) + ":" + bcdStr(readString[62]) + ":" + bcdStr(readString[63]);

  String makeStr = ""; makeStr += (char)readString[64]; makeStr += (char)readString[65]; makeStr += (char)readString[66];
  data["make"] = makeStr;
  data["phase"] = (int)readString[67];
  message4[0]=0; message4[1]=0; message4[2]=readString[69]; message4[3]=readString[68]; str_hexto_dec(); data["mul_factor"]     = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[71]; message4[3]=readString[70]; str_hexto_dec(); data["md_reset_count"] = y;

  data["digit_mode"]  = (ndigits == 8) ? "14HP" : "13HP";
  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRIR1_RAW(String rawData) {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRIR1"; doc["msg"] = "ir 1ph raw ok";
  JsonObject data = doc.createNestedObject("data");
  data["raw"] = rawData; data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRIR1_PARSED(String p_sno, String p_mfid, String p_kwh, String p_rmd) {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRIR1P"; doc["msg"] = "ir 1ph parsed ok";
  JsonObject data = doc.createNestedObject("data");
  data["sno"] = p_sno; data["mfid"] = p_mfid; data["kwh"] = p_kwh; data["rmd"] = p_rmd;
  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRIR3_RAW(String rawData) {
  StaticJsonDocument<512> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRIR3"; doc["msg"] = "ir 3ph raw ok";
  JsonObject data = doc.createNestedObject("data");
  data["raw"] = rawData; data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

void jsonSend_IRIR3_PARSED() {
  StaticJsonDocument<1024> doc;
  doc["status"] = "ok"; doc["cmd"] = "IRIR3P"; doc["msg"] = "ir 3ph parsed ok";
  JsonObject data = doc.createNestedObject("data");

  message4[0]=readString[6]; message4[1]=readString[7]; message4[2]=readString[8]; message4[3]=readString[9]; str_hexto_dec();
  data["mfid"] = y;
  data["date"] = bcdStr(readString[10]) + ":" + bcdStr(readString[11]) + ":" + bcdStr(readString[12]);
  data["time"] = bcdStr(readString[13]) + ":" + bcdStr(readString[14]);

  message4[0]=readString[15]; message4[1]=readString[16]; message4[2]=readString[17]; message4[3]=readString[18]; str_hexto_dec(); data["kwh"]       = (float)y/1000.0;
  message4[0]=readString[19]; message4[1]=readString[20]; message4[2]=readString[21]; message4[3]=readString[22]; str_hexto_dec(); data["kvarh_lag"]  = (float)y/1000.0;
  message4[0]=readString[23]; message4[1]=readString[24]; message4[2]=readString[25]; message4[3]=readString[26]; str_hexto_dec(); data["kvarh_lead"] = (float)y/1000.0;
  message4[0]=readString[27]; message4[1]=readString[28]; message4[2]=readString[29]; message4[3]=readString[30]; str_hexto_dec(); data["kvah"]       = (float)y/1000.0;
  message4[0]=0; message4[1]=0; message4[2]=0; message4[3]=readString[31]; str_hexto_dec(); data["avg_pf"] = (float)y/100.0;
  message4[0]=0; message4[1]=0; message4[2]=readString[32]; message4[3]=readString[33]; str_hexto_dec(); data["md_kwh"] = (float)y/1000.0;

  data["md_date"] = bcdStr(readString[34]) + ":" + bcdStr(readString[35]) + ":" + bcdStr(readString[36]);
  data["md_time"] = bcdStr(readString[37]) + ":" + bcdStr(readString[38]);

  message4[0]=0; message4[1]=0; message4[2]=readString[39]; message4[3]=readString[40]; str_hexto_dec(); data["tamper_count"]  = y;
  message4[0]=0; message4[1]=0; message4[2]=readString[41]; message4[3]=readString[42]; str_hexto_dec(); data["tamper_status"] = y;

  data["battery_pct"] = readBattPct(); data["version"] = FW_VERSION;
  jsonReplyWithDoc(doc);
}

// ============================================================
//  COMMAND ROUTER
// ============================================================

void processCommand() {
  readString.trim();
  if (readString.length() == 0) return;

  if (readString.charAt(0) == '{') {
    jsonMode = true;
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, readString);
    if (err) { jsonReply("parse_error", "error", "invalid json"); return; }

    String cmd = doc["cmd"] | "";
    String paramVal = "";
    if (doc.containsKey("params") && doc["params"].containsKey("value"))
      paramVal = doc["params"]["value"].as<String>();

    // --------------------------------------------------------
    // PATCH 4: KEY-EXEMPT COMMANDS (always allowed, no key needed)
    //   set_key      — how would you set the key if it required the key?
    //   get_key_status — safe read-only status, no sensitive action
    //   ping         — harmless connectivity check
    // --------------------------------------------------------
    if (cmd == "set_key")        { ledon(); beep1(); update_apikey(paramVal); ledoff(); return; }
    if (cmd == "get_key_status") { ledon(); beep1(); get_key_status();        ledoff(); return; }
    if (cmd == "ping")           { ledon(); jsonReply("ping", "ok", "pong");  ledoff(); return; }

    // --------------------------------------------------------
    // PATCH 4 (continued): API KEY CHECK
    //   Runs ONLY when apiKey is set (non-empty).
    //   If key is empty in NVS, all commands pass through freely
    //   (safe first-boot / backward-compat behaviour).
    // --------------------------------------------------------
    if (apiKey.length() > 0) {
      String receivedKey = doc["key"] | "";
      if (receivedKey != apiKey) {
        jsonReply(cmd.c_str(), "error", "unauthorized — invalid or missing key");
        return;
      }
    }

    // ---- All commands below this line are key-protected ----

    if (cmd == "update_bname")     { ledon(); beep1(); update_bname(paramVal);     ledoff(); return; }
    if (cmd == "update_ssid")      { ledon(); beep1(); update_ssid(paramVal);      ledoff(); return; }
    if (cmd == "update_password")  { ledon(); beep1(); update_password(paramVal);  ledoff(); return; }
    if (cmd == "update_ipaddress") { ledon(); beep1(); update_ipaddress(paramVal); ledoff(); return; }
    if (cmd == "update_port")      { ledon(); beep1(); update_port(paramVal);      ledoff(); return; }
    if (cmd == "update_firmware")  { ledon(); beep1(); update_firmware();          ledoff(); return; }
    if (cmd == "get_config")       { ledon(); beep1(); ReadAllValues();            ledoff(); return; }

    if (cmd == "get_battery") {
      ledon(); beep1();
      StaticJsonDocument<256> d;
      d["status"] = "ok"; d["cmd"] = "get_battery"; d["msg"] = "battery ok";
      d.createNestedObject("data")["battery_pct"] = readBattPct();
      jsonReplyWithDoc(d); ledoff(); beep2(); return;
    }
    if (cmd == "get_version") {
      ledon(); beep1();
      StaticJsonDocument<256> d;
      d["status"] = "ok"; d["cmd"] = "get_version"; d["msg"] = "version ok";
      d.createNestedObject("data")["version"] = FW_VERSION;
      jsonReplyWithDoc(d); ledoff(); beep2(); return;
    }

    if (cmd == "IRDA1")      { IRDA2400(); ledon(); beep1(); meter_flag=1; IRDA1_PHASE();        ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA1P")     { IRDA2400(); ledon(); beep1(); meter_flag=2; IRDA1_PHASE_P();      ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA3")      { IRDA9600(); ledon(); beep1(); meter_flag=3; IRDA3_PHASE();        ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA3P")     { IRDA9600(); ledon(); beep1(); meter_flag=4; IRDA3_PHASE_P();      ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA3P14HP") { IRDA9600(); ledon(); beep1(); meter_flag=4; ndigits=8; IRDA3_PHASE_P14HP(); ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA3P13HP") { IRDA9600(); ledon(); beep1(); meter_flag=4; ndigits=7; IRDA3_PHASE_P14HP(); ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA3SR")    { IRDA9600(); ledon(); beep1(); meter_flag=5; IRDA3_PHASE();        ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRDA3SP")    { IRDA9600(); ledon(); beep1(); meter_flag=6; IRDA3_PHASE_P();      ledoff(); beep2(); digitalWrite(irdaen,1); return; }
    if (cmd == "IRIR1")  { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=7;  IRIR1_PHASE();   ledoff(); beep2(); return; }
    if (cmd == "IRIR1P") { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=8;  IRIR1_PHASE_P(); ledoff(); beep2(); return; }
    if (cmd == "IRIR3")  { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=9;  IRIR3_PHASE();   ledoff(); beep2(); return; }
    if (cmd == "IRIR3P") { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=10; IRIR3_PHASE_P(); ledoff(); beep2(); return; }

    jsonReply(cmd.c_str(), "error", "unknown command");
    return;
  }

  // LEGACY RAW STRING PATH (unchanged, unprotected — backward compatible)
  jsonMode = false;
  SerialBT.println(readString);

  if (readString.indexOf("update_bname")     != -1) { ledon(); beep1(); update_bname();     ledoff(); }
  if (readString.indexOf("update_ssid")      != -1) { ledon(); beep1(); update_ssid();      ledoff(); }
  if (readString.indexOf("update_password")  != -1) { ledon(); beep1(); update_password();  ledoff(); }
  if (readString.indexOf("update_ipaddress") != -1) { ledon(); beep1(); update_ipaddress(); ledoff(); }
  if (readString.indexOf("update_port")      != -1) { ledon(); beep1(); update_port();      ledoff(); }
  if (readString.indexOf("update_firmware")  != -1) { ledon(); beep1(); update_firmware();  ledoff(); }
  if (readString.indexOf("get_config")       != -1) { ledon(); beep1(); ReadAllValues();    ledoff(); }

  if (readString == "#IRDA1*")     { ledon(); beep1(); meter_flag=1; delay(2); delay(10); IRDA2400(); IRDA1_PHASE();   ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA1P*")    { ledon(); beep1(); meter_flag=2; IRDA2400(); IRDA1_PHASE_P();                      ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA3*")     { IRDA9600(); ledon(); beep1(); meter_flag=3; IRDA3_PHASE();                        ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA3P*")    { IRDA9600(); ledon(); beep1(); meter_flag=4; IRDA3_PHASE_P();                      ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA3P14HP*"){ IRDA9600(); ledon(); beep1(); meter_flag=4; ndigits=8; IRDA3_PHASE_P14HP();      ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA3P13HP*"){ IRDA9600(); ledon(); beep1(); meter_flag=4; ndigits=7; IRDA3_PHASE_P14HP();      ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA3SR*")   { IRDA9600(); ledon(); beep1(); meter_flag=5; IRDA3_PHASE();                        ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRDA3SP*")   { IRDA9600(); ledon(); beep1(); meter_flag=6; IRDA3_PHASE_P();                      ledoff(); beep2(); digitalWrite(irdaen,1); }
  if (readString == "#IRIR1*")  { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=7;  delay(10); IRIR1_PHASE();   ledoff(); beep2(); }
  if (readString == "#IRIR1P*") { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=8;  delay(10); IRIR1_PHASE_P(); ledoff(); beep2(); }
  if (readString == "#IRIR3*")  { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=9;  delay(10); IRIR3_PHASE();   ledoff(); beep2(); }
  if (readString == "#IRIR3P*") { SerialPort1.begin(2400,SERIAL_8N1,RXD1,TXD1); delay(1000); ledon(); beep1(); meter_flag=10; delay(10); IRIR3_PHASE_P(); ledoff(); beep2(); }
  if (readString == "#BATTV*") {
    ledon(); beep1();
    batt_val = 0;
    for (i = 0; i < 51; i++) { batt_val += analogRead(15); delay(20); }
    batt_val = batt_val / 50; batt_val = (batt_val - 2000) / 4;
    if (batt_val < 0) batt_val = 0; if (batt_val > 100) batt_val = 100;
    SerialBT.print("BATTERY CHARGE: "); SerialBT.print(batt_val); SerialBT.println(" %");
    ledoff(); beep2();
  }
  if (readString == "#VER*") { ledon(); beep1(); SerialBT.println(FW_VERSION); ledoff(); beep2(); }
  if (readString == "   ")   { ledon(); SerialBT.println("PT-OK "); ledoff(); }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  pinMode(led, OUTPUT); pinMode(buz, OUTPUT); pinMode(buz1, OUTPUT);
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

  // ESP32 Arduino core 3.x API (ledcSetup/ledcAttachPin removed)
  ledcAttach(ledPin, freq, resolution);
  ledcWrite(ledPin, 85);

  pinMode(irdaen, OUTPUT); pinMode(extpw, OUTPUT); pinMode(extsw, INPUT);
  digitalWrite(extpw, 1); digitalWrite(irdaen, 1);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);

  delay(5);
  ledon(); beep2(); ledoff();
  IRDA2400(); delay(1000);
  IRDA9600(); delay(100);
  digitalWrite(irdaen, 1);
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
  readString = "";
  while (SerialBT.available()) { char c = SerialBT.read(); readString += c; stime = 0; }
  if (readString.length() > 0) processCommand();

  if (digitalRead(extsw) == 0) {
    delay(2000);
    if (digitalRead(extsw) == 0) {
      beep1(); beep1(); beep1(); beep1(); beep1();
      digitalWrite(extpw, 0);
      while (digitalRead(extsw) == 0) {}
      digitalWrite(extpw, 0);
      esp_deep_sleep_start();
      delay(1000);
      beep1(); delay(300); beep1(); delay(300); beep1(); delay(300); beep1(); delay(300);
    }
  }

  stime = stime + 1;
  delay(1);
  if (stime > 210000) {
    beep1(); beep1(); beep1(); beep1();
    digitalWrite(extpw, 0); delay(5);
    esp_deep_sleep_start();
    delay(1000);
    beep1(); delay(300); beep1(); delay(300); beep1(); delay(300); beep1(); delay(300);
  }
  readString = "";
}

// ============================================================
//  SUB-PROGRAMS (ALL ORIGINAL — ZERO CHANGES)
// ============================================================

void IRDA2400() {
  digitalWrite(irdaen, 1); SerialPort2.begin(2400); delay(50);
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 10));
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 9));
  delay(500); SerialPort2.flush(); delay(50); SerialPort2.println(); delay(200);
  digitalWrite(irdaen, 0);
}

void IRDA9600() {
  digitalWrite(irdaen, 1); SerialPort2.begin(9600); delay(50);
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 10));
  WRITE_PERI_REG(0x3FF6E020, READ_PERI_REG(0x3FF6E020) | (1 << 16) | (1 << 9));
  delay(500); SerialPort2.flush(); delay(50); SerialPort2.println(); delay(100);
  digitalWrite(irdaen, 0);
}

void beep1() {
  for (i = 1; i < 500; i++) {
    digitalWrite(buz,1); digitalWrite(buz,1); delayMicroseconds(175);
    digitalWrite(buz,0); digitalWrite(buz,0); delayMicroseconds(175);
  }
}
void beep2() { beep1(); delay(100); beep1(); }
void xbeep1() { digitalWrite(buz,1); delay(100); digitalWrite(buz,0); }
void xbeep2() { digitalWrite(buz,1); delay(100); digitalWrite(buz,0); delay(50); digitalWrite(buz,1); delay(100); digitalWrite(buz,0); }
void ledon()  { digitalWrite(led,1); delay(2); }
void ledoff() { digitalWrite(led,0); delay(2); }

// ---- IRDA 1Ph RAW ----
void IRDA1_PHASE() {
  readString = "";
  SerialPort2.println(":00413BC4"); delay(200); PACKET_READ();
  SerialPort2.println(":00423AC5"); delay(200); PACKET_READ();
  SerialPort2.println(":004339C6"); delay(200); PACKET_READ();
  SerialPort2.println(":004537C8"); delay(200); PACKET_READ();
  SerialPort2.println(":004636C9"); delay(200); PACKET_READ();

  if (jsonMode) { jsonSend_IRDA1_RAW(readString); }
  else {
    SerialBT.println(readString); BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-1Ph-RAW."); SerialBT.print((char)254); delay(10);
  }
}

// ---- IRDA 1Ph PARSED ----
// FIX: each register read now resets readString before PACKET_READ() so
//      echo bytes from SerialPort2.println() don't corrupt the extracted fields.
//      .trim() removes any trailing \r\n from packet data.
void IRDA1_PHASE_P() {
  readString = "";
  SerialPort2.println(":00413BC4"); delay(200); readpacket = ""; readString = ""; PACKET_READ();
  String l_sno = readString; l_sno.remove(0, 16); l_sno.remove(l_sno.length() - 6, 6); l_sno.trim();

  readString = "";
  SerialPort2.println(":00423AC5"); delay(800); readpacket = ""; readString = ""; PACKET_READ();
  String l_mfid = readString; l_mfid.remove(0, 16); l_mfid.remove(l_mfid.length() - 6, 6); l_mfid.trim();

  readString = "";
  SerialPort2.println(":004339C6"); delay(200); readpacket = ""; readString = ""; PACKET_READ();
  String l_kwh = readString; l_kwh.remove(0, 16); l_kwh.remove(l_kwh.length() - 6, 6); l_kwh.trim();

  readString = "";
  SerialPort2.println(":004636C9"); delay(200); readpacket = ""; readString = ""; PACKET_READ();
  String l_rmd = readString; l_rmd.remove(0, 16); l_rmd.remove(5); l_rmd.trim();

  if (jsonMode) { jsonSend_IRDA1_PARSED(l_sno, l_mfid, l_kwh, l_rmd); }
  else {
    SerialBT.println("S.NO:" + l_sno); SerialBT.println("M.ID:" + l_mfid);
    SerialBT.println("KWH:" + l_kwh);  SerialBT.println("RMD:" + l_rmd);
    BATT_STATUS();
    SerialBT.println("DATA RECEIVED: IRDA-1Ph-PARSED."); SerialBT.print((char)254); delay(10);
  }
}

// ---- IRDA 3Ph RAW ----
void IRDA3_PHASE() {
  rs = "";
  SerialPort2.write(message1, sizeof(message1));
  int len = rs.length(); rs.remove(0, len); rs = "";
  nbytes = 30; PACKET_READ3X();
  delay(50);
  message2[2] = rs[18]; message2[3] = rs[19]; message2[4] = rs[20];

  SerialPort2.write(message2, sizeof(message2));
  readString = ""; nbytes = 79; PACKET_READ3();
  String importRaw = readString;

  if (meter_flag == 3) {
    if (jsonMode) jsonSend_IRDA3_RAW(importRaw);
    else { SerialBT.println(readString); BATT_STATUS(); SerialBT.println("DATA RECEIVED: IRDA-3Ph-RAW."); SerialBT.print((char)254); delay(10); }
  }
  if (meter_flag == 5) {
    message2[6] = 0x01;
    SerialPort2.write(message2, sizeof(message2));
    readString = ""; nbytes = 79; PACKET_READ3();
    String exportRaw = readString;
    if (jsonMode) jsonSend_IRDA3SR_RAW(importRaw, exportRaw);
    else {
      SerialBT.println(importRaw); SerialBT.println("                 "); SerialBT.println("** EXPORT DATA **");
      SerialBT.println(exportRaw); BATT_STATUS();
      SerialBT.println("DATA RECEIVED: IRDA-3Ph-SOLAR-RAW."); SerialBT.print((char)254); delay(10);
    }
    message2[6] = 0x00;
  }
}

// ---- IRDA 3Ph PARSED (HPL 14/13 digit) ----
void IRDA3_PHASE_P14HP() {
  beep1(); rs = "";
  SerialPort2.write(message6, sizeof(message6));
  len = rs.length(); rs.remove(0, len); rs = "";
  delay(1); nbytes = 45; PACKET_READ3X(); len = rs.length();
  delay(1500);
  message7[2]=rs[32]; message7[3]=rs[33]; message7[4]=rs[34]; message7[5]=rs[35];
  message7[6]=rs[36]; message7[7]=rs[37]; message7[8]=rs[38]; message7[9]=rs[39];
  SerialPort2.write(message7, sizeof(message7));

  if (jsonMode) { readString = ""; nbytes = 71; PACKET_READ3(); jsonSend_IRDA3P_PARSED_HPL((ndigits == 8) ? "IRDA3P14HP" : "IRDA3P13HP"); }
  else { PACKET_READ_PARSEY(); if (meter_flag == 4) { BATT_STATUS(); SerialBT.println("DATA RECEIVED: IRDA-3Ph-PARSED."); SerialBT.print((char)254); delay(10); } }
}

// ---- IRDA 3Ph PARSED (auto-detect) ----
void IRDA3_PHASE_P() {
  rs = "";
  SerialPort2.write(message1, sizeof(message1));
  len = rs.length(); rs.remove(0, len); rs = "";
  delay(1); nbytes = 30; PACKET_READ3X(); len = rs.length();

  if (len >= 30) {
    delay(1500);
    message2[2]=rs[22]; message2[3]=rs[23]; message2[4]=rs[24];
    SerialPort2.write(message2, sizeof(message2));

    if (jsonMode) {
      readString = ""; nbytes = 79; PACKET_READ3();
      if (meter_flag == 4) jsonSend_IRDA3P_PARSED("IRDA3P");
      if (meter_flag == 6) {
        message2[6] = 0x01; SerialPort2.write(message2, sizeof(message2));
        readString = ""; nbytes = 79; PACKET_READ3();
        jsonSend_IRDA3P_PARSED("IRDA3SP"); message2[6] = 0x00;
      }
    } else {
      PACKET_READ_PARSE();
      if (meter_flag == 4) { BATT_STATUS(); SerialBT.println("DATA RECEIVED: IRDA-3Ph-PARSED."); SerialBT.print((char)254); delay(10); }
      if (meter_flag == 6) {
        SerialBT.println("                 "); SerialBT.println("** EXPORT DATA **");
        message2[6] = 0x01; SerialPort2.write(message2, sizeof(message2));
        PACKET_READ_PARSE(); BATT_STATUS();
        SerialBT.println("DATA RECEIVED: IRDA-3Ph-SOLAR-PARSED."); SerialBT.print((char)254); delay(10);
        message2[6] = 0x00;
      }
    }
  } else {
    beep1(); rs = "";
    SerialPort2.write(message6, sizeof(message6));
    len = rs.length(); rs.remove(0, len); rs = "";
    delay(1); nbytes = 45; PACKET_READ3X(); len = rs.length();
    delay(1500);
    message7[2]=rs[32]; message7[3]=rs[33]; message7[4]=rs[34]; message7[5]=rs[35];
    message7[6]=rs[36]; message7[7]=rs[37]; message7[8]=rs[38]; message7[9]=rs[39];
    SerialPort2.write(message7, sizeof(message7));
    ndigits = 8;
    if (jsonMode) { readString = ""; nbytes = 71; PACKET_READ3(); jsonSend_IRDA3P_PARSED_HPL("IRDA3P"); }
    else { PACKET_READ_PARSEY(); if (meter_flag == 4) { BATT_STATUS(); SerialBT.println("DATA RECEIVED: IRDA-3Ph-PARSED."); SerialBT.print((char)254); delay(10); } }
  }
}

// ---- PACKET_READ_PARSEY (legacy plain-text output only) ----
void PACKET_READ_PARSEY() {
  readString = ""; nbytes = 71; PACKET_READ3();
  SerialBT.print("MF.ID:");
  SerialBT.print(readString[30]); SerialBT.print(readString[29]);
  a = readString[28]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1);
  a = readString[27]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1);
  message4[0]=readString[26]; message4[1]=readString[25]; message4[2]=readString[24]; message4[3]=readString[23]; str_hexto_dec();
  int digits = 0; yb = y; while (yb > 0) { digits++; yb /= 10; }
  for (i = 0; i < ndigits - digits; i++) SerialBT.print('0');
  SerialBT.println(y);
  SerialBT.print("TIME:"); a=readString[31]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[32]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[33]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("DATE:"); a=readString[34]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[35]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[36]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  message4[0]=0; message4[1]=0; message4[2]=readString[38]; message4[3]=readString[37]; str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-R:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[40]; message4[3]=readString[39]; str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-Y:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[42]; message4[3]=readString[41]; str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-B:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[44]; message4[3]=readString[43]; str_hexto_dec(); yx=y; SerialBT.print("CURRENT-R:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[46]; message4[3]=readString[45]; str_hexto_dec(); yx=y; SerialBT.print("CURRENT-Y:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[48]; message4[3]=readString[47]; str_hexto_dec(); yx=y; SerialBT.print("CURRENT-B:"); SerialBT.println(yx/100);
  message4[0]=readString[52]; message4[1]=readString[51]; message4[2]=readString[50]; message4[3]=readString[49]; str_hexto_dec(); yx=y; SerialBT.print("KWh:"); SerialBT.println(yx/100);
  message4[0]=readString[56]; message4[1]=readString[55]; message4[2]=readString[54]; message4[3]=readString[53]; str_hexto_dec(); yx=y; SerialBT.print("KVAh:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[58]; message4[3]=readString[57]; str_hexto_dec(); yx=y; SerialBT.print("LBP1KvAMD:"); SerialBT.println(yx/100);
  SerialBT.print("MD TIME:"); a=readString[59]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[60]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MD DATE:"); a=readString[61]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[62]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[63]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MAKE:"); SerialBT.print(readString[64]); SerialBT.print(readString[65]); SerialBT.println(readString[66]);
  SerialBT.print("Phase:"); SerialBT.println(readString[67], DEC);
  message4[0]=0; message4[1]=0; message4[2]=readString[69]; message4[3]=readString[68]; str_hexto_dec(); yx=y; SerialBT.print("MUL.FACTOR:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[71]; message4[3]=readString[70]; str_hexto_dec(); yx=y; SerialBT.print("MD RESET COUNT:"); SerialBT.println(yx);
}

// ---- PACKET_READ_PARSE (legacy plain-text output only) ----
void PACKET_READ_PARSE() {
  readString = ""; nbytes = 79; PACKET_READ3();
  message4[0]=0; message4[1]=readString[20]; message4[2]=readString[19]; message4[3]=readString[18]; str_hexto_dec();
  SerialBT.print("MF.ID:"); SerialBT.println(y, DEC);
  SerialBT.print("TIME:"); a=readString[21]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[22]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[23]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("DATE:"); a=readString[24]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[25]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[26]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  message4[0]=0; message4[1]=0; message4[2]=readString[28]; message4[3]=readString[27]; str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-R:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[30]; message4[3]=readString[29]; str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-Y:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[32]; message4[3]=readString[31]; str_hexto_dec(); yx=y; SerialBT.print("VOLTAGE-B:"); SerialBT.println(yx/10);
  message4[0]=0; message4[1]=0; message4[2]=readString[34]; message4[3]=readString[33]; str_hexto_dec(); yx=y; SerialBT.print("CURRENT-R:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[36]; message4[3]=readString[35]; str_hexto_dec(); yx=y; SerialBT.print("CURRENT-Y:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[38]; message4[3]=readString[37]; str_hexto_dec(); yx=y; SerialBT.print("CURRENT-B:"); SerialBT.println(yx/100);
  message4[0]=readString[46]; message4[1]=readString[45]; message4[2]=readString[44]; message4[3]=readString[43]; str_hexto_dec(); yx=y; SerialBT.print("KWh:"); SerialBT.println(yx/100);
  message4[0]=readString[58]; message4[1]=readString[57]; message4[2]=readString[56]; message4[3]=readString[55]; str_hexto_dec(); yx=y; SerialBT.print("KVAh:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[60]; message4[3]=readString[59]; str_hexto_dec(); yx=y; SerialBT.print("LBP1KvAMD:"); SerialBT.println(yx/100);
  SerialBT.print("MD TIME:"); a=readString[61]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[62]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MD DATE:"); a=readString[63]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[64]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[65]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MAKE:"); SerialBT.print(readString[66]); SerialBT.print(readString[67]); SerialBT.println(readString[68]);
  SerialBT.print("Phase:"); SerialBT.println(readString[69], DEC);
  message4[0]=0; message4[1]=0; message4[2]=readString[71]; message4[3]=readString[70]; str_hexto_dec(); yx=y; SerialBT.print("MUL.FACTOR:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[89]; message4[3]=readString[88]; str_hexto_dec(); yx=y; SerialBT.print("MD RESET COUNT:"); SerialBT.println(yx);
}

// ---- IR 1Ph RAW ----
void IRIR1_PHASE() {
  readString = "";
  SerialPort1.println(":00413BC4"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":00423AC5"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004339C6"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004537C8"); delay(600);  PACKETIR_READ1();
  SerialPort1.println(":004636C9"); delay(1000); PACKETIR_READ1();
  if (jsonMode) { jsonSend_IRIR1_RAW(readString); }
  else { SerialBT.println(readString); BATT_STATUS(); SerialBT.println("DATA RECEIVED: IR-1Ph-RAW."); SerialBT.print((char)254); delay(10); }
}

// ---- IR 1Ph PARSED ----
void IRIR1_PHASE_P() {
  readString = "";
  SerialPort1.println(":00413BC4"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":00423AC5"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004339C6"); delay(200); PACKETIR_READ1();
  SerialPort1.println(":004636C9"); delay(200); PACKETIR_READ1();
  rs = readString;
  String l_sno = rs; l_sno.remove(0, 16); l_sno.remove(8);
  String l_mfid = rs; l_mfid.remove(0, 46); l_mfid.remove(16);
  String l_kwh  = rs; l_kwh.remove(0, 84);  l_kwh.remove(9);
  String l_rmd  = rs; l_rmd.remove(0, 115); l_rmd.remove(5);
  if (jsonMode) { jsonSend_IRIR1_PARSED(l_sno, l_mfid, l_kwh, l_rmd); }
  else { SerialBT.println("S.NO:" + l_sno); SerialBT.println("M.ID:" + l_mfid); SerialBT.println("KWH:" + l_kwh); SerialBT.println("RMD:" + l_rmd); BATT_STATUS(); SerialBT.println("DATA RECEIVED: IR-1Ph-PARSED."); SerialBT.print((char)254); delay(10); }
}

// ---- IR 3Ph RAW ----
void IRIR3_PHASE() {
  readString = "";
  SerialPort1.write(message3[0]); delay(2); SerialPort1.write(message3[1]); delay(2);
  SerialPort1.write(message3[2]); delay(2); SerialPort1.write(message3[3]); delay(2);
  SerialPort1.write(message3[4]); delay(2);
  delay(500); readString = ""; PACKETIR_READ3(); rs = readString;
  if (jsonMode) { jsonSend_IRIR3_RAW(readString); }
  else { SerialBT.println(readString); BATT_STATUS(); SerialBT.println("DATA RECEIVED: IR-3Ph-RAW."); SerialBT.print((char)254); delay(10); }
}

// ---- IR 3Ph PARSED ----
void IRIR3_PHASE_P() {
  readString = "";
  SerialPort1.write(message3, sizeof(message3));
  readString = ""; delay(200);
  if (jsonMode) { PACKETIR_READ3(); jsonSend_IRIR3_PARSED(); }
  else { IR3_PACKET_READ_PARSE(); BATT_STATUS(); SerialBT.println("DATA RECEIVED: IR-3Ph-PARSED."); SerialBT.print((char)254); delay(10); }
}

// ---- IR3_PACKET_READ_PARSE (legacy plain-text only) ----
void IR3_PACKET_READ_PARSE() {
  readString = ""; PACKETIR_READ3();
  message4[0]=readString[6]; message4[1]=readString[7]; message4[2]=readString[8]; message4[3]=readString[9]; str_hexto_dec();
  SerialBT.print("MF.ID:"); SerialBT.println(y, DEC);
  SerialBT.print("DATE:"); a=readString[10]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[11]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[12]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("TIME:"); a=readString[13]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[14]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  message4[0]=readString[15]; message4[1]=readString[16]; message4[2]=readString[17]; message4[3]=readString[18]; str_hexto_dec(); yx=y; SerialBT.print("KWh:");        SerialBT.println(yx/1000);
  message4[0]=readString[19]; message4[1]=readString[20]; message4[2]=readString[21]; message4[3]=readString[22]; str_hexto_dec(); yx=y; SerialBT.print("KVArh-Lag:");   SerialBT.println(yx/1000);
  message4[0]=readString[23]; message4[1]=readString[24]; message4[2]=readString[25]; message4[3]=readString[26]; str_hexto_dec(); yx=y; SerialBT.print("KVArh-Lead:");  SerialBT.println(yx/1000);
  message4[0]=readString[27]; message4[1]=readString[28]; message4[2]=readString[29]; message4[3]=readString[30]; str_hexto_dec(); yx=y; SerialBT.print("KVAh:");        SerialBT.println(yx/1000);
  message4[0]=0; message4[1]=0; message4[2]=0; message4[3]=readString[31]; str_hexto_dec(); yx=y; SerialBT.print("Avg P.F:"); SerialBT.println(yx/100);
  message4[0]=0; message4[1]=0; message4[2]=readString[32]; message4[3]=readString[33]; str_hexto_dec(); yx=y; SerialBT.print("MD-KWh:"); SerialBT.println(yx/1000);
  SerialBT.print("MD-DATE:"); a=readString[34]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[35]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[36]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  SerialBT.print("MD-TIME:"); a=readString[37]; bcd_char(); SerialBT.print(a2); SerialBT.print(a1); SerialBT.print(":"); a=readString[38]; bcd_char(); SerialBT.print(a2); SerialBT.println(a1);
  message4[0]=0; message4[1]=0; message4[2]=readString[39]; message4[3]=readString[40]; str_hexto_dec(); SerialBT.print("TAMPER COUNT:");  SerialBT.println(y);
  message4[0]=0; message4[1]=0; message4[2]=readString[41]; message4[3]=readString[42]; str_hexto_dec(); SerialBT.print("TAMPER-STATUS:"); SerialBT.println(y);
}

// ============================================================
//  SUB-SUB-PROGRAMS (ALL ORIGINAL — ZERO CHANGES)
// ============================================================

void str_hexto_dec() {
  uint32_t y1 = (uint32_t)message4[0] << 24;
  uint32_t y2 = (uint32_t)message4[1] << 16;
  uint32_t y3 = (uint32_t)message4[2] << 8;
  uint32_t y4 = (uint32_t)message4[3] << 0;
  y = y1 + y2 + y3 + y4;
}

void bcd_charx() { a1=0; a2=0; a1=a&15; a2=a&240; a2=a2>>4; }

void bcd_char() {
  a1=0; a2=0;
  for (i=0; i<a; i++) { a1=a1+1; if (a1==10) { a1=0; a2=a2+1; } }
}

void PACKET_READ() {
  readpacket = ""; i = 0;
  boolean at_flag = 1;
  while (at_flag) {
    while (SerialPort2.available()) { delay(3); char c = SerialPort2.read(); readString += c; readpacket += c; }
    delay(1); i++;
    if ((readpacket.length() > 26) || (i > 2000)) at_flag = 0;
  }
  delay(10);
}

void PACKET_READ3() {
  readpacket = ""; i = 0;
  boolean at_flag = 1;
  while (at_flag) {
    while (SerialPort2.available()) { delay(3); char c = SerialPort2.read(); readString += c; readpacket += c; }
    delay(1); i++;
    if ((readpacket.length() > nbytes) || (i > 9000)) at_flag = 0;
  }
  delay(10);
}

void PACKET_READ3X() {
  readpacket = ""; rs = ""; i = 0;
  boolean at_flag = 1;
  while (at_flag) {
    while (SerialPort2.available()) { delay(3); char c = SerialPort2.read(); rs += c; readpacket += c; }
    delay(1); i++;
    if ((readpacket.length() >= nbytes) || (i > 4000)) at_flag = 0;
  }
  delay(10);
}

void PACKETIR_READ1() {
  delay(500); readpacket = ""; i = 0;
  boolean at_flag = 1;
  while (at_flag) {
    while (SerialPort1.available()) { delay(3); char c = SerialPort1.read(); readString += c; readpacket += c; }
    delay(1); i++;
    if ((readpacket.length() > 26) || (i > 2000)) at_flag = 0;
  }
  delay(10);
}

void PACKETIR_READ3() {
  delay(100); readpacket = ""; i = 0;
  boolean at_flag = 1;
  while (at_flag) {
    while (SerialPort1.available()) { delay(3); char c = SerialPort1.read(); readString += c; readpacket += c; }
    delay(1); i++;
    if ((readpacket.length() > 40) || (i > 2000)) at_flag = 0;
  }
  delay(10);
}

void readBluetoothName() {
  preferences.getString("bluetooth_name", bluetooth_name, MAX_BT_NAME_LENGTH);
  if (strlen(bluetooth_name) == 0) { strcpy(bluetooth_name, "PTA-DEFAULT"); saveBluetoothName(); }
}

void saveBluetoothName() { preferences.putString("bluetooth_name", bluetooth_name); }

//*********************** END OF PROGRAM *****************************
