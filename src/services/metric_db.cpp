#include "metric_db.hpp"
#include <crate/externals/aixlog/logger.hpp>

#include <algorithm>

namespace monolith {
namespace services {

using namespace std::chrono_literals;

namespace {
uint64_t get_now() {
   return std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
       .count();
}
}

metric_db_c::metric_db_c(const std::string &file,
               bool enable_weekly_limit,
               bool enable_rollups) 
   : _file(file), 
      _limit_weekly(enable_weekly_limit), 
      _perform_rollups(enable_rollups) {}

metric_db_c::~metric_db_c() { stop(); }

bool metric_db_c::start() {

   if (p_running.load()) {
      LOG(WARNING) << TAG("metric_db_c::start") << "Server already started\n";
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
      timestamp BIGINT,
      node TEXT,
      sensor TEXT,
      value DOUBLE
   )
   )");

   /*
      If we've been told that we are purging record > 1 week do so before 
      we potentially rollup a database that potentially has a lot of records
   */
   if (_limit_weekly) {

      LOG(INFO) << TAG("metric_db_c::start") << "Performing pre-flight database prune on old (> 1 Week) records\n";

      if (!purge_weekly()) {
         LOG(ERROR) << TAG("metric_db_c::start") << "Failed to remove records > 1 week\n";
         return false;
      }
   }

   /*
      Its possible that we had rollups enabled on a database that has never
      been rolled up before so before we kick off the service we want to pre 
      rollup everything as depending on the number of metrics stored, it might
      take some time
   */
   if (_perform_rollups) {

      LOG(INFO) << TAG("metric_db_c::start") << "Performing pre-flight rollup check (this might take a moment)\n";

      _db->execute(R"(
      CREATE TABLE IF NOT EXISTS rollups (
         id INTEGER PRIMARY KEY AUTOINCREMENT,
         range_start BIGINT,
         range_end BIGINT,
         quantity INT
      )
      )");

      if (!check_rollups()) {
         LOG(WARNING) << TAG("metric_db_c::start") << "Failed to perform pre-flight rollups\n";
         delete _db;
         _db = nullptr;
         return false;
      }
   }

   p_running.store(true);
   p_thread = std::thread(&metric_db_c::run, this);

