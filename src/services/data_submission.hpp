#ifndef MONOLITH_SERVICES_DATA_SUBMISSION_HPP
#define MONOLITH_SERVICES_DATA_SUBMISSION_HPP

#include <mutex>
#include <queue>

#include "db/kv.hpp"
#include "heartbeats.hpp"
#include "interfaces/service_if.hpp"
#include "networking/types.hpp"
#include "services/metric_db.hpp"
#include "services/metric_streamer.hpp"
#include "services/rule_executor.hpp"

#include <crate/metrics/reading_v1.hpp>

/*
ABOUT:
   This data submission service takes in data from a specified port via
   the function submit_data and enqueues the data.
   Periodically it will go through the queued metrics and burst
   them out in chunks to :
      1) metrics_db_c -> Where they will be written to disk
      2) metric_streamer -> Where they will be queued up to be
         dispersed to any registered metric stream receivers
*/

namespace monolith {
namespace services {

//! \brief Create the data submission tool
class data_submission_c : public service_if {
 public:
   data_submission_c() = delete;

   //! \brief Create the submission client
   //! \param registrar_db The registrar database
   //! \param metric_streamer Metric streaming service
   //! \param metric_db Metrics database
   //! \param heartbeat_manager Manager for recording heartbeats
   data_submission_c(monolith::db::kv_c *registrar_db,
                     monolith::services::metric_streamer_c *metric_streamer,
                     monolith::services::metric_db_c *metric_db,
                     monolith::services::rule_executor_c *rule_executor,
                     monolith::heartbeats_c *heartbeat_manager);

   // From service_if
   virtual bool start() override final;
   virtual bool stop() override final;

   //! \brief Submit data reading from another servuce
   //! \param data The validated data to submit
   //! \note This is the same as if an endpoint submitted data via the
   //!       crate::networking::message_receiver_if (TCP)
   void submit_data(crate::metrics::sensor_reading_v1_c &data);

 private:
   static constexpr uint8_t MAX_METRICS_PER_BURST =
       100; // Maximum umber of metric per metric burst
   static constexpr uint8_t MAX_SUBMISSION_ATTEMPTS =
       3; // Maximum number of times to attempt to sending each metric

   struct db_entry_queue {
      size_t submission_attempts{0};
      crate::metrics::sensor_reading_v1_c metric;
   };

   monolith::services::metric_streamer_c *_stream_server{nullptr};
   monolith::services::metric_db_c *_database{nullptr};
   monolith::services::rule_executor_c *_rule_executor{nullptr};
   monolith::heartbeats_c *_heartbeat_manager{nullptr};

   std::queue<db_entry_queue> _metric_queue;
   std::mutex _metric_queue_mutex;

   monolith::db::kv_c *_registrar{nullptr};

   void run();
   void check_purge();
   void submit_metrics();
};

} // namespace services
} // namespace monolith
#endif