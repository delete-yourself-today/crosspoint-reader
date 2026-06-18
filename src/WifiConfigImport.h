#pragma once

/**
 * Seeds WiFi credentials from a plaintext JSON file on the SD card root
 * (/wifi.json), so networks can be provisioned without on-device keyboard
 * entry. Accepts either a single object or an array of objects:
 *
 *   { "ssid": "MyNetwork", "password": "secret" }
 *   [ { "ssid": "A", "password": "x" }, { "ssid": "B", "password": "y" } ]
 *
 * Entries are merged into WifiCredentialStore (persisted, obfuscated on disk).
 * When the device has no last-connected network yet, the first imported network
 * becomes the auto-connect target, so a freshly seeded device connects with no
 * interaction. Idempotent: only writes when a credential is new or changed, so
 * it is safe to leave the file in place across boots.
 *
 * Returns true if any credential was newly imported or updated.
 */
bool importWifiConfigFromFile();
