#ifndef MONOLITH_SERVICES_ACTION_DISPATCH_HPP
#define MONOLITH_SERVICES_ACTION_DISPATCH_HPP

#include "db/kv.hpp"
#include "networking/types.hpp"
#include "interfaces/service_if.hpp"
#include <crate/control/action_v1.hpp>
#include <atomic>
#include <mutex>
#include <queue>
namespace monolith {
namespace services {

//! \brief Action dispatcher (pushes action requests to controllers)
class action_dispatch_c : public service_if {
public:
   action_dispatch_c() = delete;

   //! \brief Create the dispatcher
   //! \param registrar_db The registration database
   action_dispatch_c(monolith::db::kv_c* registrar_db);

   //! \brief Queue an action for dispatch
   //! \param controller_id The id of the controller to dispatch to
   //! \param action_id The id of the action to trigger
   //! \param value The value to send along with the action
   bool dispatch(std::string controller_id, std::string action_id, double value);

   // From service_if
   virtual bool start() override final;
   virtual bool stop() override final;
private:
  static constexpr uint16_t MAX_BURST = 100;
  static constexpr uint8_t MAX_RETRIES = 5;

   struct queued_action_s {
      std::string address;
      uint32_t port {0};
      crate::control::action_v1_c action;
   };

   monolith::db::kv_c* _registrar_db {nullptr};
   std::mutex _action_queue_mutex;
   std::queue<queued_action_s> _action_queue;

  void run();
  void burst();
};

} // namespace services
} // namespace monolith

#endif