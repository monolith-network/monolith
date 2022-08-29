#ifndef MONOLITH_SERVICES_DATA_SUBMISSION_HPP
#define MONOLITH_SERVICES_DATA_SUBMISSION_HPP

#include <queue>
#include <mutex>

#include "networking/types.hpp"
#include "interfaces/service_if.hpp"
#include "services/metric_streamer.hpp"
#include "db/metric_db.hpp"
#include "db/kv.hpp"

#include <crate/metrics/reading.hpp>
#include <crate/networking/message_server.hpp>
#include <crate/networking/message_receiver_if.hpp>

namespace monolith {
namespace services {

// !\brief Create the data submission tool
class data_submission_c : public service_if,
                          private crate::networking::message_receiver_if {
public:
   data_submission_c() = delete;
   data_submission_c(
      const monolith::networking::ipv4_host_port_s& host_port,
      monolith::db::kv_c* registrar,
      monolith::services::metric_streamer_c* metric_streamer,
      monolith::db::metric_db_c* metric_db
      );

   virtual bool start() override final;
   virtual bool stop() override final;

   void submit_data(crate::metrics::sensor_reading_v1& data);

private:
   static constexpr uint8_t MAX_METRICS_PER_BURST = 100;
   static constexpr uint8_t MAX_SUBMISSION_ATTEMPTS = 3;

   struct db_entry_queue {
      size_t submission_attempts {0};
      crate::metrics::sensor_reading_v1 metric;
   };

   monolith::networking::ipv4_host_port_s _host_port;
   monolith::services::metric_streamer_c* _stream_server {nullptr};
   monolith::db::metric_db_c* _database {nullptr};

   crate::networking::message_server* _message_server {nullptr};
   std::queue<db_entry_queue> _metric_queue;
   std::mutex _metric_queue_mutex;

   monolith::db::kv_c* _registrar {nullptr};

   void run();
   void submit_metrics();
   virtual void receive_message(std::string message) override final;
};

} // namespace services
} // namespace monolith
#endif