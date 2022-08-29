#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

#include <toml++/toml.h>
#include <crate/externals/aixlog/logger.hpp>
#include <crate/common/common.hpp>

#include "networking/types.hpp"
#include "services/app.hpp"
#include "services/data_submission.hpp"
#include "services/metric_streamer.hpp"

using namespace std::chrono_literals;

namespace {

   struct monolith_configuration_s {
      std::string instance_name;
      std::string log_file_name;
      std::string registration_db_path;
   };
   monolith_configuration_s monolith_config; 

   struct networking_configuration_s {
      bool use_ipv6 {false};
      std::string ipv4_address;
      std::string ipv6_address;
      uint32_t http_port {8080};
      uint32_t metric_submission_port {9000};
      uint32_t registration_port {9001};
   };
   networking_configuration_s network_config;

   std::atomic<bool> active {true};
   std::atomic<bool> handling_signal {false};

   monolith::db::kv_c* registrar_database {nullptr};
   monolith::db::metric_db_c* metric_database {nullptr};

   std::unordered_map<std::string, monolith::service_if*> services;
}

// Handle signals that will trigger us to shutdown
//
void handle_signal(int signal) {

   active.store(false);

   if (handling_signal.load()) {
      return;
   }

   handling_signal.store(true);
   std::cout << "\nExiting.." << std::endl;
}

// Handle signals that will be ignored
//
void signal_ignore_handler(int signum) {
   std::cout << "Ignoring signal: " 
               << signum 
               << "\n";
}

void load_configs(std::string file) {
   
   toml::table tbl;
   try {
      tbl = toml::parse_file(file);
   } catch (const toml::parse_error& err) {
      LOG(ERROR) << TAG("load_configs") 
         << "Unable to parse file : " 
         << *err.source().path 
         << ". Description: " 
         << err.description() 
         << " (" << err.source().begin << ")\n";
      std::exit(1);
   }

   std::optional<std::string> instance_name = 
      tbl["monolith"]["instance_name"].value<std::string>();
   if (instance_name.has_value()) {
      monolith_config.instance_name = *instance_name;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'instance_name'\n";
      std::exit(1);
   } 

   std::optional<std::string> log_file_name = 
      tbl["monolith"]["log_file_name"].value<std::string>();
   if (log_file_name.has_value()) {
      monolith_config.log_file_name = *log_file_name;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'log_file_name'\n";
      std::exit(1);
   } 

   std::optional<std::string> registration_db_path = 
      tbl["monolith"]["registration_db_path"].value<std::string>();
   if (registration_db_path.has_value()) {
      monolith_config.registration_db_path = *registration_db_path;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'registration_db_path'\n";
      std::exit(1);
   } 
   
   std::optional<bool> use_ipv6 = 
      tbl["networking"]["use_ipv6"].value<bool>();
   if (use_ipv6.has_value()) {
      network_config.use_ipv6 = *use_ipv6;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'use_ipv6'\n";
      std::exit(1);
   } 

   std::optional<std::string> ipv4_address = 
      tbl["networking"]["ipv4_address"].value<std::string>();
   if (ipv4_address.has_value()) {
      network_config.ipv4_address = *ipv4_address;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'ipv4_address'\n";
      std::exit(1);
   } 
   
   std::optional<std::string> ipv6_address = 
      tbl["networking"]["ipv6_address"].value<std::string>();
   if (ipv6_address.has_value()) {
      network_config.ipv6_address = *ipv6_address;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'ipv6_address'\n";
      std::exit(1);
   } 

   std::optional<uint32_t> http_port = 
      tbl["networking"]["http_port"].value<uint32_t>();
   if (http_port.has_value()) {
      network_config.http_port = *http_port;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'http_port'\n";
      std::exit(1);
   } 

   std::optional<uint32_t> metric_submission_port = 
      tbl["networking"]["metric_submission_port"].value<uint32_t>();
   if (metric_submission_port.has_value()) {
      network_config.metric_submission_port = *metric_submission_port;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing config for 'metric_submission_port'\n";
      std::exit(1);
   } 
}

void start_services() {

   LOG(INFO) << TAG("start_services") << "Starting services\n";

   registrar_database = new monolith::db::kv_c(monolith_config.registration_db_path);

   auto metric_streamer = new monolith::services::metric_streamer_c();

   if (!metric_streamer->start()) {
      LOG(ERROR) << TAG("start_services") << "Failed to start metric streamer\n";
      std::exit(1);
   }

   auto data_submission = new monolith::services::data_submission_c(
      monolith::networking::ipv4_host_port_s{
         network_config.ipv4_address,
         network_config.metric_submission_port
      },
      registrar_database,
      metric_streamer,
      metric_database
   );

   if (!data_submission->start()) {
      LOG(ERROR) << TAG("start_services") 
                  << "Failed to start data submission server\n";
      std::exit(1);
   }

   auto app_service = new monolith::services::app_c(
      monolith::networking::ipv4_host_port_s{
         network_config.ipv4_address, 
         network_config.http_port
      },
      registrar_database,
      metric_streamer,
      data_submission
   );

   if (!app_service->start()) {
      LOG(ERROR) << TAG("start_services") 
                  << "Failed to start application server\n";
      std::exit(1);
   }

   services["data_submission"] = data_submission;
   services["metric_stream"] = metric_streamer;
   services["application"] = app_service;
}

void stop_services() {

   LOG(INFO) << TAG("stop_services") << "Stopping services\n";

   for (auto& [service_name, service] : services) {
      if (service && service->is_running()) {
         if (!service->stop()) {
            LOG(ERROR) << TAG("stop_services") 
                        << "Failed to stop service : " 
                        << service_name 
                        << "\n";
         } else {
            delete service;
         }
      }
   }

   if (registrar_database) {
      delete registrar_database;
   }
}

int main(int argc, char** argv) {

   if (argc != 2) {
      std::cout << "Usage : " << argv[0] << " <config>.toml" << std::endl;
      return 1; 
   }

   load_configs(argv[1]);
   
   crate::common::setup_logger("monolith_app", AixLog::Severity::debug);

   // setup signal handlers
   //
   signal(SIGHUP , handle_signal);   /* Hangup the process */ 
   signal(SIGINT , handle_signal);   /* Interrupt the process */ 
   signal(SIGQUIT, handle_signal);   /* Quit the process */ 
   signal(SIGILL , handle_signal);   /* Illegal instruction. */ 
   signal(SIGTRAP, handle_signal);   /* Trace trap. */ 
   signal(SIGABRT, handle_signal);   /* Abort. */
   signal(SIGPIPE, signal_ignore_handler); 

   if (network_config.use_ipv6) {
      std::cerr << "IPV6 is not yet supported" << std::endl;
      std::exit(1);
   }

   start_services();

   while(active.load()) {
      std::this_thread::sleep_for(100ms);
   }

   stop_services();

   return 0;
}