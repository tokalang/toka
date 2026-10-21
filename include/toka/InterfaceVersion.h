#pragma once

#include <string>
#include <utility>

#define TOKA_INTERFACE_FORMAT_VERSION "6"
#define TOKA_COMPILER_INTERFACE_VERSION "0.9.9-38"
#define TOKA_LOCAL_BODY_POLICY "checked-local-v1"
#define TOKA_PLACE_YIELD_ABI_SCHEMA_VERSION "1"
#define TOKA_DEFAULT_ENCAP_EPOCH "0.19"

namespace toka {

inline std::string &mutableActiveEncapEpoch() {
  static std::string epoch = TOKA_DEFAULT_ENCAP_EPOCH;
  return epoch;
}

inline const std::string &getActiveEncapEpoch() {
  return mutableActiveEncapEpoch();
}

inline bool isValidEncapEpoch(const std::string &epoch) {
  return epoch == "0.18" || epoch == "0.19";
}

inline void setActiveEncapEpoch(std::string epoch) {
  mutableActiveEncapEpoch() = std::move(epoch);
}

inline std::string
formatReplaySurfaceHashInput(const std::string &declarations,
                             const std::string &epoch = getActiveEncapEpoch()) {
  if (epoch.empty() || epoch == "0.18")
    return declarations;
  return "// @encap_epoch " + epoch + "\n" + declarations;
}

} // namespace toka
