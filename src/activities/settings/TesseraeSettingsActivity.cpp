#include "TesseraeSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include "AlmanacSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void copySetting(char* destination, const size_t capacity, const std::string& value) {
  const size_t count = std::min(capacity - 1, value.size());
  memcpy(destination, value.data(), count);
  destination[count] = '\0';
}

bool validDeviceId(const std::string& value) {
  if (value.size() > 32) return false;
  return std::all_of(value.begin(), value.end(),
                     [](const unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
}

bool validPairingCode(const std::string& value) {
  return value.size() == 6 &&
         std::all_of(value.begin(), value.end(), [](const unsigned char c) { return std::isdigit(c); });
}
}  // namespace

void TesseraeSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  state = State::Menu;
  pairingCode.clear();
  message.clear();
  menuError.clear();
  requestUpdate();
}

void TesseraeSettingsActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
}

void TesseraeSettingsActivity::loop() {
  if (state == State::Connecting) {
    // Put the progress screen on glass before the blocking LAN request.
    requestUpdateAndWait();
    performConnection();
    return;
  }

  if (state != State::Menu) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      state = State::Menu;
      message.clear();
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, ITEM_COUNT, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
}

void TesseraeSettingsActivity::handleSelection() {
  menuError.clear();
  if (selectedIndex == 0) {
    const std::string prefill = SETTINGS.tesseraeServerUrl[0] ? SETTINGS.tesseraeServerUrl : "http://";
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TESSERAE_SERVER_URL), prefill,
                                                sizeof(SETTINGS.tesseraeServerUrl) - 1, InputType::Url),
        [this](const ActivityResult& result) {
          if (result.isCancelled) return;
          const std::string& entered = std::get<KeyboardResult>(result.data).text;
          std::string normalized;
          if (!TesseraeClient::normalizeServerUrl(entered, normalized)) {
            menuError = tr(STR_TESSERAE_INVALID_URL);
            return;
          }
          if (normalized != SETTINGS.tesseraeServerUrl) {
            copySetting(SETTINGS.tesseraeServerUrl, sizeof(SETTINGS.tesseraeServerUrl), normalized);
            TesseraeClient::clearRegistration();
            SETTINGS.saveToFile();
          }
        });
    return;
  }

  if (selectedIndex == 1) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TESSERAE_DEVICE_ID),
                                                std::string(SETTINGS.tesseraeDeviceId),
                                                sizeof(SETTINGS.tesseraeDeviceId) - 1, InputType::Text),
        [this](const ActivityResult& result) {
          if (result.isCancelled) return;
          const std::string& entered = std::get<KeyboardResult>(result.data).text;
          if (!validDeviceId(entered)) {
            menuError = tr(STR_TESSERAE_INVALID_DEVICE_ID);
            return;
          }
          if (entered != SETTINGS.tesseraeDeviceId) {
            copySetting(SETTINGS.tesseraeDeviceId, sizeof(SETTINGS.tesseraeDeviceId), entered);
            TesseraeClient::clearRegistration();
            SETTINGS.saveToFile();
          }
        });
    return;
  }

  if (selectedIndex == 2) {
    SETTINGS.tesseraeGrayscale = !SETTINGS.tesseraeGrayscale;
    TesseraeClient::clearRegistration();
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedIndex == 3) {
    if (TesseraeClient::isRegistered()) {
      TesseraeClient::clearRegistration();
      requestUpdate();
    } else {
      beginConnection("");
    }
    return;
  }

  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TESSERAE_PAIRING_CODE),
                                                                 "", 6, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const std::string& entered = std::get<KeyboardResult>(result.data).text;
                           if (!validPairingCode(entered)) {
                             menuError = tr(STR_TESSERAE_INVALID_PAIRING_CODE);
                             return;
                           }
                           beginConnection(entered);
                         });
}

