#include "WifiConfigImport.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <string>

#include "WifiCredentialStore.h"

namespace {
constexpr char WIFI_CONFIG_PATH[] = "/wifi.json";
}  // namespace

bool importWifiConfigFromFile() {
  if (!Storage.exists(WIFI_CONFIG_PATH)) return false;

  const String json = Storage.readFile(WIFI_CONFIG_PATH);
  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WIFI", "wifi.json parse error: %s", error.c_str());
    return false;
  }

  bool imported = false;
  std::string firstSsid;

  // Merge one {ssid, password} entry into the store, writing only on a change.
  auto processEntry = [&](JsonObject entry) {
    const std::string ssid = entry["ssid"] | std::string("");
    const std::string password = entry["password"] | std::string("");
    if (ssid.empty()) return;
    if (firstSsid.empty()) firstSsid = ssid;

    const auto* existing = WIFI_STORE.findCredential(ssid);
    if (!existing || existing->password != password) {
      WIFI_STORE.addCredential(ssid, password);  // persists (obfuscated)
      imported = true;
      LOG_INF("WIFI", "Imported network from wifi.json: %s", ssid.c_str());
    }
  };

  if (doc.is<JsonArray>()) {
    for (JsonObject entry : doc.as<JsonArray>()) processEntry(entry);
  } else if (doc.is<JsonObject>()) {
    processEntry(doc.as<JsonObject>());
  } else {
    LOG_ERR("WIFI", "wifi.json: expected an object or array");
    return false;
  }

  // Seed the auto-connect target only when none is set, so a fresh device
  // connects with no interaction without overriding a later manual choice.
  if (!firstSsid.empty() && WIFI_STORE.getLastConnectedSsid().empty()) {
    WIFI_STORE.setLastConnectedSsid(firstSsid);  // guarded internally; persists
  }

  return imported;
}
