#ifndef MONOLITH_SERVICES_RULE_EXECUTOR_HPP
#define MONOLITH_SERVICES_RULE_EXECUTOR_HPP

#include <compare>
#include <queue>
#include <crate/metrics/reading_v1.hpp>
#include "interfaces/service_if.hpp"

namespace monolith {
namespace services {

class rule_executor_c : public service_if {
public:
   rule_executor_c() = delete;
   rule_executor_c(const std::string& file);
   virtual ~rule_executor_c() override final;

   bool open();

   void submit_metric(crate::metrics::sensor_reading_v1_c& data);

   // From service_if
   virtual bool start() override final;
   virtual bool stop() override final;
private:

   static constexpr uint8_t MAX_BURST = 100;

   std::string _file;
   std::queue<crate::metrics::sensor_reading_v1_c> _reading_queue;
   std::mutex _reading_queue_mutex;

   void run();
   void burst();
};

} // namespace services
} // namespace monolith

#endif