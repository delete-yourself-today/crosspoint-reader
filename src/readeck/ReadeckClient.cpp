#include "ReadeckClient.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>

#include "network/HttpDownloader.h"

namespace {
// Temp file for the bookmark list response. The full summaries can exceed free
// heap while TLS is up, so we never buffer the body in RAM (see below).
constexpr char LIST_TMP_PATH[] = "/.crosspoint/readeck_list.json";

// Percent-encode a query-parameter value (labels may contain spaces, etc.).
std::string urlEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}
}  // namespace

bool ReadeckClient::fetchArticles(std::vector<ReadeckBookmark>& out, int limit, int offset) {
  out.clear();

  // Scope to the configured label if set (curated-for-device set), else unread.
  char query[224];
  if (config.label.empty()) {
    snprintf(query, sizeof(query), "/api/bookmarks?read_status=unread&type=article&sort=-created&limit=%d&offset=%d",
             limit, offset);
  } else {
    snprintf(query, sizeof(query), "/api/bookmarks?labels=%s&type=article&sort=-created&limit=%d&offset=%d",
             urlEncode(config.label).c_str(), limit, offset);
  }
  const std::string url = config.baseUrl + query;

  // Stream the response to a temp file rather than buffering it in RAM: the
  // bookmark list (full summaries: resources, labels, description...) can exceed
  // free heap, especially with TLS buffers up. Downloading to SD closes the HTTP
  // client first (freeing TLS heap), then we parse from the file with a filter so
  // the document stays tiny and the body is never wholly resident. Mirrors
  // FontDownloadActivity's manifest fetch.
  Storage.mkdir("/.crosspoint");
  if (HttpDownloader::downloadToFileBearer(url, LIST_TMP_PATH, config.token) != HttpDownloader::OK) {
    LOG_ERR("RDK", "Bookmark list request failed");
    Storage.remove(LIST_TMP_PATH);
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("RDK", LIST_TMP_PATH, file)) {
    LOG_ERR("RDK", "Failed to open bookmark list temp file");
    Storage.remove(LIST_TMP_PATH);
    return false;
  }

  // Filter so the JsonDocument only retains the handful of fields we read,
  // regardless of how much the server returns per item. For a JSON array the
  // filter is an array holding one object template.
  JsonDocument filter;
  JsonObject elem = filter.add<JsonObject>();
  elem["id"] = true;
  elem["title"] = true;
  elem["site_name"] = true;
  elem["created"] = true;
  elem["read_progress"] = true;
  elem["has_article"] = true;

  JsonDocument doc;
  const auto error = deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();
  Storage.remove(LIST_TMP_PATH);
  if (error) {
    LOG_ERR("RDK", "Bookmark list parse error: %s", error.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  out.reserve(arr.size());
  for (JsonObject item : arr) {
    // Only articles with an extracted body can be exported to EPUB.
    if (!(item["has_article"] | false)) continue;

    ReadeckBookmark b;
    b.id = item["id"] | std::string("");
    if (b.id.empty()) continue;
    b.title = item["title"] | std::string("");
    b.siteName = item["site_name"] | std::string("");
    b.created = item["created"] | std::string("");
    b.readProgress = static_cast<uint8_t>(item["read_progress"] | 0);
    out.push_back(std::move(b));
  }

  LOG_DBG("RDK", "Fetched %u unread article(s)", (unsigned)out.size());
  return true;
}

bool ReadeckClient::downloadArticleEpub(const std::string& id, const std::string& destPath,
                                        const ProgressCallback& progress) {
  const std::string url = config.baseUrl + "/api/bookmarks/" + id + "/article.epub";
  const auto result = HttpDownloader::downloadToFileBearer(url, destPath, config.token, progress);
  if (result != HttpDownloader::OK) {
    LOG_ERR("RDK", "EPUB download failed (%d): %s", result, id.c_str());
    return false;
  }
  return true;
}
