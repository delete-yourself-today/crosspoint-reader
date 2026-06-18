#include "ReadeckConfig.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

bool ReadeckConfig::isConfigured() { return Storage.exists(CONFIG_PATH); }

bool ReadeckConfig::load() {
  if (!Storage.exists(CONFIG_PATH)) {
    LOG_ERR("RDK", "Config not found: %s", CONFIG_PATH);
    return false;
  }

  // The config file is tiny; readFile into a String is fine here.
  const String json = Storage.readFile(CONFIG_PATH);

  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RDK", "Config parse error: %s", error.c_str());
    return false;
  }

  baseUrl = doc["url"] | std::string("");
  token = doc["token"] | std::string("");
  label = doc["label"] | std::string("");

  // Strip trailing slashes so the client can append "/api/..." cleanly.
  while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

  if (baseUrl.empty() || token.empty()) {
    LOG_ERR("RDK", "Config missing url or token");
    return false;
  }
  return true;
}
