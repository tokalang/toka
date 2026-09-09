#pragma once
#include <string>

namespace toka {
// A type-level prerequisite only. Closed does NOT create an initialized slot,
// native resource, owner/guard relation or thread-publication authority.
enum class NativeClosedPayloadState { Closed, ExternalDependency, Incomplete };
struct NativeClosedPayloadResult {
  NativeClosedPayloadState State = NativeClosedPayloadState::Incomplete;
  std::string Path;
  std::string Reason;
  bool closed() const { return State == NativeClosedPayloadState::Closed; }
};
}
