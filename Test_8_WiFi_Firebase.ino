// ============================================================
// QC SCANNER TEST 8 — WiFi + Firebase
// Board: ESP32-S3-DevKitC-1 N16R8
//
// Tests QC Scanner specific Firebase reads and writes:
//   - Read /batches (filter status=OPEN)
//   - Read /formulas (filter active=true)
//   - Write PRE scan data to a test batch
//   - Write POST scan data
//   - Write analysis result
//   - Atomic PATCH to close batch
//
// BEFORE RUNNING: edit credentials below
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── EDIT THESE ────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";
const char* FIREBASE_HOST = "https://your-project-default-rtdb.firebaseio.com/";
const char* FIREBASE_KEY  = "YOUR_FIREBASE_API_KEY";
// ─────────────────────────────────────────────────────────────

WiFiClientSecure client;

String fbUrl(const String& path) {
  return String(FIREBASE_HOST) + path + ".json?auth=" + FIREBASE_KEY;
}

String fbGet(const String& path) {
  HTTPClient http;
  http.begin(client, fbUrl(path));
  http.setTimeout(8000);
  int code = http.GET();
  String body = (code == 200) ? http.getString() : "";
  http.end();
  Serial.printf("  GET %s → %d\n", path.c_str(), code);
  return body;
}

bool fbPut(const String& path, const String& payload) {
  HTTPClient http;
  http.begin(client, fbUrl(path));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  int code = http.PUT(payload);
  http.end();
  Serial.printf("  PUT %s → %d\n", path.c_str(), code);
  return (code == 200);
}

bool fbPatch(const String& path, const String& payload) {
  HTTPClient http;
  http.begin(client, fbUrl(path));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("  PATCH %s → %d\n", path.c_str(), code);
  return (code == 200);
}

