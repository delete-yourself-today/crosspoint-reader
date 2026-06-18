#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "readeck/ReadeckClient.h"
#include "readeck/ReadeckConfig.h"
#include "util/ButtonNavigator.h"

/**
 * Browses unread Readeck articles (newest first) and, on selection, downloads
 * the chosen article's EPUB to /readeck/ and opens it in the reader.
 *
 * Modeled on OpdsBookBrowserActivity (same WiFi-connect flow + status states),
 * trimmed to barebones. Like the other WiFi activities it reboots in onExit() to
 * clear heap fragmentation; on a successful download it reboots straight into
 * the reader (silentRestartToReader) on the freshly downloaded file.
 */
class ReadeckBrowserActivity final : public Activity {
 public:
  enum class State { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR };

  explicit ReadeckBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Readeck", renderer, mappedInput), buttonNavigator() {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  State state = State::CHECK_WIFI;
  ReadeckConfig config;
  std::vector<ReadeckBookmark> articles;
  int selectorIndex = 0;
  std::string statusMessage;
  std::string errorMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  int bulkDone = 0;  // >0 total => bulk download in progress (for the count UI)
  int bulkTotal = 0;
  bool openDownloadedBook = false;  // onExit routes the reboot to the reader when true

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchArticles();
  void openArticle(const ReadeckBookmark& article);
  void downloadAll();
  static std::string destPathFor(const ReadeckBookmark& article);
  bool preventAutoSleep() override { return true; }
};
