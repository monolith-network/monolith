#include "metric_db.hpp"
#include <crate/externals/aixlog/logger.hpp>

namespace monolith {
namespace services {
   
using namespace std::chrono_literals;

metric_db_c::metric_db_c(const std::string& file) : _file(file){}

metric_db_c::~metric_db_c(){
   stop();
}

bool metric_db_c::start() {

   if (p_running.load()) {
      LOG(WARNING) << TAG("metric_db_c::start") 
                     << "Server already started\n";
      return true;
   }
   
   _db = new sqlitelib::Sqlite(_file.c_str());

   if (!_db->is_open()) {
      delete _db;
      _db = nullptr;
      return false;
   }

   _db->execute(R"(
   CREATE TABLE IF NOT EXISTS metrics (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      timestamp TEXT,
      node TEXT,
      sensor TEXT,
      value TEXT
   )
   )");

   p_running.store(true);
   p_thread = std::thread(&metric_db_c::run, this);

   LOG(INFO) << TAG("metric_db_c::start") 
               << "Database service started\n";
   return true;
}

bool metric_db_c::stop() {

   if (!p_running.load()) {
      return true;
   }

   p_running.store(false);

   if (p_thread.joinable()) {
      p_thread.join();
   }

   if (_db) {
      delete _db;
      _db = nullptr;
   }

   return true;
}

void metric_db_c::run() {

   while(p_running.load()) {
      std::this_thread::sleep_for(100ms);
      burst();
   }
}

void metric_db_c::burst() {

   // Check to see if we should do anything
   {
      const std::lock_guard<std::mutex> lock(_request_queue_mutex);
      if (_request_queue.empty()) {
         return;
      }
   }
   
   // Setup outside of the lock
   uint16_t queries_num  {0};
   std::vector<request_if*> selected_requests;
   selected_requests.reserve(MAX_BURST);

   // Retrieve a potential subset of the query queue to execute
   {
      const std::lock_guard<std::mutex> lock(_request_queue_mutex);
      while(queries_num++ < MAX_BURST && !_request_queue.empty()) {
         selected_requests.push_back(_request_queue.front());
         _request_queue.pop();
      }
   }

   for (auto req : selected_requests) {

      // Determine what the request is attempting to do and route it
      switch (req->type) {
         case request_type::SUBMIT:
            store_metric(static_cast<submission_c*>(req)->entry);
         break;
         case request_type::FETCH:
            fetch_metric(static_cast<fetch_c*>(req)->fetch);
         break;
      }

      // Clean up the request
      delete req;
      req = nullptr;
   }
}

void metric_db_c::store_metric(crate::metrics::sensor_reading_v1_c metrics_entry) {

   auto [ts, node_id, sensor_id, value] = metrics_entry.getData();
   auto stmt = _db->prepare("INSERT INTO metrics (timestamp, node, sensor, value) VALUES (?, ?, ?, ?)");

   stmt.execute(
      std::to_string(ts).c_str(), node_id.c_str(), sensor_id.c_str(), value
      );
}

void metric_db_c::fetch_metric(fetch_s fetch) {


   // TODO : Finish this method


   LOG(WARNING) << TAG("metric_db_c::burst") << "FETCH has not yet been implemented\n";
}

bool metric_db_c::store(crate::metrics::sensor_reading_v1_c metrics_entry) {

   if (!_db) {
      LOG(WARNING) << TAG("metric_db_c::store") << "metric_db_c not instantiated!\n";
      return false;
   }

   if (!_db->is_open()) {
      LOG(WARNING) << TAG("metric_db_c::store") << "metric_db_c not open!\n";
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new submission_c(metrics_entry));
   return true;
}

bool metric_db_c::fetch(fetch_s fetch_request) {

   if (!_db) {
      LOG(WARNING) << TAG("metric_db_c::store") << "metric_db_c not instantiated!\n";
      return false;
   }

   if (!_db->is_open()) {
      LOG(WARNING) << TAG("metric_db_c::store") << "metric_db_c not open!\n";
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new fetch_c(fetch_request));
   return true;
}

} // namespace services
} // namespace monolith