#ifndef MONOLITH_ALERT_HPP
#define MONOLITH_ALERT_HPP

#include <string>
#include <chrono>
#include <mutex>
#include <unordered_map>

#include "interfaces/sms_backend_if.hpp"

namespace monolith {
namespace alert {

//! \brief The alert manager
class alert_manager_c {
public:
   //! \brief Configuration
   struct configuration_c {
      monolith::sms_backend_if* sms_backend {nullptr};
   };

   //! \brief Construct the alert manager with a config
   alert_manager_c(configuration_c config);

   //! \brief Trigger an alert
   //! \param id The id of the alert
   //! \param message The message to send
   void trigger(const int id, const std::string message);
private:

   static constexpr double SPAM_LIMITER = 30.0; //! Only allow one message per id every 30 seconds
   static constexpr uint8_t MAX_SENDS = 10;     //! Limit number of sends during testing

   struct sent_s {
      std::chrono::time_point<std::chrono::steady_clock> last_send;
      uint64_t num_sends { 0};
   };

   std::unordered_map<int, sent_s> _send_map;
   std::mutex _send_map_mutex;

   configuration_c _config;
};

} // namespace alert
} // namespace monolith

#endif