void TesseraeSettingsActivity::beginConnection(std::string code) {
  if (!TesseraeClient::isConfigured()) {
    menuError = tr(STR_TESSERAE_SET_SERVER_FIRST);
    requestUpdate();
    return;
  }
  pairingCode = std::move(code);
  if (WiFi.status() == WL_CONNECTED) {
    state = State::Connecting;
    message = tr(STR_TESSERAE_CONNECTING);
    requestUpdate();
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void TesseraeSettingsActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    state = State::Failed;
    message = tr(STR_WIFI_CONN_FAILED);
  } else {
    state = State::Connecting;
    message = tr(STR_TESSERAE_CONNECTING);
  }
  requestUpdate();
}

void TesseraeSettingsActivity::performConnection() {
  const uint16_t panelWidth = static_cast<uint16_t>(renderer.getScreenWidth());
  const uint16_t panelHeight = static_cast<uint16_t>(renderer.getScreenHeight());
  const TesseraeClient::Result result = pairingCode.empty()
                                            ? TesseraeClient::discover(panelWidth, panelHeight)
                                            : TesseraeClient::registerWithCode(pairingCode, panelWidth, panelHeight);
  pairingCode.clear();
  showResult(result);
}

void TesseraeSettingsActivity::showResult(const TesseraeClient::Result result) {
  LOG_DBG("TESS", "Setup result: %s", TesseraeClient::resultName(result));
  switch (result) {
    case TesseraeClient::Result::Ok:
      state = State::Success;
      message = tr(STR_TESSERAE_CONNECTED);
      break;
    case TesseraeClient::Result::WaitingApproval:
      state = State::WaitingApproval;
      message = tr(STR_TESSERAE_WAITING_APPROVAL);
      break;
    case TesseraeClient::Result::NoWifi:
      state = State::Failed;
      message = tr(STR_WIFI_CONN_FAILED);
      break;
    case TesseraeClient::Result::NotConfigured:
      state = State::Failed;
      message = tr(STR_TESSERAE_SET_SERVER_FIRST);
      break;
    default:
      state = State::Failed;
      message = tr(STR_TESSERAE_CONNECTION_FAILED);
      break;
  }
  requestUpdate();
}

void TesseraeSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TESSERAE),
                 state == State::Menu && !menuError.empty() ? menuError.c_str() : nullptr);

  if (state == State::Menu) {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex,
        [](const int index) -> std::string {
          switch (index) {
            case 0:
              return I18N.get(StrId::STR_TESSERAE_SERVER_URL);
            case 1:
              return I18N.get(StrId::STR_TESSERAE_DEVICE_ID);
            case 2:
              return I18N.get(StrId::STR_TESSERAE_FRAME_MODE);
            case 3:
              return I18N.get(StrId::STR_TESSERAE_CONNECTION);
            default:
              return I18N.get(StrId::STR_TESSERAE_PAIRING_CODE);
          }
        },
        nullptr, nullptr,
        [](const int index) -> std::string {
          switch (index) {
            case 0:
              return SETTINGS.tesseraeServerUrl[0] ? SETTINGS.tesseraeServerUrl : I18N.get(StrId::STR_NOT_SET);
            case 1:
              return SETTINGS.tesseraeDeviceId[0] ? SETTINGS.tesseraeDeviceId : I18N.get(StrId::STR_TESSERAE_AUTOMATIC);
            case 2:
              return SETTINGS.tesseraeGrayscale ? I18N.get(StrId::STR_TESSERAE_GRAYSCALE)
                                                : I18N.get(StrId::STR_TESSERAE_MONOCHROME);
            case 3:
              return TesseraeClient::isRegistered() ? I18N.get(StrId::STR_TESSERAE_DISCONNECT)
                                                    : I18N.get(StrId::STR_TESSERAE_CONNECT);
            default:
              return I18N.get(StrId::STR_TESSERAE_PAIR_WITH_CODE);
          }
        });
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
    const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing * 3;
    UITheme::drawCenteredWrappedText(
        renderer, Rect{metrics.contentSidePadding, top, pageWidth - metrics.contentSidePadding * 2, height},
        UI_10_FONT_ID, message.c_str(), 4);
    if (state != State::Connecting) {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }
  renderer.displayBuffer();
}
