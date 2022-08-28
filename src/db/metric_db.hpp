#ifndef MONOLITH_DB_METRICS_HPP
#define MONOLITH_DB_METRICS_HPP

#include <string>
#include <sqlitelib.h>
#include <crate/metrics/reading.hpp>

namespace monolith {
namespace db {

class metric_db_c {
public:
   metric_db_c();
   ~metric_db_c();
   bool open(const std::string& file);
   void close();
   bool store(crate::metrics::sensor_reading_v1 metrics_entry);

private:
   sqlitelib::Sqlite* _db {nullptr};
};

} // namespace db
} // namespace monolith

#endif