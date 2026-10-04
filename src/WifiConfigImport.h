#pragma once

#include <string>

/**
 * Seeds WiFi credentials from a plaintext JSON file on the SD card root
 * (/wifi.json), so networks can be provisioned without on-device keyboard
 * entry. Accepts either a single object or an array of objects:
 *
 *   { "ssid": "MyNetwork", "password": "secret" }
 *   [ { "ssid": "A", "password": "x" }, { "ssid": "B", "password": "y" } ]
 *
 * Entries are merged into WifiCredentialStore (persisted, obfuscated on disk).
 * The first network is returned as the preferred auto-connect target. When the
 * device has no last-connected network yet, it is also persisted as the initial
 * target. Only changed credentials are written, so the file can remain on SD.
 *
 * Returns true if any credential was newly imported or updated. When provided,
 * preferredSsid receives the first network in the file for auto-connect.
 */
bool importWifiConfigFromFile(std::string* preferredSsid = nullptr);
