#ifndef MONOLITH_SERVICES_APP_HPP
#define MONOLITH_SERVICES_APP_HPP

#include <atomic>
#include <thread>
#include <httplib.h>

#include "db/kv.hpp"
#include "interfaces/service_if.hpp"
#include "services/metric_streamer.hpp"
#include "services/data_submission.hpp"
#include "heartbeats.hpp"
#include "networking/types.hpp"

namespace monolith {
namespace services {

//! \brief Main web application
class app_c : public service_if {
public:
   app_c() = delete;

   //! \brief Construct the application
   //! \param host_port The ipv4 connection information
   //! \param registrar_db The registrar database 
   //! \param metric_streamer Metric streaming service
   //! \param data_submission Data submission service
   app_c(monolith::networking::ipv4_host_port_s host_port,
         monolith::db::kv_c* registrar_db,
         monolith::services::metric_streamer_c* metric_streamer,
         monolith::services::data_submission_c* data_submission,
         monolith::heartbeats_c* heartbeat_manager);

   virtual ~app_c() override final;

   // From service_if
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
   monolith::services::data_submission_c* _data_submission {nullptr};
   monolith::heartbeats_c* _heartbeat_manager {nullptr};
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
   void metric_stream_add(const httplib::Request& req, httplib:: Response& res);
   void metric_stream_delete(const httplib::Request& req, httplib:: Response& res);
   void metric_heartbeat(const httplib::Request& req, httplib:: Response& res);

   // Registrar endpoints
   //
   void registrar_probe(const httplib::Request& req ,httplib:: Response &res);
   void registrar_add(const httplib::Request& req ,httplib:: Response &res);
   void registrar_fetch(const httplib::Request& req ,httplib:: Response &res);
   void registrar_delete(const httplib::Request& req ,httplib:: Response &res);

   // Metric endpoints
   //
   void metric_submit(const httplib::Request& req ,httplib:: Response &res);
};

} // namespace services
} // namespace monolith

#endif