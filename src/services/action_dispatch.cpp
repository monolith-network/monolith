#include "action_dispatch.hpp"
#include <crate/externals/aixlog/logger.hpp>
#include <crate/networking/message_writer.hpp>
#include <crate/registrar/controller_v1.hpp>
#include <vector>

using namespace std::chrono_literals;

namespace monolith {
namespace services {

namespace {

uint64_t stamp() {
   return std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
       .count();
}

} // namespace

action_dispatch_c::action_dispatch_c(monolith::db::kv_c *registrar_db)
    : _registrar_db(registrar_db) {}

bool action_dispatch_c::start() {

   p_running.store(true);

   p_thread = std::thread(&action_dispatch_c::run, this);

   return true;
}

bool action_dispatch_c::stop() {

   p_running.store(false);

   if (p_thread.joinable()) {
      p_thread.join();
   }

   return true;
}

void action_dispatch_c::run() {
   while (p_running.load()) {
      std::this_thread::sleep_for(10ms);
      burst();
   }
}

void action_dispatch_c::burst() {

   {
      const std::lock_guard<std::mutex> lock(_action_queue_mutex);
      if (_action_queue.empty()) {
         return;
      }
   }

   LOG(TRACE) << TAG("action_dispatch_c::burst") << "Starting dispatch\n";

   // Setup a vevtor and counter we can copy some actions into
   // to make sure we don't keep the mutex during all the dispatch sends

   uint32_t num_actions{0};
   std::vector<queued_action_s> selected_actions;
   selected_actions.reserve(MAX_BURST);

   {
      const std::lock_guard<std::mutex> lock(_action_queue_mutex);
      while (!_action_queue.empty() && num_actions++ < MAX_BURST) {
         selected_actions.push_back(_action_queue.front());
         _action_queue.pop();
      }
   }

   // Go through the selected actions and send them out

   for (auto &action_entry : selected_actions) {

      // Encode the action to a string
      std::string encoded_action;
      if (!action_entry.action.encode_to(encoded_action)) {
         LOG(FATAL) << TAG("action_dispatch_c::burst")
                    << "Failed to decode selected action\n";
         continue;
      }

      crate::networking::message_writer_c writer(action_entry.address,
                                                 action_entry.port);

      bool sent{false};
      bool write_okay{true};
      uint8_t attempts{0};

      while (!sent && write_okay && attempts++ < MAX_RETRIES) {

         // Attempt to write the data and then check the number of
         // bytes written against how many we expected to write
         if ((writer.write(encoded_action, write_okay) ==
              encoded_action.length())) {
            sent = true;
         }
      }

      LOG(FATAL) << TAG("action_dispatch_c::burst")
                 << "Failed to write action to destination\n";
   }
}

bool action_dispatch_c::dispatch(std::string controller_id,
                                 std::string action_id, double value) {

   LOG(TRACE) << TAG("action_dispatch_c::dispatch") << controller_id << " | "
              << action_id << " | " << value << "\n";

   // Check db for controller
   auto data = _registrar_db->load(controller_id);

   if (!data.has_value()) {
      LOG(FATAL) << TAG("action_dispatch_c::dispatch")
                 << "Given controller id is not regestered: " << controller_id
                 << "\n";
      return false;
   }

   // Ensure its a controller and not a node
   crate::registrar::controller_v1_c controller;
   if (!controller.decode_from(*data)) {
      LOG(FATAL) << TAG("action_dispatch_c::dispatch")
                 << "Given controller id could not be decoded:  "
                 << controller_id << "\n";
      return false;
   }

   auto [c_id, c_desc, c_ip, c_port, c_action_list] = controller.get_data();

   for (auto &action : c_action_list) {

      // Find the action that we are going to enqueue
      if (action_id == action.id) {

         // Enqueue the action
         const std::lock_guard<std::mutex> lock(_action_queue_mutex);
         _action_queue.push({.address = c_ip,
                             .port = c_port,
                             .action = crate::control::action_v1_c(
                                 stamp(), c_id, action.id, value)});
         return true;
      }
   }

   LOG(FATAL) << TAG("action_dispatch_c::dispatch")
              << "Failed to locate action id [" << action_id
              << "] on controller [" << controller_id << "]\n";
   return false;
}

} // namespace services
} // namespace monolith