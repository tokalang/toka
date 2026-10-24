#pragma once

#include <string>
#include <utility>

#define TOKA_INTERFACE_FORMAT_VERSION "7"
#define TOKA_COMPILER_INTERFACE_VERSION "0.20.1-1"
#define TOKA_LOCAL_BODY_POLICY "checked-local-v1-0201"
#define TOKA_PLACE_YIELD_ABI_SCHEMA_VERSION "1"
#define TOKA_DEFAULT_LANGUAGE_EPOCH "0.20"
#define TOKA_DEFAULT_ENCAP_EPOCH "0.19"

namespace toka {

inline std::string &mutableActiveLanguageEpoch() {
  static std::string epoch = TOKA_DEFAULT_LANGUAGE_EPOCH;
  return epoch;
}

inline const std::string &getActiveLanguageEpoch() {
  return mutableActiveLanguageEpoch();
}

inline bool isValidLanguageEpoch(const std::string &epoch) {
  return epoch == "0.20";
}

inline void setActiveLanguageEpoch(std::string epoch) {
  mutableActiveLanguageEpoch() = std::move(epoch);
}

inline std::string &mutableActiveEncapEpoch() {
  static std::string epoch = TOKA_DEFAULT_ENCAP_EPOCH;
  return epoch;
}

inline const std::string &getActiveEncapEpoch() {
  return mutableActiveEncapEpoch();
}

inline bool isValidEncapEpoch(const std::string &epoch) {
  return epoch == "0.19";
}

inline void setActiveEncapEpoch(std::string epoch) {
  mutableActiveEncapEpoch() = std::move(epoch);
}

inline std::string
formatReplaySurfaceHashInput(const std::string &declarations,
                             const std::string &languageEpoch,
                             const std::string &encapEpoch = getActiveEncapEpoch()) {
  return "// @language_epoch " + languageEpoch + "\n// @encap_epoch " +
         encapEpoch + "\n" + declarations;
}

inline std::string
formatReplaySurfaceHashInput(const std::string &declarations) {
  return formatReplaySurfaceHashInput(declarations, getActiveLanguageEpoch(),
                                      getActiveEncapEpoch());
}

} // namespace toka
