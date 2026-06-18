#include "ReadeckBrowserActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

namespace {
constexpr int PAGE_ITEMS = 23;
// Hold Confirm this long to bulk-download the whole list (vs a short tap to open
// one). Mirrors the reader's long-press conventions.
constexpr uint32_t BULK_HOLD_MS = 600;
}  // namespace

void ReadeckBrowserActivity::onEnter() {
  Activity::onEnter();

  state = State::CHECK_WIFI;
  articles.clear();
  selectorIndex = 0;
  openDownloadedBook = false;
  errorMessage.clear();

  if (!config.load()) {
    state = State::ERROR;
    errorMessage = "Missing or invalid /readeck.json";
    requestUpdate();
    return;
  }

  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void ReadeckBrowserActivity::onExit() {
  Activity::onExit();
  articles.clear();

  // Mirror the other WiFi activities: reboot on the way out to clear heap
  // fragmentation. After a successful download, route the reboot straight into
  // the reader on the freshly downloaded file (path persisted in openArticle).
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    if (openDownloadedBook) {
      silentRestartToReader();
    } else {
      silentRestart();
    }
  }
}

void ReadeckBrowserActivity::loop() {
  if (state == State::WIFI_SELECTION) return;

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = State::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchArticles();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::CHECK_WIFI || state == State::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onGoHome();
    return;
  }

  if (state == State::DOWNLOADING) return;

  if (state == State::BROWSING) {
    // Long-press Confirm bulk-downloads the whole list; a short tap opens one.
    // (Left/Right are navigation here, so the bulk action can't use a button.)
    if (!articles.empty() && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        mappedInput.getHeldTime() >= BULK_HOLD_MS) {
      downloadAll();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!articles.empty()) openArticle(articles[selectorIndex]);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }

    if (!articles.empty()) {
      const int count = static_cast<int>(articles.size());
      buttonNavigator.onNextRelease([this, count] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this, count] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this, count] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, count, PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this, count] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, count, PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void ReadeckBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Readeck", true, EpdFontFamily::BOLD);

  if (state == State::CHECK_WIFI || state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    if (bulkTotal > 0) {
      const std::string counter = std::to_string(bulkDone + 1) + " / " + std::to_string(bulkTotal);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 22, counter.c_str());
    }
    auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 4, title.c_str());
    if (downloadTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadProgress,
                          downloadTotal);
    }
    renderer.displayBuffer();
    return;
  }

  // BROWSING
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (articles.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No articles");
  } else {
    // Bulk download is long-press Confirm (documented in docs/readeck.md); no
    // on-screen hint — the title/list band is too tight for one to sit cleanly.
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (size_t i = pageStartIndex; i < articles.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& article = articles[i];
      std::string displayText = article.title;
      if (!article.siteName.empty()) displayText += " - " + article.siteName;
      auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void ReadeckBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchArticles();
    return;
  }
  launchWifiSelection();
}

void ReadeckBrowserActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ReadeckBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchArticles();
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = State::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

void ReadeckBrowserActivity::fetchArticles() {
  ReadeckClient client(config);
  if (!client.fetchArticles(articles)) {
    state = State::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  selectorIndex = 0;
  // Empty is not an error (nothing matched the label / nothing unread); BROWSING
  // renders a friendly "No articles" message.
  state = State::BROWSING;
  requestUpdate();
}

void ReadeckBrowserActivity::openArticle(const ReadeckBookmark& article) {
  state = State::DOWNLOADING;
  statusMessage = article.title;
  downloadProgress = downloadTotal = 0;
  requestUpdate(true);

  Storage.mkdir("/readeck");
  const std::string destPath = destPathFor(article);

  bool ok = true;
  if (!Storage.exists(destPath.c_str())) {
    ReadeckClient client(config);
    ok = client.downloadArticleEpub(article.id, destPath, [this](const size_t downloaded, const size_t total) {
      downloadProgress = downloaded;
      downloadTotal = total;
      requestUpdate(true);
    });
    if (ok) clearBookCache(destPath);
  }

  if (!ok) {
    state = State::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
    requestUpdate();
    return;
  }

  // Persist the target so the WiFi-teardown reboot in onExit() lands in the
  // reader on this file.
  APP_STATE.openEpubPath = destPath;
  APP_STATE.saveToFile();
  openDownloadedBook = true;
  activityManager.goToReader(destPath);
}

std::string ReadeckBrowserActivity::destPathFor(const ReadeckBookmark& article) {
  // Save-order filename: YYYY-MM-DD prefix (from the RFC3339 created date) keeps
  // re-reads ordered in the File Browser's natural sort. created may be short or
  // empty; substr(0, 10) is safe at pos 0 regardless.
  const std::string datePrefix = article.created.substr(0, 10);
  const std::string base = datePrefix.empty() ? article.title : (datePrefix + " " + article.title);
  return "/readeck/" + StringUtils::sanitizeFilename(base) + ".epub";
}

void ReadeckBrowserActivity::downloadAll() {
  Storage.mkdir("/readeck");
  state = State::DOWNLOADING;
  bulkTotal = static_cast<int>(articles.size());
  ReadeckClient client(config);

  // Sequential download of the whole list in one WiFi session; each EPUB streams
  // to SD so RAM stays flat. Files land in /readeck/ for offline reading via the
  // File Browser — no reader hand-off here.
  for (bulkDone = 0; bulkDone < bulkTotal; ++bulkDone) {
    const auto& article = articles[bulkDone];
    statusMessage = article.title;
    downloadProgress = downloadTotal = 0;
    requestUpdate(true);

    const std::string destPath = destPathFor(article);
    if (!Storage.exists(destPath.c_str())) {  // skip ones already downloaded
      const bool ok =
          client.downloadArticleEpub(article.id, destPath, [this](const size_t downloaded, const size_t total) {
            downloadProgress = downloaded;
            downloadTotal = total;
            requestUpdate(true);
          });
      if (ok) {
        clearBookCache(destPath);
      } else {
        LOG_ERR("RDK", "Bulk: download failed, skipping: %s", article.id.c_str());
      }
    }
    delay(1);  // yield between files so the long batch can't starve the watchdog
  }

  bulkTotal = 0;
  // Heap is fragmented from the WiFi session; exit home so onExit's silent reboot
  // clears it. Articles are now readable offline from the library.
  onGoHome();
}
