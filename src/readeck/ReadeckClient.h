#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ReadeckConfig.h"

/**
 * Minimal view of a Readeck bookmark, holding only the fields the UI needs.
 * (The API's bookmarkSummary has many more; we filter the rest out at parse
 * time to keep the JsonDocument small.)
 */
struct ReadeckBookmark {
  std::string id;        // short-uid, used to build the export URL
  std::string title;     // article title
  std::string siteName;  // source site
  std::string created;   // RFC3339 timestamp, used for save-order filenames
  uint8_t readProgress = 0;
};

/**
 * Barebones, read-only Readeck REST client. On-demand: list unread articles,
 * download one as EPUB. No write-back (no mark-as-read). Built on
 * HttpDownloader's Bearer-token variants.
 */
class ReadeckClient {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

  explicit ReadeckClient(ReadeckConfig config) : config(std::move(config)) {}

  // GET /api/bookmarks?type=article&sort=-created (newest first), scoped to the
  // configured label if set, otherwise to unread articles. Returns false (and
  // LOG_ERR) on network or parse failure.
  bool fetchArticles(std::vector<ReadeckBookmark>& out, int limit = 20, int offset = 0);

  // GET /api/bookmarks/{id}/article.epub -> destPath on the SD card.
  bool downloadArticleEpub(const std::string& id, const std::string& destPath, const ProgressCallback& progress);

 private:
  ReadeckConfig config;
};
