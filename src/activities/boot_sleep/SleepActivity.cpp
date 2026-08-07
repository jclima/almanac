#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <TesseraeFrame.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>

#include "AlmanacSettings.h"
#include "AlmanacState.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "network/TesseraeClient.h"

namespace {
constexpr char TESSERAE_FRAME_FILE[] = "/.crosspoint/tesserae_frame.bin";
constexpr size_t TESSERAE_READ_CHUNK = 1024;

bool loadTesseraeGrayPlane(HalFile& file, uint8_t* destination, const size_t destinationSize,
                           const tesserae::GrayPlane plane) {
  if (!file.seek(0)) return false;

  uint8_t input[TESSERAE_READ_CHUNK];
  size_t written = 0;
  while (written < destinationSize) {
    const size_t remainingInput = (destinationSize - written) * 2;
    const size_t requested = std::min(sizeof(input), remainingInput);
    const int bytesRead = file.read(input, requested);
    if (bytesRead != static_cast<int>(requested) || (bytesRead & 1) != 0) return false;
    for (int i = 0; i < bytesRead; i += 2) {
      destination[written++] = tesserae::expandGray2Pair(input[i], input[i + 1], plane);
    }
  }
  return written == destinationSize;
}
}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == AlmanacSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == AlmanacSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (AlmanacSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (AlmanacSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (AlmanacSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (AlmanacSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (AlmanacSettings::SLEEP_SCREEN_MODE::TESSERAE):
      if (renderTesseraeSleepScreen()) return;
      LOG_DBG("TESS", "Falling back to the default sleep screen");
      return renderDefaultSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      if (dir) dir.close();
      return;
    }
    file.close();
  }

  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    std::vector<std::string> files;
    char name[500];
    // collect all valid BMP files
    for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
      if (dirFile.isDirectory()) {
        dirFile.close();
        continue;
      }
      dirFile.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        dirFile.close();
        continue;
      }

      if (!FsHelpers::hasBmpExtension(filename)) {
        LOG_DBG("SLP", "Skipping non-.bmp file name: %s", name);
        dirFile.close();
        continue;
      }
      Bitmap bitmap(dirFile);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
        dirFile.close();
        continue;
      }
      files.emplace_back(filename);
      dirFile.close();
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Pick a random wallpaper, excluding recently shown ones.
      // Window: up to SLEEP_RECENT_COUNT entries, capped at numFiles-1.
      const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
      auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
      for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(randomFileIndex, window); attempt++) {
        randomFileIndex = static_cast<uint16_t>(random(fileCount));
      }
      APP_STATE.pushRecentSleep(randomFileIndex);
      APP_STATE.saveToFile();
      const auto filename = std::string(sleepDir) + "/" + files[randomFileIndex];
      HalFile randFile;
      if (Storage.openFileForRead("SLP", filename, randFile)) {
        LOG_DBG("SLP", "Randomly loading: %s/%s", sleepDir, files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(randFile, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          randFile.close();
          dir.close();
          return;
        }
        randFile.close();
      }
    }
  }
  if (dir) dir.close();

  renderDefaultSleepScreen();
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_APP_NAME), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != AlmanacSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == AlmanacSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == AlmanacSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() && SETTINGS.sleepScreenCoverFilter == AlmanacSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == AlmanacSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (AlmanacSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == AlmanacSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool SleepActivity::renderTesseraeSleepScreen() const {
  if (!gpio.isXteinkDevice() || !TesseraeClient::isConfigured()) return false;

  TesseraeClient::Result result = TesseraeClient::connectSavedWifi();
  if (result != TesseraeClient::Result::Ok) {
    LOG_ERR("TESS", "Wi-Fi unavailable: %s", TesseraeClient::resultName(result));
    return false;
  }

  const uint16_t panelWidth = static_cast<uint16_t>(renderer.getScreenWidth());
  const uint16_t panelHeight = static_cast<uint16_t>(renderer.getScreenHeight());
  if (!TesseraeClient::isRegistered()) {
    result = TesseraeClient::discover(panelWidth, panelHeight);
    if (result != TesseraeClient::Result::Ok) {
      LOG_DBG("TESS", "Registration unavailable: %s", TesseraeClient::resultName(result));
      return false;
    }
  }

  TesseraeClient::FrameInfo frame;
  result = TesseraeClient::fetchFrame(frame);
  if (result != TesseraeClient::Result::Ok) {
    LOG_ERR("TESS", "Frame metadata unavailable: %s", TesseraeClient::resultName(result));
    return false;
  }
  if (frame.panelWidth != panelWidth || frame.panelHeight != panelHeight) {
    LOG_ERR("TESS", "Frame dimensions %ux%u do not match panel %ux%u", frame.panelWidth, frame.panelHeight, panelWidth,
            panelHeight);
    TesseraeClient::clearRegistration();
    return false;
  }

  const size_t monoSize = renderer.getBufferSize();
  const size_t expectedFrameSize = tesserae::encodedFrameSize(
      monoSize, SETTINGS.tesseraeGrayscale ? tesserae::FrameDepth::Gray2 : tesserae::FrameDepth::Mono1);
  result = TesseraeClient::downloadFrame(frame, TESSERAE_FRAME_FILE, expectedFrameSize);
  if (result != TesseraeClient::Result::Ok) {
    LOG_ERR("TESS", "Frame download failed: %s", TesseraeClient::resultName(result));
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("TESS", TESSERAE_FRAME_FILE, file)) {
    Storage.remove(TESSERAE_FRAME_FILE);
    return false;
  }

  const size_t frameSize = file.size();
  const bool isMono = frameSize == tesserae::encodedFrameSize(monoSize, tesserae::FrameDepth::Mono1);
  const bool isGray = frameSize == tesserae::encodedFrameSize(monoSize, tesserae::FrameDepth::Gray2);
  const bool expectedDepth = SETTINGS.tesseraeGrayscale ? isGray : isMono;
  if (!expectedDepth) {
    LOG_ERR("TESS", "Unexpected frame size: %zu (expected %zu)", frameSize, expectedFrameSize);
    file.close();
    Storage.remove(TESSERAE_FRAME_FILE);
    TesseraeClient::clearRegistration();
    return false;
  }

  // The reference client reports telemetry while the radio is still up and
  // before the slow e-ink paint. A failed heartbeat must not discard a frame
  // that was already downloaded and validated.
  const auto statusResult = TesseraeClient::postStatus(panelWidth, panelHeight);
  if (statusResult != TesseraeClient::Result::Ok) {
    LOG_DBG("TESS", "Status heartbeat failed: %s", TesseraeClient::resultName(statusResult));
  }

  bool rendered = false;
  if (isMono) {
    rendered = file.read(renderer.getFrameBuffer(), monoSize) == static_cast<int>(monoSize);
    file.close();
    Storage.remove(TESSERAE_FRAME_FILE);
    if (rendered) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return rendered;
  }

  uint8_t* const buffer = renderer.getFrameBuffer();
  if (!loadTesseraeGrayPlane(file, buffer, monoSize, tesserae::GrayPlane::Base)) {
    file.close();
    Storage.remove(TESSERAE_FRAME_FILE);
    return false;
  }
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  renderer.preconditionGrayscale();  // no-op on X4; required by X3's gray waveform

  if (loadTesseraeGrayPlane(file, buffer, monoSize, tesserae::GrayPlane::Lsb)) {
    renderer.copyGrayscaleLsbBuffers();
    if (loadTesseraeGrayPlane(file, buffer, monoSize, tesserae::GrayPlane::Msb)) {
      renderer.copyGrayscaleMsbBuffers();
      renderer.displayGrayBuffer();
      renderer.setRenderMode(GfxRenderer::BW);
      rendered = true;
    }
  }

  file.close();
  Storage.remove(TESSERAE_FRAME_FILE);
  return rendered;
}
