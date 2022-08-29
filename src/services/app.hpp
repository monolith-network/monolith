#ifndef MONOLITH_SERVICES_APP_HPP
#define MONOLITH_SERVICES_APP_HPP

#include <atomic>
#include <thread>
#include <httplib.h>

#include "db/kv.hpp"
#include "interfaces/service_if.hpp"
#include "services/metric_streamer.hpp"

#include <crate/registrar/cache.hpp>

namespace monolith {
namespace services {

class app_c : public service_if {
public:
   app_c() = delete;
   app_c(const std::string& address, 
         uint32_t port,
         monolith::db::kv_c* db,
         monolith::services::metric_streamer_c* metric_streamer);

   virtual bool start() override final;
   virtual bool stop() override final;
   
private:
   enum class return_codes_e {
      OKAY = 200,
      BAD_REQUEST_400 = 400,
      INTERNAL_SERVER_500 = 500,
      NOT_IMPLEMENTED_501 = 501,
      GATEWAY_TIMEOUT_504 = 504
   };

   std::string _address;
   uint32_t _port {0};
   monolith::db::kv_c* _registration_db {nullptr};
   monolith::services::metric_streamer_c* _metric_streamer {nullptr};
   httplib::Server* _app_server {nullptr};

   void setup_endpoints();
   std::string get_json_response(const return_codes_e rc, 
                                    const std::string msg);               
   bool valid_http_req(const httplib::Request& req, 
                        httplib::Response& res, 
                        size_t expected_items);
   void http_root(const httplib::Request& req, httplib:: Response& res);

   // Stream receiver registration and de-registration
   //
   void add_metric_stream_endpoint(const httplib::Request& req, httplib:: Response& res);
   void del_metric_stream_endpoint(const httplib::Request& req, httplib:: Response& res);

   // Registration database access
   //
   void db_probe(const httplib::Request& req ,httplib:: Response &res);
   void db_submit(const httplib::Request& req ,httplib:: Response &res);
   void db_fetch(const httplib::Request& req ,httplib:: Response &res);
   void db_remove(const httplib::Request& req ,httplib:: Response &res);

   // Handlers for database access from endpoint methods
   //
   std::string run_probe(const std::string& key);
   std::string run_submit(const std::string& key, const std::string& value);
   std::tuple<std::string,
      std::string> run_fetch(const std::string& key);
   std::string run_remove(const std::string& key);
};

} // namespace services
} // namespace monolith

#endif