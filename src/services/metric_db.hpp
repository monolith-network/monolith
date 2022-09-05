#ifndef MONOLITH_DB_METRICS_HPP
#define MONOLITH_DB_METRICS_HPP

#include "interfaces/service_if.hpp"
#include <atomic>
#include <crate/metrics/reading_v1.hpp>
#include <functional>
#include <mutex>
#include <queue>
#include <sqlitelib.h>
#include <string>
/*
   ABOUT:
      Long term storage for submitted metrics

      This object is not yet developed. Data submission object calls into store,
      but it returns out and sends a log message saying its not yet developed.
*/

namespace monolith {
namespace services {

//! \brief Metric database
class metric_db_c : public service_if {
 public:
   static constexpr double DEFAULT_QUERY_TIMEOUT_SEC = 30;

   //! \brief A structure representing the response to a fetch
   struct fetch_response_s {
      std::string fetch_result; //! The data returned from the fetch
      std::atomic<bool> complete{
          false}; //! Will become true when the fetch response is complete
      std::atomic<bool> timeout{
          false}; //! Flag to indicate if request timed out
   };

   //! \brief A callback function for submitted queries
   using fetch_callback_f =
       std::function<void(fetch_response_s *, std::string)>;

   //! \brief A structure representing a fetch
   struct fetch_s {
      fetch_callback_f callback; //! Callback function to execute post fetch
      fetch_response_s *callback_data; //! Data objec to hand back post fetch
   };

   //! \brief Create the database
   //! \param file The file to open for the database
   //! \param metric_expiration_time_sec Length of time any metric is allowed to exist (0 = infinite)
   metric_db_c(const std::string &file,
               uint64_t metric_expiration_time_sec);

   //! \brief Close and destroy the database
   virtual ~metric_db_c() override final;

   //! \brief Store a metrics entry
   //! \param metrics_entry The metric to store
   //! \returns true iff the database is open and the metric could be queued
   bool store(crate::metrics::sensor_reading_v1_c metrics_entry);

   bool check_db();
   bool fetch_nodes(fetch_s fetch);
   bool fetch_sensors(fetch_s fetch, std::string node_id);
   bool fetch_range(fetch_s fetch, std::string node_id, int64_t start,
                    int64_t end);
   bool fetch_after(fetch_s fetch, std::string node_id, int64_t time);
   bool fetch_before(fetch_s fetch, std::string node_id, int64_t time);

   // From service_if
   virtual bool start() override final;
   virtual bool stop() override final;

 private:
   static constexpr uint16_t MAX_BURST = 100;
   static constexpr uint64_t METRIC_PURGE_CHECK_INTERVAL_SEC = 30;

   enum class request_type_e {
      SUBMIT,
      FETCH_NODES,
      FETCH_SENSORS,
      FETCH_RANGE,
      FETCH_AFTER,
      FETCH_BEFORE
   };

   class request_if {
    public:
      request_if() = delete;
      virtual ~request_if() {}
      request_if(request_type_e type) : type(type) {}
      request_type_e type;
   };

   class submission_c : public request_if {
    public:
      submission_c() = delete;
      submission_c(crate::metrics::sensor_reading_v1_c metrics_entry)
          : request_if(request_type_e::SUBMIT), entry(metrics_entry) {}
      crate::metrics::sensor_reading_v1_c entry;
   };

   class fetch_nodes_c : public request_if {
    public:
      fetch_nodes_c() = delete;
      fetch_nodes_c(fetch_s metrics_fetch)
          : request_if(request_type_e::FETCH_NODES), fetch(metrics_fetch) {}
      fetch_s fetch;
   };

   class fetch_sensors_c : public request_if {
    public:
      fetch_sensors_c() = delete;
      fetch_sensors_c(fetch_s metrics_fetch, std::string node_id)
          : request_if(request_type_e::FETCH_SENSORS), node(node_id),
            fetch(metrics_fetch) {}
      std::string node;
      fetch_s fetch;
   };

   class fetch_range_c : public request_if {
    public:
      fetch_range_c() = delete;
      fetch_range_c(fetch_s metrics_fetch, std::string node_id, int64_t start,
                    int64_t end)
          : request_if(request_type_e::FETCH_RANGE), node(node_id), start(start),
            end(end), fetch(metrics_fetch) {}
      std::string node;
      int64_t start;
      int64_t end;
      fetch_s fetch;
   };

   class fetch_after_c : public request_if {
    public:
      fetch_after_c() = delete;
      fetch_after_c(fetch_s metrics_fetch, std::string node_id, int64_t time)
          : request_if(request_type_e::FETCH_AFTER), node(node_id), time(time),
            fetch(metrics_fetch) {}
      std::string node;
      int64_t time;
      fetch_s fetch;
   };

   class fetch_before_c : public request_if {
    public:
      fetch_before_c() = delete;
      fetch_before_c(fetch_s metrics_fetch, std::string node_id, int64_t time)
          : request_if(request_type_e::FETCH_BEFORE), node(node_id), time(time),
            fetch(metrics_fetch) {}
      std::string node;
      int64_t time;
      fetch_s fetch;
   };

   std::string _file;
   sqlitelib::Sqlite *_db{nullptr};
   std::mutex _request_queue_mutex;
   std::queue<request_if *> _request_queue;
   uint64_t _metric_expiration_time_sec{0};
   uint64_t _last_metric_purge{0};

   bool purge_metrics();

   void run();
   void burst();

   // Handle the individual types of access to the database
   void store_metric(crate::metrics::sensor_reading_v1_c metrics_entry);
   void fetch_metric(fetch_nodes_c *fetch);
   void fetch_metric(fetch_sensors_c *fetch);
   void fetch_metric(fetch_range_c *fetch);
   void fetch_metric(fetch_after_c *fetch);
   void fetch_metric(fetch_before_c *fetch);
};

} // namespace services
} // namespace monolith

#endif