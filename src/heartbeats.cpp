#include "heartbeats.hpp"
#include <chrono>

namespace monolith {

namespace {
int64_t stamp() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
} // namespace

void heartbeats_c::submit(std::string id) {
  const std::lock_guard<std::mutex> lock(_heartbeat_mutex);
  _heartbeats[id] = stamp();
}

std::optional<uint64_t> heartbeats_c::sec_since_contact(std::string id) {

  auto now = stamp();
  int64_t value = 0;

  {
    const std::lock_guard<std::mutex> lock(_heartbeat_mutex);

    if (!_heartbeats.contains(id)) {
      return {};
    }

    value = _heartbeats[id];
  }

  if (value > now || value == 0) {
    return {};
  }

  return now - value;
}

} // namespace monolith