   LOG(INFO) << TAG("metric_db_c::start") << "Database service started\n";
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

/*
   Perform the weekly prune
*/
bool metric_db_c::purge_weekly() {
   auto now = get_now();
   auto last_week = now - ONE_WEEK_IN_SECONDS;
   std::string statement = "delete from metrics where timestamp < " + std::to_string(last_week) + ";";
   _db->execute(statement.c_str());
   _last_purge = get_now();
   return true;
}

/*
   Check the rollup table to see when the last rollup was performed and ensure
   that all data since then is rolled up into the correct rollup groups

   Note: No mutexing needed here. We are called from ::run so all bursting either
         has happened or will happen after this. Any fetch/store requests will be
         queued up while this runs 


   ------- THOUGHTS:

         Perhaps rollups should lag behind some amount of time (day or hour maybe?)
         This would greatly decrease the likelyhood of metrics falling between rollup segments
         as data should be submitted as soon as its collected. 
*/
bool metric_db_c::check_rollups() {

   _last_rollup = get_now();

   // Pull the latest entry from `rollups` table and figure out how many minutes 
   // have passed since then. For every minute, call perform_rollup 
   // which will rollup that data by averaging out the value of each reading
   // within that range

   auto stmt_latest_rollup = _db->prepare<int>("select MAX(id) from rollups");

   int latest_id = -1;
   for (const auto &id : stmt_latest_rollup.execute_cursor()) {
      latest_id = id;
   }

   uint64_t start_range{0};

   if (latest_id == -1) {
      LOG(DEBUG) << TAG("metric_db_c::check_rollups") << "Perfoming very first rollup\n";

      // Select the first metric that exists and set start_range to the time it was submitted
      // if empty, we are done and can return

   } else {

      // Select the `range_end` of the latest_id and set `start_range` to that value

   }

   // 
   uint64_t end_range = get_now();




   return false;
}

/*
   Perform the database rollup for a given range
*/
bool metric_db_c::perform_rollup(uint64_t start_time, uint64_t end_time) {

   return false;
}

void metric_db_c::run() {

   while (p_running.load()) {
      std::this_thread::sleep_for(100ms);

      // Check if its time for the weekly database purge
      if (_limit_weekly && (get_now() - _last_purge > ONE_WEEK_IN_SECONDS)) {
         purge_weekly();
      }

      // Check for rollups once a minute
      if (_perform_rollups && (get_now() - _last_rollup > 60)) {
         check_rollups();
      }

      // Bust out data storage / retrieval requests
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
   uint16_t queries_num{0};
   std::vector<request_if *> selected_requests;
   selected_requests.reserve(MAX_BURST);

   // Retrieve a potential subset of the query queue to execute
   {
      const std::lock_guard<std::mutex> lock(_request_queue_mutex);
      while (queries_num++ < MAX_BURST && !_request_queue.empty()) {
         selected_requests.push_back(_request_queue.front());
         _request_queue.pop();
      }
   }

   for (auto req : selected_requests) {

      // Determine what the request is attempting to do and route it
      switch (req->type) {
      case request_type_e::SUBMIT:
         store_metric(static_cast<submission_c *>(req)->entry);
         break;
      case request_type_e::FETCH_NODES:
         fetch_metric(static_cast<fetch_nodes_c *>(req));
         break;
      case request_type_e::FETCH_SENSORS:
         fetch_metric(static_cast<fetch_sensors_c *>(req));
         break;
      case request_type_e::FETCH_RANGE:
         fetch_metric(static_cast<fetch_range_c *>(req));
         break;
      case request_type_e::FETCH_AFTER:
         fetch_metric(static_cast<fetch_after_c *>(req));
         break;
      case request_type_e::FETCH_BEFORE:
         fetch_metric(static_cast<fetch_before_c *>(req));
         break;
      }

      // Clean up the request
      delete req;
      req = nullptr;
   }
}

void metric_db_c::store_metric(
    crate::metrics::sensor_reading_v1_c metrics_entry) {

   auto [ts, node_id, sensor_id, value] = metrics_entry.get_data();
   auto stmt = _db->prepare("INSERT INTO metrics (timestamp, node, sensor, "
                            "value) VALUES (?, ?, ?, ?)");

   // TODO: <WARNING> !!!
   //       SQLite3 Can't use int64_t (or uint32_t??) .. so it has to be an
   //       int32_t which maxes out at 2147483647 which means it will only work
   //       until: Mon Jan 18 2038 22:14:07 GMT-0500 (Eastern Standard Time)
   stmt.execute(static_cast<int32_t>(ts), node_id.c_str(), sensor_id.c_str(),
                value);
}

void metric_db_c::fetch_metric(fetch_nodes_c *fetch) {

   std::string query = "select distinct node from metrics;";

   auto stmt = _db->prepare<std::string>(query.c_str());

   std::string json_response = "[";
   for (const auto &node_name : stmt.execute_cursor()) {
      json_response += "\"" + node_name + "\",";
   }

   // if we don't get anything back then we need to be empty,
   // otherwise we have to pop off the comma
   if (json_response != "[") {
      json_response.pop_back();
   }
   json_response += "]";

   fetch->fetch.callback_data->fetch_result = json_response;
   fetch->fetch.callback_data->complete.store(true);
}

void metric_db_c::fetch_metric(fetch_sensors_c *fetch) {

   std::string query = "select distinct sensor from metrics where node = \"" +
                       fetch->node + "\";";

   auto stmt = _db->prepare<std::string>(query.c_str());

   std::string json_response = "[";
   for (const auto &sensor_name : stmt.execute_cursor()) {
      json_response += "\"" + sensor_name + "\",";
   }

   // if we don't get anything back then we need to be empty,
   // otherwise we have to pop off the comma
   if (json_response != "[") {
      json_response.pop_back();
   }
   json_response += "]";

   fetch->fetch.callback_data->fetch_result = json_response;
   fetch->fetch.callback_data->complete.store(true);
}

void metric_db_c::fetch_metric(fetch_range_c *fetch) {

   std::string query = " select * from metrics where node = \"" + fetch->node +
                       "\" and timestamp > " + std::to_string(fetch->start) +
                       " and timestamp < " + std::to_string(fetch->end) + ";";

   auto stmt = _db->prepare<int, int32_t, std::string, std::string, double>(
       query.c_str());

   std::string json_response = "[";
   for (const auto &[id, ts, node, sensor, value] : stmt.execute_cursor()) {

      // Construct a reading for easy json
      crate::metrics::sensor_reading_v1_c reading(ts, node, sensor, value);
      std::string encoded;
      if (!reading.encode_to(encoded)) {
         json_response += "{\"error\":\"Failed to encode reading\"},";
      } else {
         json_response += encoded + ",";
      }
   }

   // if we don't get anything back then we need to be empty,
   // otherwise we have to pop off the comma
   if (json_response != "[") {
      json_response.pop_back();
   }
   json_response += "]";

   fetch->fetch.callback_data->fetch_result = json_response;
   fetch->fetch.callback_data->complete.store(true);
}

void metric_db_c::fetch_metric(fetch_after_c *fetch) {

   std::string query = " select * from metrics where node = \"" + fetch->node +
                       "\" and timestamp > " + std::to_string(fetch->time) +
                       ";";

   auto stmt = _db->prepare<int, int32_t, std::string, std::string, double>(
       query.c_str());

   std::string json_response = "[";
   for (const auto &[id, ts, node, sensor, value] : stmt.execute_cursor()) {

      // Construct a reading for easy json
      crate::metrics::sensor_reading_v1_c reading(ts, node, sensor, value);
      std::string encoded;
      if (!reading.encode_to(encoded)) {
         json_response += "{\"error\":\"Failed to encode reading\"},";
      } else {
         json_response += encoded + ",";
      }
   }

   // if we don't get anything back then we need to be empty,
   // otherwise we have to pop off the comma
   if (json_response != "[") {
      json_response.pop_back();
   }
   json_response += "]";

   fetch->fetch.callback_data->fetch_result = json_response;
   fetch->fetch.callback_data->complete.store(true);
}

void metric_db_c::fetch_metric(fetch_before_c *fetch) {

   std::string query = " select * from metrics where node = \"" + fetch->node +
                       "\" and timestamp < " + std::to_string(fetch->time) +
                       ";";

   auto stmt = _db->prepare<int, int32_t, std::string, std::string, double>(
       query.c_str());

   std::string json_response = "[";
   for (const auto &[id, ts, node, sensor, value] : stmt.execute_cursor()) {

      // Construct a reading for easy json
      crate::metrics::sensor_reading_v1_c reading(ts, node, sensor, value);
      std::string encoded;
      if (!reading.encode_to(encoded)) {
         json_response += "{\"error\":\"Failed to encode reading\"},";
      } else {
         json_response += encoded + ",";
      }
   }

   // if we don't get anything back then we need to be empty,
   // otherwise we have to pop off the comma
   if (json_response != "[") {
      json_response.pop_back();
   }
   json_response += "]";

   fetch->fetch.callback_data->fetch_result = json_response;
   fetch->fetch.callback_data->complete.store(true);
}

bool metric_db_c::check_db() {
   if (!_db) {
      LOG(WARNING) << TAG("metric_db_c::check_db")
                   << "metric_db_c not instantiated!\n";
      return false;
   }

   if (!_db->is_open()) {
      LOG(WARNING) << TAG("metric_db_c::check_db") << "metric_db_c not open!\n";
      return false;
   }
   return true;
}

bool metric_db_c::store(crate::metrics::sensor_reading_v1_c metrics_entry) {

   if (!check_db()) {
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new submission_c(metrics_entry));
   return true;
}

bool metric_db_c::fetch_nodes(fetch_s fetch) {

   if (!check_db()) {
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new fetch_nodes_c(fetch));
   return true;
}

bool metric_db_c::fetch_sensors(fetch_s fetch, std::string node_id) {

   if (!check_db()) {
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new fetch_sensors_c(fetch, node_id));
   return true;
}

bool metric_db_c::fetch_range(fetch_s fetch, std::string node_id, int64_t start,
                              int64_t end) {

   if (!check_db()) {
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new fetch_range_c(fetch, node_id, start, end));
   return true;
}

bool metric_db_c::fetch_after(fetch_s fetch, std::string node_id,
                              int64_t time) {

   if (!check_db()) {
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new fetch_after_c(fetch, node_id, time));
   return true;
}

bool metric_db_c::fetch_before(fetch_s fetch, std::string node_id,
                               int64_t time) {

   if (!check_db()) {
      return false;
   }

   // Enqueue the item to be put into the database
   const std::lock_guard<std::mutex> lock(_request_queue_mutex);
   _request_queue.push(new fetch_before_c(fetch, node_id, time));
   return true;
}

} // namespace services
} // namespace monolith