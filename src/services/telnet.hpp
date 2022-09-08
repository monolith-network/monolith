#ifndef MONOLITH_SERVICES_TELNET_HPP
#define MONOLITH_SERVICES_TELNET_HPP

#include <string>
#include <vector>

#include "interfaces/service_if.hpp"
#include "interfaces/reloadable_if.hpp"
#include "networking/types.hpp"
#include <crate/externals/admincmd/admin_cmd_server.hpp>

namespace monolith {
namespace services {

//! \brief Telnet object
class telnet_c : public service_if {
 public:
   telnet_c() = delete;
   telnet_c(std::string access_code, monolith::networking::ipv4_host_port_s host_port,
   monolith::reloadable_if* rule_executor_reload_if);
   virtual ~telnet_c() override final;

   // From service_if
   virtual bool start() override final;
   virtual bool stop() override final;

 private:

   class server_c {
   public:
      struct session_state_s {
         uint8_t login_attempts {0};
         bool is_logged_in{false};
      };

      using admin_server_c = admincmd::admin_cmd_server_c<server_c, session_state_s>;
      using admin_conn_c = admin_server_c::connection_c;

      server_c(telnet_c* parent) : _parent(parent) {}
      bool setup(const std::string address, uint32_t port);
      void tick();

      // Required callbacks
      void on_admin_connect(admin_conn_c& conn);
      void on_admin_disconnect(admin_conn_c& conn, const std::string& error);
      void on_admin_cmd(admin_conn_c& conn, int argc, const char** argv);
   private:
      static constexpr uint8_t MAX_LOGIN_ATTEMPTS = 5;
      telnet_c* _parent {nullptr};
      admin_server_c _server;
      void hangup(admin_conn_c& conn);
      std::string handle_cmd(admin_conn_c& conn, std::vector<std::string> command);
   };
   server_c* _local_server {nullptr};
   std::string _access_code;
   monolith::networking::ipv4_host_port_s _host_port;
   monolith::reloadable_if* _rule_executor_reload_if {nullptr};

   void run();
   std::string get_banner();
   std::string get_help();
   std::string get_version();
   std::string reload_rules();
};

} // namespace services
} // namespace monolith

#endif