#ifndef MONOLITH_DB_METRICS_HPP
#define MONOLITH_DB_METRICS_HPP

#include <string>
#include <sqlitelib.h>
#include <crate/metrics/reading_v1.hpp>

/*
   ABOUT:
      Long term storage for submitted metrics

      This object is not yet developed. Data submission object calls into store, 
      but it returns out and sends a log message saying its not yet developed.
*/

namespace monolith {
namespace db {

//! \brief Metric database
class metric_db_c {
public:
   //! \brief Create the database
   metric_db_c();

   //! \brief Close and destroy the database
   ~metric_db_c();

   //! \brief Open a database file
   //! \param file The file to open for the database
   //! \returns true iff the database could be opened
   bool open(const std::string& file);

   //! \brief Close the database
   //! \post The database will be closed and data won't be able
   //!       to be stored
   void close();

   //! \brief Store a metrics entry
   //! \param metrics_entry The metric to store
   //! \returns true iff the database is open and the metric could be stored
   bool store(crate::metrics::sensor_reading_v1 metrics_entry);

private:
   sqlitelib::Sqlite* _db {nullptr};
};

} // namespace db
} // namespace monolith

#endif