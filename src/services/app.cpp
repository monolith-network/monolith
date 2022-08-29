#include "app.hpp"

#include <crate/externals/aixlog/logger.hpp>
#include <chrono>
#include <sstream>

using namespace std::chrono_literals;

namespace monolith {
namespace services {

app_c::app_c(const std::string& address, 
               uint32_t port, 
               monolith::db::kv_c* db,
               monolith::services::metric_streamer_c* metric_streamer)
         : _address(address), 
            _port(port), 
            _registration_db(db), 
            _metric_streamer(metric_streamer){
   _app_server = new httplib::Server();
}

bool app_c::start() {

   if (p_running.load()) {
      LOG(INFO) << TAG("app_c::start") << "Application already running\n";
      return false;
   }

   LOG(INFO) << TAG("app_c::start") << "Starting app web server [" << _address << ":" << _port << "]\n";
   
   setup_endpoints();
   
   auto runner = [](httplib::Server* server, 
                     std::string address, 
                     short port) {

      LOG(INFO) << TAG("app_c::runner") << "App HTTP starting [" << address << ":" << port << "]\n";
      server->listen(address, port);
   };
   
   p_thread = std::thread(runner, _app_server, _address, _port);

   std::this_thread::sleep_for(10ms);

   if (!_app_server->is_running()) {
      LOG(INFO) << TAG("app_c::start") << "Failed to start app webserver\n";
      p_running.store(false);
      return false;
   }
   p_running.store(true);
   return true;
}

bool app_c::stop() {

   if (!p_running.load()) {
      LOG(INFO) << TAG("app_c::stop") << "Application not running\n";
      return false;
   }

   _app_server->stop();
   p_running.store(false);
   return true;
}

void app_c::setup_endpoints() {

   // Endpoint to add metric stream destination
   _app_server->Get("/", 
      std::bind(&app_c::http_root, 
            this, 
            std::placeholders::_1, 
            std::placeholders::_2));

   // -------- [Stream Registration Endpoints] --------

   // Endpoint to add metric stream destination
   _app_server->Get(R"(/metric/stream/add/(.*?)/(\d+))", 
      std::bind(&app_c::add_metric_stream_endpoint, 
            this, 
            std::placeholders::_1, 
            std::placeholders::_2));
            
   // Endpoint to add metric stream destination
   _app_server->Get(R"(/metric/stream/del/(.*?)/(\d+))", 
      std::bind(&app_c::del_metric_stream_endpoint, 
            this, 
            std::placeholders::_1, 
            std::placeholders::_2));

   // ---------- [Registration DB Endpoints] ----------

   // Endpoint to probe for item in database
   _app_server->Get(R"(/probe/(.*?))", 
      std::bind(&app_c::db_probe, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));
   
   // Endpoint to submit item to database
   _app_server->Get(R"(/submit/(.*?)/(.*?))", 
      std::bind(&app_c::db_submit, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));

   // Endpoint to fetch item from database
   _app_server->Get(R"(/fetch/(.*?))", 
      std::bind(&app_c::db_fetch, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));

   // Endpoint to delete item from database
   _app_server->Get(R"(/delete/(.*?))", 
      std::bind(&app_c::db_remove, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));
}

std::string app_c::get_json_response(const app_c::return_codes_e rc, 
                                 const std::string msg) {
   return "{\"status\":" + 
   std::to_string(static_cast<uint32_t>(rc)) +
   ",\"data\":\"" +
      msg + 
      "\"}";
}

bool app_c::valid_http_req(const httplib::Request& req, 
                     httplib::Response& res, 
                     size_t expected_items) {
   if (req.matches.size() < expected_items) {
      LOG(INFO) << TAG("app_c::valid_http_req") << "Expected " << expected_items << ", but got " << req.matches.size() << "\n";

      for(auto i = 0; i < req.matches.size(); i++) {
         LOG(INFO) << TAG("<dump>") << req.matches[i] << "\n";
      }

      res.set_content(
         get_json_response(
            return_codes_e::BAD_REQUEST_400,
            "Invalid request"), 
         "application/json"
         );
      return false;
   }
   return true;
}

void app_c::http_root(const httplib::Request& req, httplib:: Response& res) {
   std::string body = "<h1>Monolith app server</h1><br>"
   "TODO: Show status of db/streamer/submission server etc";

   res.set_content(body, "text/html");
}

void app_c::add_metric_stream_endpoint(const httplib::Request& req, httplib:: Response& res) {

   if (!valid_http_req(req, res, 3)) { return; }

   uint32_t port {0};
   std::stringstream ts_ss(req.matches[2].str());
   ts_ss >> port;

   // Should be impossible given how httplib routes things, but its always
   // best to be sure
   if (port == 0) {
      res.set_content(
         get_json_response(
            return_codes_e::BAD_REQUEST_400,
            "Invalid port given : " + req.matches[2].str()), 
         "application/json"
         );
   }

   // Queue the item to be added
   //
   _metric_streamer->add_destination(req.matches[1].str(), port);

   res.set_content(
      get_json_response(
         return_codes_e::OKAY,
         "success"),
       "application/json");
}

void app_c::del_metric_stream_endpoint(const httplib::Request& req, httplib:: Response& res) {
   if (!valid_http_req(req, res, 3)) { return; }
   
   uint32_t port {0};
   std::stringstream ts_ss(req.matches[2].str());
   ts_ss >> port;

   // Should be impossible given how httplib routes things, but its always
   // best to be sure
   if (port == 0) {
      res.set_content(
         get_json_response(
            return_codes_e::BAD_REQUEST_400,
            "Invalid port given : " + req.matches[2].str()), 
         "application/json"
         );
   }

   // Queue the item to be added
   //
   _metric_streamer->del_destination(req.matches[1].str(), port);

   res.set_content(
      get_json_response(
         return_codes_e::OKAY,
         "success"),
       "application/json");
}

void app_c::db_probe(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 2)) { return; }
   
   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);

   LOG(DEBUG) << TAG("app_c::db_probe") << "Got key: " << key << "\n";

   res.set_content(run_probe(key), "application/json");
}

