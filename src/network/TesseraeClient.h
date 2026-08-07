#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class TesseraeClient {
 public:
  enum class Result {
    Ok,
    WaitingApproval,
    NotConfigured,
    NoWifi,
    NetworkError,
    Unauthorized,
    NoFrame,
    InvalidResponse,
    DownloadFailed,
  };

  struct FrameInfo {
    std::string url;
    uint16_t panelWidth = 0;
    uint16_t panelHeight = 0;
  };

  static bool normalizeServerUrl(const std::string& input, std::string& normalized);
  static bool isConfigured();
  static bool isRegistered();
  static std::string resolvedDeviceId();

  // Sleep-screen networking uses only the last saved network and a short
  // deadline. Interactive setup uses WifiSelectionActivity instead.
  static Result connectSavedWifi(uint32_t timeoutMs = 7000);

  static Result discover(uint16_t panelWidth, uint16_t panelHeight);
  static Result registerWithCode(const std::string& pairingCode, uint16_t panelWidth, uint16_t panelHeight);
  static Result fetchFrame(FrameInfo& frame);
  static Result downloadFrame(const FrameInfo& frame, const std::string& destination, size_t expectedBytes);
  static Result postStatus(uint16_t panelWidth, uint16_t panelHeight);

  static void clearRegistration();
  static const char* resultName(Result result);
};
