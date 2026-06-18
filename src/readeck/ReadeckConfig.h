#pragma once
#include <string>

/**
 * Readeck connection settings, loaded from a plaintext JSON file on the SD card
 * (CONFIG_PATH). On-device keyboard entry is poor, so credentials are supplied
 * by dropping a file on the card:
 *
 *   { "url": "https://readeck.example", "token": "<api-token>", "label": "ereader" }
 *
 * The token is the long-lived API token from Readeck's /profile/tokens page.
 * `label` is optional: when set, the device shows and bulk-downloads only
 * articles carrying that Readeck label (curate on the web UI, pull on-device);
 * when empty, it shows unread articles.
 */
struct ReadeckConfig {
  std::string baseUrl;  // normalized: no trailing slash; client appends /api/...
  std::string token;
  std::string label;  // optional; empty => unread articles

  // Reads and parses CONFIG_PATH. Returns false (and LOG_ERR) if the file is
  // missing, malformed, or either field is empty.
  bool load();

  // Cheap presence check used to gate the Readeck menu entry. Does not validate
  // contents — load() does that.
  static bool isConfigured();

  static constexpr const char* CONFIG_PATH = "/readeck.json";
};
