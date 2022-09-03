#include "alert.hpp"

#include <crate/externals/aixlog/logger.hpp>

namespace monolith {
namespace alert {

alert_manager_c::alert_manager_c(alert_manager_c::configuration_c config) : _config(config) {}

void alert_manager_c::trigger(const int id, const std::string message) {

   LOG(INFO) << TAG("alert_manager_c::trigger") << "Alert ID: " << id << " => " << message << "\n";

   uint64_t num_sends {0};

   {
      const std::lock_guard<std::mutex> lock(_send_map_mutex);
      auto it = _send_map.find(id);

      // If it has been sent before we need to check it
      if (it != _send_map.end()) {
         auto now = std::chrono::steady_clock::now();
         std::chrono::duration<double> diff = now - it->second.last_send;
         if (diff.count() <= _config.alert_cooldown_seconds) {
            LOG(INFO) << TAG("alert_manager_c::trigger") 
                        << "Actively limiting alert for : " 
                        << id 
                        << ". Seconds left on limiter: " 
                        << _config.alert_cooldown_seconds - diff.count() 
                        << "s \n";
            return;
         }

         // We are going to try sending so lets mark the num sends
         num_sends = it->second.num_sends + 1;
      }
   }

   if (_config.max_alert_sends != 0) {
      if (_total_alerts_sent++ >= _config.max_alert_sends) {
         LOG(INFO) << TAG("alert_manager_c::trigger") 
                     << "Maximum number of alerts (" 
                     << _config.max_alert_sends 
                     << ") has been reached\n";
         return;
      }
   }

   if (_config.sms_backend) {
      if (!_config.sms_backend->send_message(message)) {
         LOG(INFO) << TAG("alert_manager_c::trigger") << "Failed to send message\n";
         return;
      }
   }

   // If we add other backends for writing data we could
   // add them to the config and then check for them here and send the data

   // Indicate that the item was sent and ensure we limit the spam
   {
      const std::lock_guard<std::mutex> lock(_send_map_mutex);
      _send_map[id] = {
         .last_send = std::chrono::steady_clock::now(),
         .num_sends = num_sends
      };
   }
}

} // namespace alert
} // namespace monolith
