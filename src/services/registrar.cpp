#include "registrar.hpp"
#include <crate/externals/aixlog/logger.hpp>
#include <functional>
#include <chrono>

using namespace std::chrono_literals;

namespace monolith {
namespace services {

registrar_c::registrar_c(const std::string& address, 
                   const short port, 
                   monolith::db::kv_c* db) : 
   _address(address),
   _port(port),
   _db(db) {

   _http_server = new httplib::Server();

   LOG(DEBUG) << TAG("registrar_c::registrar_c") 
               << "Server created with port: " 
               << port 
               << "\n";
               
   _http_server->set_logger([](const httplib::Request &req, const httplib::Response &res) {

      // req.matches[0] should always be present but a
      // segfault was caught twice coming from
      // accessing matches[0]
      std::string endpoint{};
      if (!req.matches.empty()) {
         endpoint = ", endpoint:" + req.matches[0].str();
      }

      LOG(DEBUG) << TAG("httplib") 
                  << "[address:" << req.remote_addr 
                  << ", port:" << req.remote_port
                  << ", agent:" << req.get_header_value("User-Agent")
                  << endpoint
                  << ", method:" << req.method
                  << ", body:" << req.body
                  << "]\n";
   });
}

registrar_c::~registrar_c() {

   if (_http_server->is_running()) {
      _http_server->stop();
   }

   if (p_thread.joinable()) {
      p_thread.join();
   }

   delete _http_server;
}

bool registrar_c::start() {
   if (p_running.load()) {
      return true;
   }

   LOG(INFO) << TAG("registrar_c::start") << "Starting server [" << _address << ":" << _port << "]\n";
   
   setup_endpoints();

   auto http_runner = [](httplib::Server* server, const std::string address, const short port) {
      server->listen(address, port);
   };

   p_running.store(true);
   p_thread = std::thread(http_runner, _http_server, _address, _port);

   std::this_thread::sleep_for(10ms);

   if (!_http_server->is_running()) {
      LOG(ERROR) << TAG("registrar_c::start") << "Failed to start http server\n";
      p_running.store(false);
      return false;
   }

   return true;
}

bool registrar_c::stop() {
   if (!p_running.load()) {
      return true;
   }

   _http_server->stop();

   if (p_thread.joinable()) {
      p_thread.join();
   }

   p_running.store(false);
   return true;
}

std::string registrar_c::get_json_response(const return_codes_e rc, const std::string msg) {
   return "{\"status\":" + 
      std::to_string(static_cast<uint32_t>(rc)) +
      ",\"data\":\"" +
         msg + 
         "\"}";
}

std::string registrar_c::get_json_raw_response(const return_codes_e rc, const std::string json) {
   return "{\"status\":" + 
      std::to_string(static_cast<uint32_t>(rc)) +  
      ",\"data\":" +
      json + 
      "}";
}

void registrar_c::setup_endpoints() {

   _http_server->Get("/", 
      std::bind(&registrar_c::http_root, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));

   _http_server->Get(R"(/probe/(.*?))", 
      std::bind(&registrar_c::http_probe, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));

   _http_server->Get(R"(/submit/(.*?)/(.*?))", 
      std::bind(&registrar_c::http_submit, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));

   _http_server->Get(R"(/fetch/(.*?))", 
      std::bind(&registrar_c::http_fetch, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));

   _http_server->Get(R"(/delete/(.*?))", 
      std::bind(&registrar_c::http_remove, 
                  this, 
                  std::placeholders::_1, 
                  std::placeholders::_2));
}

bool registrar_c::valid_http_req(const httplib::Request& req, httplib::Response& res, 
                              size_t expected_items) {
   if (req.matches.size() < expected_items) {
      res.set_content(
         get_json_response(
            return_codes_e::BAD_REQUEST_400,
            "Json data not detected"), 
         "application/json"
         );
      return false;
   }
   return true;
}

void registrar_c::http_root(const httplib::Request& req ,httplib:: Response &res) {
   res.set_content(
      get_json_response(
         return_codes_e::OKAY,
         "success"),
       "application/json");
}

void registrar_c::http_probe(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 2)) { return; }
   
   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);

   LOG(DEBUG) << TAG("registrar_c::http_probe") << "Got key: " << key << "\n";

   res.set_content(run_probe(key), "application/json");
}

void registrar_c::http_submit(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 3)) { return; }
   
   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);
   auto value = std::string(req.matches[2]);

   LOG(DEBUG) << TAG("registrar_c::http_submit") << "Got key: " << key << "\n";
   LOG(DEBUG) << TAG("registrar_c::http_submit") << "Got value: " << value << "\n";

   res.set_content(run_submit(key, value), "application/json");
}

void registrar_c::http_fetch(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 2)) { return; }

   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);
   LOG(DEBUG) << TAG("registrar_c::http_fetch") << "Got key: " << key << "\n";

   auto [response, response_type] = run_fetch(key);

   res.set_content(response, response_type);
}

void registrar_c::http_remove(const httplib::Request& req, httplib::Response& res) {
   if (!valid_http_req(req, res, 2)) { return; }

   auto endpoint = req.matches[0];
   auto key = std::string(req.matches[1]);
   LOG(DEBUG) << TAG("registrar_c::http_remove") << "Got key: " << key << "\n";

   res.set_content(run_remove(key), "application/json");
}

std::string registrar_c::run_probe(const std::string& key) {

   if (_db->exists(key)) {
      return get_json_response(return_codes_e::OKAY, "found");
   }

   return get_json_response(return_codes_e::OKAY, "not found");
}

std::string registrar_c::run_submit(const std::string& key, const std::string& value) {

   if (_db->store(key, value)) {
      return get_json_response(return_codes_e::OKAY, "success");
   }

   return get_json_response(return_codes_e::INTERNAL_SERVER_500, "server error");
}

std::tuple<std::string,
      std::string> registrar_c::run_fetch(const std::string& key) {

   auto result = _db->load(key);
   
   if (!result.has_value()) {
      return {get_json_response(return_codes_e::OKAY, "not found"), "application/json"};
   }

   return {*result, "text/plain"};
}

std::string registrar_c::run_remove(const std::string& key){

   if (_db->remove(key)) {
      return get_json_response(
            return_codes_e::OKAY,
            "success");
   }

   return get_json_response(return_codes_e::INTERNAL_SERVER_500, "server error");
}

} // namespace services 
} // namespace monolith 