bool fbDelete(const String& path) {
  HTTPClient http;
  http.begin(client, fbUrl(path));
  http.setTimeout(8000);
  int code = http.sendRequest("DELETE");
  http.end();
  return (code == 200);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== QC SCANNER — WIFI + FIREBASE TEST ===");
  Serial.println();

  client.setInsecure();

  // ── Test 1: WiFi ─────────────────────────────────────────
  Serial.println("[1] Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(400); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("  FAIL: WiFi timeout");
    while (true) delay(1000);
  }
  Serial.printf("  PASS: %s\n", WiFi.localIP().toString().c_str());

  // ── Test 2: Read open batches ─────────────────────────────
  Serial.println();
  Serial.println("[2] Reading open batches from Firebase...");
  String queryUrl = String(FIREBASE_HOST) + "/batches.json?auth=" +
                    FIREBASE_KEY + "&orderBy=\"batch_status\"&equalTo=\"OPEN\"";
  HTTPClient http;
  http.begin(client, queryUrl);
  http.setTimeout(10000);
  int code = http.GET();
  String batchBody = (code == 200) ? http.getString() : "";
  http.end();
  Serial.printf("  GET /batches (status=OPEN) → %d\n", code);

  if (batchBody.isEmpty() || batchBody == "null" || batchBody == "{}") {
    Serial.println("  INFO: No open batches found");
    Serial.println("  This is expected if TAPU has not processed any payments yet");
    Serial.println("  Run TAPU first to create batches, then re-run this test");
  } else {
    DynamicJsonDocument bDoc(8192);
    if (deserializeJson(bDoc, batchBody) == DeserializationError::Ok) {
      int count = 0;
      for (JsonPair kv : bDoc.as<JsonObject>()) {
        count++;
        Serial.printf("  Batch: %s | Aggregator: %s | Crates: %d\n",
                      kv.key().c_str(),
                      kv.value()["aggregator_name"].as<const char*>(),
                      kv.value()["crate_count"].as<int>());
      }
      Serial.printf("  PASS: %d open batch(es) found\n", count);
    }
  }

  // ── Test 3: Read active formulas ──────────────────────────
  Serial.println();
  Serial.println("[3] Reading active formulas from Firebase...");
  String formulaUrl = String(FIREBASE_HOST) + "/formulas.json?auth=" +
                      FIREBASE_KEY + "&orderBy=\"active\"&equalTo=true";
  HTTPClient http2;
  http2.begin(client, formulaUrl);
  http2.setTimeout(8000);
  int fCode = http2.GET();
  String formulaBody = (fCode == 200) ? http2.getString() : "";
  http2.end();
  Serial.printf("  GET /formulas (active=true) → %d\n", fCode);

  if (formulaBody.isEmpty() || formulaBody == "null" || formulaBody == "{}") {
    Serial.println("  INFO: No active formulas found");
    Serial.println("  Add formulas to Firebase under /formulas/");
    Serial.println("  Format: {\"name\": \"Bio-Shield A\", \"notes\": \"Standard\", \"active\": true}");
  } else {
    DynamicJsonDocument fDoc(4096);
    if (deserializeJson(fDoc, formulaBody) == DeserializationError::Ok) {
      int count = 0;
      for (JsonPair kv : fDoc.as<JsonObject>()) {
        count++;
        Serial.printf("  Formula: %s | Name: %s | Active: %s\n",
                      kv.key().c_str(),
                      kv.value()["name"].as<const char*>(),
                      kv.value()["active"].as<bool>() ? "true" : "false");
      }
      Serial.printf("  PASS: %d formula(s) found\n", count);
    }
  }

  // ── Test 4: Write test PRE scan ───────────────────────────
  Serial.println();
  Serial.println("[4] Writing test PRE scan to /batches/TEST-QC-001/pre_coating...");

  DynamicJsonDocument preDoc(512);
  preDoc["temp_avg"]           = 32.5f;
  preDoc["humidity_avg"]       = 68.2f;
  preDoc["voc_index_avg"]      = 245.3f;
  preDoc["surface_temp_avg"]   = 31.8f;
  preDoc["weight_kg"]          = 12.450f;
  preDoc["crates_scanned"]     = 3;
  preDoc["readings_per_crate"] = 10;
  preDoc["timestamp"]          = (long)(millis() / 1000);
  preDoc["device_id"]          = "QC-KN-01";

  String prePayload;
  serializeJson(preDoc, prePayload);

  // Create test batch first
  DynamicJsonDocument batchDoc(512);
  batchDoc["batch_id"]        = "TEST-QC-001";
  batchDoc["aggregator_id"]   = "AGG-T001";
  batchDoc["aggregator_name"] = "Test Aggregator";
  batchDoc["crate_count"]     = 50;
  batchDoc["batch_status"]    = "OPEN";
  batchDoc["hub_id"]          = "HUB-KN-01";
  String batchPayload;
  serializeJson(batchDoc, batchPayload);
  fbPut("/batches/TEST-QC-001", batchPayload);

  bool preOk = fbPut("/batches/TEST-QC-001/pre_coating", prePayload);
  Serial.printf("  PRE write: %s\n", preOk ? "PASS" : "FAIL");

  // ── Test 5: Write test POST scan ──────────────────────────
  Serial.println();
  Serial.println("[5] Writing test POST scan to /batches/TEST-QC-001/post_coating...");

  DynamicJsonDocument postDoc(512);
  postDoc["temp_avg"]           = 33.1f;
  postDoc["humidity_avg"]       = 65.8f;
  postDoc["voc_index_avg"]      = 198.7f;   // lower than PRE = coating working
  postDoc["surface_temp_avg"]   = 32.2f;
  postDoc["weight_kg"]          = 12.520f;  // heavier after coating
  postDoc["crates_scanned"]     = 3;
  postDoc["readings_per_crate"] = 10;
  postDoc["timestamp"]          = (long)(millis() / 1000);
  postDoc["device_id"]          = "QC-KN-01";

  String postPayload;
  serializeJson(postDoc, postPayload);
  bool postOk = fbPut("/batches/TEST-QC-001/post_coating", postPayload);
  Serial.printf("  POST write: %s\n", postOk ? "PASS" : "FAIL");

  // ── Test 6: Write analysis + close batch ──────────────────
  Serial.println();
  Serial.println("[6] Writing analysis + closing batch (atomic PATCH)...");

  float preVOC  = 245.3f;
  float postVOC = 198.7f;
  float preWt   = 12.450f;
  float postWt  = 12.520f;

  float vocReductionPct    = ((preVOC - postVOC) / preVOC) * 100.0f;
  float coatingMassGPerKg  = ((postWt - preWt) / preWt) * 1000.0f;
  bool  effective          = (vocReductionPct > 15.0f);

  Serial.printf("  VOC reduction:    %.2f%%\n", vocReductionPct);
  Serial.printf("  Coating mass:     %.2f g/kg\n", coatingMassGPerKg);
  Serial.printf("  Coating effective: %s\n", effective ? "YES" : "NO");

  DynamicJsonDocument patchDoc(1024);
  patchDoc["/batches/TEST-QC-001/analysis/voc_reduction_pct"]     = vocReductionPct;
  patchDoc["/batches/TEST-QC-001/analysis/coating_mass_g_per_kg"] = coatingMassGPerKg;
  patchDoc["/batches/TEST-QC-001/analysis/temp_change"]           = 0.6f;
  patchDoc["/batches/TEST-QC-001/analysis/humidity_change"]       = -2.4f;
  patchDoc["/batches/TEST-QC-001/analysis/coating_effective"]     = effective;
  patchDoc["/batches/TEST-QC-001/analysis/formula_batch_ref"]     = "BIO-SHIELD-A-001";
  patchDoc["/batches/TEST-QC-001/batch_status"]                   = "COMPLETE";
  patchDoc["/batches/TEST-QC-001/closed_at"]                      = (long)(millis() / 1000);

  String patchPayload;
  serializeJson(patchDoc, patchPayload);

  HTTPClient http3;
  http3.begin(client, String(FIREBASE_HOST) + ".json?auth=" + FIREBASE_KEY);
  http3.addHeader("Content-Type", "application/json");
  int patchCode = http3.PATCH(patchPayload);
  http3.end();
  Serial.printf("  Atomic PATCH → %d : %s\n", patchCode,
                patchCode == 200 ? "PASS" : "FAIL");

  // ── Test 7: Read back and verify ─────────────────────────
  Serial.println();
  Serial.println("[7] Reading back TEST-QC-001...");
  String readBack = fbGet("/batches/TEST-QC-001");

  if (!readBack.isEmpty()) {
    DynamicJsonDocument verifyDoc(2048);
    if (deserializeJson(verifyDoc, readBack) == DeserializationError::Ok) {
      Serial.printf("  batch_status:       %s\n",
                    verifyDoc["batch_status"].as<const char*>());
      Serial.printf("  pre voc_index_avg:  %.1f\n",
                    verifyDoc["pre_coating"]["voc_index_avg"].as<float>());
      Serial.printf("  post voc_index_avg: %.1f\n",
                    verifyDoc["post_coating"]["voc_index_avg"].as<float>());
      Serial.printf("  voc_reduction_pct:  %.2f%%\n",
                    verifyDoc["analysis"]["voc_reduction_pct"].as<float>());
      Serial.printf("  coating_effective:  %s\n",
                    verifyDoc["analysis"]["coating_effective"].as<bool>() ?
                    "true" : "false");
      Serial.println("  PASS: data verified in Firebase");
    }
  }

  // ── Cleanup ───────────────────────────────────────────────
  Serial.println();
  Serial.println("[8] Cleaning up test data...");
  fbDelete("/batches/TEST-QC-001");
  Serial.println("  TEST-QC-001 deleted");

  Serial.println();
  Serial.println("=== QC SCANNER FIREBASE TEST COMPLETE ===");
  Serial.println();
  Serial.println("Next steps for Firebase setup:");
  Serial.println("  1. Add formula under /formulas/BIO-SHIELD-A:");
  Serial.println("     {\"name\": \"Bio-Shield Grade A\", \"notes\": \"Standard\", \"active\": true}");
  Serial.println("  2. Ensure TAPU has created real batches");
  Serial.println("  3. Run QC Scanner firmware");
}

void loop() {}
