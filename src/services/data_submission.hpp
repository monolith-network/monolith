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

/*
ABOUT:
   This data submission service takes in data from a specified port via
   a crate::networking::message_server and through the function
   submit_data (if data is submitted to application over http)
   and enqueues the data. Periodically it will go through the queued
   metrics and burst them out in chunks to :
      1) metrics_db_c -> Where they will be written to disk
      2) metric_streamer -> Where they will be queued up to be
         dispersed to any registered metric stream receivers
*/

namespace monolith {
namespace services {

//! \brief Create the data submission tool
class data_submission_c : public service_if,
                          private crate::networking::message_receiver_if {
public:
   data_submission_c() = delete;

   //! \brief Create the submission client
   //! \param host_port The ipv4 connection information
   //! \param registrar_db The registrar database 
   //! \param metric_streamer Metric streaming service
   //! \param metric_db Metrics database
   data_submission_c(
      const monolith::networking::ipv4_host_port_s& host_port,
      monolith::db::kv_c* registrar_db,
      monolith::services::metric_streamer_c* metric_streamer,
      monolith::db::metric_db_c* metric_db
      );

   // From service_if
   virtual bool start() override final;
   virtual bool stop() override final;

   //! \brief Submit data reading from another servuce
   //! \param data The validated data to submit
   //! \note This is the same as if an endpoint submitted data via the 
   //!       crate::networking::message_receiver_if (TCP)
   void submit_data(crate::metrics::sensor_reading_v1& data);

private:
   static constexpr uint8_t MAX_METRICS_PER_BURST = 100;    // Maximum umber of metric per metric burst
   static constexpr uint8_t MAX_SUBMISSION_ATTEMPTS = 3;    // Maximum number of times to attempt to sending each metric

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
   void check_purge();
   void submit_metrics();
   virtual void receive_message(std::string message) override final;
};

} // namespace services
} // namespace monolith
#endif