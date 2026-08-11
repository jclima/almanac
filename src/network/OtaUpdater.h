#pragma once

// sdkconfig.h is ESP-IDF-only; it supplies CONFIG_IDF_TARGET_ESP32C3 for
// releaseBinaryRunsOnThisDevice below. The host simulator has no ESP-IDF, and
// leaving the macro undefined there is the correct answer anyway: no published
// firmware.bin runs on the host.
#ifndef SIMULATOR
#include <sdkconfig.h>
#endif

#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  // Whether a release carries a binary this device can run.
  //
  // Releases publish exactly one firmware.bin, built by [env:gh_release],
  // which targets the ESP32-C3 (its ESP image header carries chip_id 5). The
  // Sticky is an ESP32-S3 and has no published binary -- it is built from
  // source -- so it must never be offered one.
  //
  // Without this, a Sticky would download 5.5MB only for esp_ota_end() to
  // reject the image: esp_image_verify() checks the header's chip_id against
  // the running build's CONFIG_IDF_FIRMWARE_CHIP_ID, so the wrong-MCU image
  // is refused before esp_ota_set_boot_partition() and the device is never at
  // risk. The cost is the wasted transfer and a generic failure screen, not a
  // bad flash.
  //
  // Keyed on the MCU rather than on FREEINK_DEVICE_STICKY so any future
  // non-C3 target inherits the guard. constexpr, so the check-for-update path
  // compiles out entirely on those builds.
  static constexpr bool releaseBinaryRunsOnThisDevice =
#if defined(CONFIG_IDF_TARGET_ESP32C3)
      true;
#else
      false;
#endif

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