void app_c::db_submit(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 3)) { return; }
   
   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);
   auto value = std::string(req.matches[2]);

   LOG(DEBUG) << TAG("app_c::db_submit") << "Got key: " << key << "\n";
   LOG(DEBUG) << TAG("app_c::db_submit") << "Got value: " << value << "\n";

   res.set_content(run_submit(key, value), "application/json");
}

void app_c::db_fetch(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 2)) { return; }

   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);
   LOG(DEBUG) << TAG("app_c::db_fetch") << "Got key: " << key << "\n";

   auto [response, response_type] = run_fetch(key);

   res.set_content(response, response_type);
}

void app_c::db_remove(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 2)) { return; }

   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);
   LOG(DEBUG) << TAG("app_c::db_remove") << "Got key: " << key << "\n";

   res.set_content(run_remove(key), "application/json");
}

std::string app_c::run_probe(const std::string& key) {

   if (_registration_db->exists(key)) {
      return get_json_response(return_codes_e::OKAY, "found");
   }

   return get_json_response(return_codes_e::OKAY, "not found");
}

std::string app_c::run_submit(const std::string& key, const std::string& value) {

   if (_registration_db->store(key, value)) {
      return get_json_response(return_codes_e::OKAY, "success");
   }

   return get_json_response(return_codes_e::INTERNAL_SERVER_500, "server error");
}

std::tuple<std::string,
      std::string> app_c::run_fetch(const std::string& key) {

   auto result = _registration_db->load(key);
   
   if (!result.has_value()) {
      return {get_json_response(return_codes_e::OKAY, "not found"), "application/json"};
   }

   return {*result, "text/plain"};
}

std::string app_c::run_remove(const std::string& key){

   if (_registration_db->remove(key)) {
      return get_json_response(
            return_codes_e::OKAY,
            "success");
   }

   return get_json_response(return_codes_e::INTERNAL_SERVER_500, "server error");
}















} // namespace services
} // namespace monolith