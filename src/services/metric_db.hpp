#ifndef MONOLITH_DB_METRICS_HPP
#define MONOLITH_DB_METRICS_HPP

#include <string>
#include <sqlitelib.h>
#include <crate/metrics/reading_v1.hpp>
#include "interfaces/service_if.hpp"
#include <functional>
#include <queue>
#include <mutex>

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
   using fetch_callback_f = std::function<void(void*, std::string data)>;

   //! \brief A structure representing a fetch
   struct fetch_s {
      fetch_callback_f callback; //! Callback function to execute post fetch
      void* callback_data;       //! Data objec to hand back post fetch
      std::string query;         //! Query string to execute
   };

   //! \brief Create the database
   //! \param file The file to open for the database
   metric_db_c(const std::string& file);

   //! \brief Close and destroy the database
   virtual ~metric_db_c() override final;

   //! \brief Store a metrics entry
   //! \param metrics_entry The metric to store
   //! \returns true iff the database is open and the metric could be queued
   bool store(crate::metrics::sensor_reading_v1_c metrics_entry);

   //! \brief Submit a fetch request
   //! \param fetch The fetch to submit
   //! \returns true iff the database is open and the fetch could be queued
   //! \post The fetch will be enqueued and upon completion
   //!       the given callback will be called with the given
   //!       callback data
   bool fetch(fetch_s fetch);

   // From service_if
   virtual bool  start() override final;
   virtual bool stop() override final;

private:
   static constexpr uint16_t MAX_BURST = 100;

   enum class request_type {
      SUBMIT,
      FETCH
   };

   class request_if {
   public:
      request_if() = delete;
      virtual ~request_if() {}
      request_if(request_type type) : type(type){}
      request_type type;
   };

   class submission_c : public request_if {
   public:
      submission_c() = delete;
      submission_c(crate::metrics::sensor_reading_v1_c metrics_entry) 
         : request_if(request_type::SUBMIT), entry(metrics_entry) {}
      crate::metrics::sensor_reading_v1_c entry;
   };

   class fetch_c : public request_if {
   public:
      fetch_c() = delete;
      fetch_c(fetch_s metrics_fetch) 
         : request_if(request_type::FETCH), fetch(metrics_fetch) {}
      fetch_s fetch;
   };

   std::string _file;
   sqlitelib::Sqlite* _db {nullptr};
   std::mutex _request_queue_mutex;
   std::queue<request_if*> _request_queue;
   void run();
   void burst();
   void store_metric(crate::metrics::sensor_reading_v1_c metrics_entry);
   void fetch_metric(fetch_s fetch);
};

} // namespace services
} // namespace monolith

#endif