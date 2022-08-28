#include "metric_db.hpp"
#include <crate/externals/aixlog/logger.hpp>

namespace monolith {
namespace db {
   
metric_db_c::metric_db_c(){}

metric_db_c::~metric_db_c(){
   close();
}

bool metric_db_c::open(const std::string& file) {

   if (_db) {
      LOG(WARNING) << TAG("metric_db_c::open") << "Database object already open\n";
      return false;
   }

   _db = new sqlitelib::Sqlite(file.c_str());

   if (!_db->is_open()) {
      close();
      return false;
   }

   _db->execute(R"(
   CREATE TABLE IF NOT EXISTS metrics (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      timestamp INTEGER,
      node TEXT,
      sensor TEXT,
      value REAL
   )
   )");
   return true;
}

void metric_db_c::close() {

   if (_db) {
      delete _db;
      _db = {nullptr};
   }
}

bool metric_db_c::store(crate::metrics::sensor_reading_v1 metrics_entry) {

   /*
         TODO:  WE NEED TO WORK ON THIS. KILL FOR NOW
   */
   LOG(WARNING) << TAG("metric_db_c::store") << "metric_db_c submission disabled [NYD]\n";
   return true;

   if (!_db) {
      LOG(WARNING) << TAG("metric_db_c::store") << "metric_db_c not open!\n";
      return false;
   }

   auto [ts, node_id, sensor_id, value] = metrics_entry.getData();

   auto stmt = _db->prepare("INSERT INTO metrics (timestamp, node, sensor, value) VALUES (?, ?, ?, ?)");
   stmt.execute(
      ts, node_id.c_str(), sensor_id.c_str(), value
      );
   return true;
}

} // namespace db
} // namespace monolith