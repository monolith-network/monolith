#include "app.hpp"

#include <crate/externals/aixlog/logger.hpp>
#include <chrono>
#include <sstream>

using namespace std::chrono_literals;

namespace monolith {
namespace services {

app_c::app_c(const std::string& address, uint32_t port, 
         monolith::services::metric_streamer_c* metric_streamer)
         : _address(address), _port(port), _metric_streamer(metric_streamer){
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

} // namespace services
} // namespace monolith