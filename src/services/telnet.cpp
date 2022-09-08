#include "telnet.hpp"
#include <crate/externals/aixlog/logger.hpp>

#include "version.hpp"

namespace monolith {
namespace services {

using namespace std::chrono_literals;

telnet_c::telnet_c(std::string access_code, monolith::networking::ipv4_host_port_s host_port, monolith::reloadable_if *rule_executor)
   : _access_code(access_code), _host_port(host_port), _rule_executor_reload_if(rule_executor) {

   _local_server = new server_c(this);
}

telnet_c::~telnet_c() {
   stop();
   delete _local_server;
   _local_server = nullptr;
}

bool telnet_c::start() {

   if (p_running.load()) {
      LOG(WARNING) << TAG("telnet_c::start")
                   << "Telnet service already started\n";
      return true;
   }

   if (!_local_server->setup(_host_port.address, _host_port.port)) {
      LOG(WARNING) << TAG("telnet_c::start")
                   << "Failed to init telnet server\n";
      return false;
   }

   p_running.store(true);
   p_thread = std::thread(&telnet_c::run, this);
   LOG(TRACE) << TAG("telnet_c::start") << "Telnet service started on : " << _host_port.port;
   return true;
}

bool telnet_c::stop() {
   p_running.store(false);
   if (p_thread.joinable()) {
      p_thread.join();
   }
   return true;
}

void telnet_c::run() {
   while (p_running.load()) {
      _local_server->tick();
      std::this_thread::sleep_for(100ms);
   }
}

bool telnet_c::server_c::setup(const std::string address, uint32_t port) {
   if (!_server.init(address.c_str(), port)) {
      LOG(WARNING) << TAG("telnet_c::server_c::setup")
                   << "Unable to start telnet service\n";
      return false;
   }
   return true;
}

void telnet_c::server_c::tick() {

   _server.poll(this);
}

std::string telnet_c::get_banner() {
   return R"(

   Monolith Telnet Service
    - Use `help` for a list of commands
   
)";
}

std::string telnet_c::get_help() {

   return R"(
help                 - Show this message
quit                 - Exit telnet session
login <password>     - Log into monolith

--- The following require a user log in ---

version              - Get the version information
stats                - Retrieve statistics
reload <target>      - Reload a given target
   valid targets:
      rules          - The lua rules script

   )";
}

std::string telnet_c::get_version() {
   auto [name, hash, semver] = monolith::get_version_info().get_data();
   return "Instance Name: " + name + "\nBuild Hash: " + hash + "\n" + 
         "Semver: " + semver.major + "." + semver.minor + "." + semver.patch + "\n\n";
}

std::string telnet_c::reload_rules() {
   if (_rule_executor_reload_if) {
      if (!_rule_executor_reload_if->reload()) {
         return "< failed to reload rule executor >";
      }
      return "< rules reloaded >";
   }
   return "< rule executor not set >";
}

void telnet_c::server_c::on_admin_connect(admin_conn_c& conn) {
   struct sockaddr_in addr;
   conn.get_peer_name(addr);

   LOG(INFO) << TAG("telnet_c::server_c::on_admin_connect")
                  << "Connection from: " 
                  << inet_ntoa(addr.sin_addr) 
                  << ":" 
                  << ntohs(addr.sin_port) 
                  << "\n";

   auto banner = _parent->get_banner();
   conn.user_data.is_logged_in = false;
   conn.write(banner.data(), banner.size());
}

void telnet_c::server_c::on_admin_disconnect(admin_conn_c& conn, const std::string& error) {
   struct sockaddr_in addr;

   conn.get_peer_name(addr);
   LOG(INFO) << TAG("telnet_c::server_c::on_admin_disconnect")
                  << "Disconnect from: " 
                  << inet_ntoa(addr.sin_addr) 
                  << ":" 
                  << ntohs(addr.sin_port) 
                  << ", error: " 
                  << error 
                  << "\n";
}

void telnet_c::server_c::on_admin_cmd(admin_conn_c& conn, int argc, const char** argv) {
   std::vector<std::string> command(argv, argv + argc);
   auto response = handle_cmd(conn, command);

   if (response.empty()) {
      return;
   }

   if (response.back() != '\n') {
      response.push_back('\n');
   }

   conn.write(response.data(), response.size());
}

void telnet_c::server_c::hangup(admin_conn_c& conn) {
   conn.user_data = session_state_s{};
   conn.close();
}

std::string telnet_c::server_c::handle_cmd(admin_conn_c& conn, std::vector<std::string> command) {
   
   if (command.empty()) {
      return "\n";
   }

   if (command[0] == "quit") {
      hangup(conn);
   }

   if (command[0] == "help") {
      return _parent->get_help();
   }

   /*
         Check for max login attempts
   */
   if (!conn.user_data.is_logged_in && 
         conn.user_data.login_attempts >= MAX_LOGIN_ATTEMPTS) {
      return "Maximum login attempts exceeded. Use `quit` and try again later";
   }

   /*
         Log in
   */
   if (command[0] == "login") {

      if (conn.user_data.is_logged_in) {
         return "Already logged in";
      }

      if (command.size() < 2) {
         return "Missing field \"password\"";
      }

      if (command[1] != _parent->_access_code) {
         conn.user_data.login_attempts++;
         return "Invalid password";
      }

      // Log the user in
      conn.user_data.is_logged_in = true;
      return "Login success";
   }

   /*
         Ensure user is logged in before continuing
   */
   if (!conn.user_data.is_logged_in) {
      return "Must log in";
   }

   /*
         Version Info
   */
   if (command[0] == "version") {
      return _parent->get_version();
   }

   /*
         Statistics Info
   */
   if (command[0] == "stats") {
      return "TODO: Return runtime statistics";
   }
   
   /*
         Reload items
   */
   if (command[0] == "reload") {

      if (command.size() < 2) {
         return "Missing field \"target\"";
      }

      if (command[1] == "rules") {
         return _parent->reload_rules();
      }

      return "Unknown target \"" + command[1] + "\"";
   }

   return "Unknown command";
}

} // namespace services
} // namespace monolith