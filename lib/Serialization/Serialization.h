#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <iostream>

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

inline void readString(HalFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);

  // len is untrusted: it comes off the SD cache, and readPod cannot report a
  // short read, so at EOF len keeps whatever was on the stack. resize() uses a
  // throwing operator new, which under -fno-exceptions aborts the firmware
  // instead of failing -- so a truncated or corrupt cache file would take the
  // device down rather than being rejected. Nothing can legitimately be longer
  // than the bytes left in the file; yield an empty string for anything that is
  // and let the caller's own validation reject the record.
  // Both are size_t. SdFat's seekSet refuses to move past EOF for a regular
  // file, so position() <= size() holds today -- but this clamp exists precisely
  // because callers reach here with offsets from corrupt data, so do not lean on
  // that invariant: an underflow here would wrap `remaining` huge and make the
  // check below silently inert.
  const size_t pos = file.position();
  const size_t total = file.size();
  if (pos >= total) {
    s.clear();
    return;
  }

  const size_t remaining = total - pos;
  if (len > remaining) {
    LOG_ERR("SER", "String length %lu exceeds %u bytes left in file", static_cast<unsigned long>(len),
            static_cast<unsigned>(remaining));
    s.clear();
    return;
  }

  s.resize(len);
  file.read(&s[0], len);
}
}  // namespace serialization
