#ifndef MONOLITH_ALERT_HPP
#define MONOLITH_ALERT_HPP

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "interfaces/sms_backend_if.hpp"

namespace monolith {
namespace alert {

//! \brief The alert manager
class alert_manager_c {
public:
  //! \brief Configuration
  struct configuration_c {
    uint64_t max_alert_sends{0};
    double alert_cooldown_seconds{30.0};
    monolith::sms_backend_if *sms_backend{nullptr};
  };

  //! \brief Construct the alert manager with a config
  alert_manager_c(configuration_c config);

  //! \brief Trigger an alert
  //! \param id The id of the alert
  //! \param message The message to send
  void trigger(const int id, const std::string message);

private:
  struct sent_s {
    std::chrono::time_point<std::chrono::steady_clock> last_send;
    uint64_t num_sends{0};
  };

  std::unordered_map<int, sent_s> _send_map;
  std::mutex _send_map_mutex;

  configuration_c _config;
  uint64_t _total_alerts_sent{0};
};

} // namespace alert
} // namespace monolith

#endif