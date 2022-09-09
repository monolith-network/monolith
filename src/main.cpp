#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

#include <crate/common/common.hpp>
#include <crate/externals/aixlog/logger.hpp>
#include <crate/metrics/streams/stream_receiver_if.hpp>
#include <toml++/toml.h>

#include "alert/alert.hpp"
#include "alert/sms/twilio/twilio.hpp"
#include "heartbeats.hpp"
#include "networking/types.hpp"
#include "portal/portal.hpp"
#include "services/action_dispatch.hpp"
#include "services/app.hpp"
#include "services/data_submission.hpp"
#include "services/metric_db.hpp"
#include "services/metric_streamer.hpp"
#include "services/rule_executor.hpp"
#include "services/telnet.hpp"

#include "version.hpp"

using namespace std::chrono_literals;

namespace {

/*
      Application configuration
*/
struct app_configuration_s {
   std::string instance_name;
   std::string log_file_name;
   std::string registration_db_path;
   std::string rule_script;
};
app_configuration_s app_config;

/*
      Networking configuration
*/
struct networking_configuration_s {
   std::string ipv4_address;
   uint32_t http_port{8080};
   bool telnet_enabled{false};
   uint32_t telnet_port{25565};
   std::string telnet_access_code;
};
networking_configuration_s network_config;

/*
      Database configuration
*/
struct metrics_configuration_c {
   bool save_metrics{false};
   bool stream_metrics{false};
   uint64_t metric_expiration_time_sec{0};
   std::string database_path;

};
metrics_configuration_c metrics_config;

/*
      Alert configuration
*/
monolith::alert::alert_manager_c::configuration_c alerts_config;

/*
      Main app control atomics
*/
std::atomic<bool> active{true};
std::atomic<bool> handling_signal{false};

/*
      Shared non-service objects
*/
monolith::portal::portal_c *portal;
monolith::heartbeats_c heartbeat_manager;
monolith::db::kv_c *registrar_database{nullptr};
std::vector<crate::metrics::streams::stream_receiver_if>
    internal_stream_receivers;

/*
      Services
*/
monolith::services::data_submission_c *data_submission{nullptr};
monolith::services::metric_db_c *metric_database{nullptr};
monolith::services::rule_executor_c *rule_executor{nullptr};
monolith::services::action_dispatch_c *action_dispatch{nullptr};
monolith::services::metric_streamer_c *metric_streamer{nullptr};
monolith::services::telnet_c *telnet{nullptr};
monolith::services::app_c *app_service{nullptr};

/*
      Alert backends
*/
monolith::sms::twilio_c *twilio_backend{nullptr};

} // namespace

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
   std::cout << "Ignoring signal: " << signum << "\n";
}

// Load a configuration file
//
void load_configs(std::string file) {

   toml::table tbl;
   try {
      tbl = toml::parse_file(file);
   } catch (const toml::parse_error &err) {
      LOG(ERROR) << TAG("load_configs")
                 << "Unable to parse file : " << *err.source().path
                 << ". Description: " << err.description() << " ("
                 << err.source().begin << ")\n";
      std::exit(1);
   }

   /*

         Load monolith configurations

   */
   std::optional<std::string> instance_name =
       tbl["monolith"]["instance_name"].value<std::string>();
   if (instance_name.has_value()) {
      app_config.instance_name = *instance_name;
   } else {
      LOG(ERROR) << TAG("load_config")
                 << "Missing monolith config for 'instance_name'\n";
      std::exit(1);
   }

   std::optional<std::string> log_file_name =
       tbl["monolith"]["log_file_name"].value<std::string>();
   if (log_file_name.has_value()) {
      app_config.log_file_name = *log_file_name;
   } else {
      LOG(ERROR) << TAG("load_config")
                 << "Missing monlith config for 'log_file_name'\n";
      std::exit(1);
   }

   std::optional<std::string> registration_db_path =
       tbl["monolith"]["registration_db_path"].value<std::string>();
   if (registration_db_path.has_value()) {
      app_config.registration_db_path = *registration_db_path;
   } else {
      LOG(ERROR) << TAG("load_config")
                 << "Missing monolith config for 'registration_db_path'\n";
      std::exit(1);
   }

   std::optional<std::string> rule_script =
       tbl["monolith"]["rule_script"].value<std::string>();
   if (rule_script.has_value()) {
      app_config.rule_script = *rule_script;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing monlith config for 'rule_script'\n";
      std::exit(1);
   }

   if (!std::filesystem::is_regular_file(app_config.rule_script)) {
      LOG(ERROR) << TAG("load_config")
                 << "Given rule script: " << app_config.rule_script
                 << " does not exist\n";
      std::exit(1);
   }

   /*

         Load networking configurations

   */
   std::optional<std::string> ipv4_address =
       tbl["networking"]["ipv4_address"].value<std::string>();
   if (ipv4_address.has_value()) {
      network_config.ipv4_address = *ipv4_address;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing networking config for 'ipv4_address'\n";
      std::exit(1);
   }

   std::optional<uint32_t> http_port =
       tbl["networking"]["http_port"].value<uint32_t>();
   if (http_port.has_value()) {
      network_config.http_port = *http_port;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing networking config for 'http_port'\n";
      std::exit(1);
   }

   std::optional<bool> telnet_enabled = tbl["networking"]["telnet_enabled"].value<bool>();
   if (telnet_enabled.has_value()) {
      network_config.telnet_enabled = *telnet_enabled;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing networking config for 'telnet_enabled'\n";
      std::exit(1);
   }

   if (network_config.telnet_enabled) {
      std::optional<std::string> telnet_access_code =
         tbl["networking"]["telnet_access_code"].value<std::string>();
      if (telnet_access_code.has_value()) {
         network_config.telnet_access_code = *telnet_access_code;
      } else {
         LOG(ERROR) << TAG("load_config") << "Missing networking config for telnet 'telnet access code'\n";
         std::exit(1);
      }

      if (network_config.telnet_access_code.empty()) {
         LOG(ERROR) << TAG("load_config") << "Telnet access code can not be empty\n";
         std::exit(1);
      }

      std::optional<uint32_t> telnet_port =
         tbl["networking"]["telnet_port"].value<uint32_t>();
      if (telnet_port.has_value()) {
         network_config.telnet_port = *telnet_port;
      } else {
         LOG(ERROR) << TAG("load_config") << "Missing networking config for 'telnet port'\n";
         std::exit(1);
      }
   }

   /*

         Load metrics configurations

   */

   std::optional<bool> save_metrics = tbl["metrics"]["save_metrics"].value<bool>();
   if (save_metrics.has_value()) {
      metrics_config.save_metrics = *save_metrics;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing metric_database config for 'save_metrics'\n";
      std::exit(1);
   }

   // Check if we want the ability to stream metrics
   std::optional<bool> stream_metrics = tbl["metrics"]["enable_metric_streamer"].value<bool>();
   if (stream_metrics.has_value()) {
      metrics_config.stream_metrics = *stream_metrics;
   } else {
      LOG(ERROR) << TAG("load_config") << "Missing metric_database config for 'enable_metric_streamer'\n";
      std::exit(1);
   }

   if (metrics_config.save_metrics) {

      std::optional<uint64_t> metric_expiration_time_sec = tbl["metrics"]["metric_expiration_time_sec"].value<uint64_t>();
      if (metric_expiration_time_sec.has_value()) {
         metrics_config.metric_expiration_time_sec = *metric_expiration_time_sec;
      } else {
         LOG(ERROR) << TAG("load_config") << "Missing metric_database config for 'metric_expiration_time_sec'\n";
         std::exit(1);
      }

      std::optional<std::string> metric_db_path =
         tbl["metrics"]["path"].value<std::string>();
      if (metric_db_path.has_value()) {
         metrics_config.database_path = *metric_db_path;
      } else {
         LOG(ERROR) << TAG("load_config")
                  << "Missing metric_database config for 'path'\n";
         std::exit(1);
      }
   }

   /*

         Load alert configurations

   */
   std::optional<uint64_t> max_alert_sends =
       tbl["alerts"]["max_alert_sends"].value<uint64_t>();
   if (max_alert_sends.has_value()) {
      alerts_config.max_alert_sends = *max_alert_sends;
   } else {
      LOG(ERROR) << TAG("load_config")
                 << "Missing alerts config for 'max_alert_sends'\n";
      std::exit(1);
   }

   std::optional<double> alert_cooldown_seconds =
       tbl["alerts"]["alert_cooldown_seconds"].value<double>();
   if (alert_cooldown_seconds.has_value()) {
      alerts_config.alert_cooldown_seconds = *alert_cooldown_seconds;
   } else {
      LOG(ERROR) << TAG("load_config")
                 << "Missing alerts config for 'alert_cooldown_seconds'\n";
      std::exit(1);
   }

   /*

         Load optional twilio configurations

   */
   if (tbl["twilio"]) {

      monolith::sms::twilio_c::configuration_c twilio_config;
      std::optional<std::string> twilio_account_id =
         tbl["twilio"]["account_sid"].value<std::string>();
      if (twilio_account_id.has_value()) {
         twilio_config.account_id = *twilio_account_id;
      } else {
         LOG(ERROR) << TAG("load_config")
                  << "Twilio config missing 'account_sid'\n";
         std::exit(1);
      }

      // Get the auth token
      //
      std::optional<std::string> twilio_auth_token =
         tbl["twilio"]["auth_token"].value<std::string>();
      if (twilio_auth_token.has_value()) {
         twilio_config.auth_token = *twilio_auth_token;
      } else {
         LOG(ERROR) << TAG("load_config")
                  << "Twilio config missing 'auth_token'\n";
         std::exit(1);
      }

      // Get the from
      //
      std::optional<std::string> twilio_from =
         tbl["twilio"]["from"].value<std::string>();
      if (twilio_from.has_value()) {
         twilio_config.from = *twilio_from;
      } else {
         LOG(ERROR) << TAG("load_config") << "Twilio config missing 'from'\n";
         std::exit(1);
      }

      // Obtain the destination phone numbers
      //
      auto twilio_to = tbl["twilio"]["to"];
      if (!twilio_to) {
         LOG(ERROR) << TAG("load_config") << "Twilio config missing 'to'\n";
         std::exit(1);
      }

      auto arr = twilio_to.as_array();
      if (!arr) {
         LOG(ERROR) << TAG("load_config") << "Twilio 'to' must be an array\n";
         std::exit(1);
      }

      for (auto&& element : *arr) {

         auto e = element.value<std::string>();
         if (!e.has_value()) {
            LOG(ERROR) << TAG("load_config") << "Twilio 'to' items must be stringed phone numbers\n";
            std::exit(1);
         }

         twilio_config.to.push_back(*e);
      }

      // If twilio was configured we need to instantiate the object
      //
      twilio_backend = new monolith::sms::twilio_c(twilio_config);

      // Ensure that the backend is setup
      //
      if (!twilio_backend->setup()) {
         LOG(ERROR) << TAG("load_config") << "Failed to setup twilio backend\n";
         delete twilio_backend;
         twilio_backend = nullptr;
      } else {

         // If the thing was setup then we can set the alert's sms backend to
         // the twilio pointer
         alerts_config.sms_backend = twilio_backend;
      }
   }
}

void cleanup() {

   if (telnet) {
      telnet->stop();
      delete telnet;
   }

   if (app_service) {
      app_service->stop();
      delete app_service;
   }

   if (data_submission) {
      data_submission->stop();
      delete data_submission;
   }

   if (rule_executor) {
      rule_executor->stop();
      delete rule_executor;
   }

   if (action_dispatch) {
      action_dispatch->stop();
      delete action_dispatch;
   }

   if (metric_streamer) {
      metric_streamer->stop();
      delete metric_streamer;
   }

   if (metric_database) {
      metric_database->stop();
      delete metric_database;
   }

   if (registrar_database) {
      delete registrar_database;
   }
}

void start_services() {

   LOG(INFO) << TAG("start_services") << "Starting services\n";

   registrar_database =
       new monolith::db::kv_c(app_config.registration_db_path);

   // Start the metric streamer if its enabled
   if (metrics_config.stream_metrics) {
      metric_streamer = new monolith::services::metric_streamer_c();

      if (!metric_streamer->start()) {
         LOG(ERROR) << TAG("start_services")
                  << "Failed to start metric streamer\n";
         cleanup();
         std::exit(1);
      }
   }

   if (metrics_config.save_metrics) {
      metric_database =
         new monolith::services::metric_db_c(metrics_config.database_path, 
                                             metrics_config.metric_expiration_time_sec);
      if (!metric_database->start()) {
         LOG(ERROR) << TAG("start_services")
                  << "Failed to start metric database service\n";
         cleanup();
         std::exit(1);
      }
   }

   action_dispatch =
       new monolith::services::action_dispatch_c(registrar_database);

   if (!action_dispatch->start()) {
      LOG(ERROR) << TAG("start_services")
                 << "Failed to start action dispatch service\n";
      cleanup();
      std::exit(1);
   }

   rule_executor = new monolith::services::rule_executor_c(
       app_config.rule_script, alerts_config, action_dispatch);
   if (!rule_executor->open()) {
      LOG(ERROR) << TAG("start_services")
                 << "Failed to open rule executor script\n";
      cleanup();
      std::exit(1);
   }

   if (!rule_executor->start()) {
      LOG(ERROR) << TAG("start_services") << "Failed to start rule executor\n";
      cleanup();
      std::exit(1);
   }

   data_submission = new monolith::services::data_submission_c(
       registrar_database, metric_streamer, metric_database, rule_executor,
       &heartbeat_manager);

   if (!data_submission->start()) {
      LOG(ERROR) << TAG("start_services")
                 << "Failed to start data submission server\n";
      cleanup();
      std::exit(1);
   }

   if (network_config.telnet_enabled) {

      LOG(WARNING) << TAG("start_services") 
                << "Telnet has been enabled.\n Please ensure that port rules on the machine doesn't expose port `" 
                << network_config.telnet_port << "` to be public facing.\n Telnet is not a secure protocol."
                << "\n Telnet should only be used to locally reconfigure and control a running intstance of Monolith.\n";

      telnet = new monolith::services::telnet_c(network_config.telnet_access_code, 
         monolith::networking::ipv4_host_port_s(network_config.ipv4_address, network_config.telnet_port),
         rule_executor
      );

      if (!telnet->start()) {
         LOG(ERROR) << TAG("start_services")
                  << "Failed to start telnet server\n";
         cleanup();
         std::exit(1);
       }
   }

   portal = new monolith::portal::portal_c(registrar_database, metric_database);

   app_service = new monolith::services::app_c(
       monolith::networking::ipv4_host_port_s{network_config.ipv4_address,
                                              network_config.http_port},
       registrar_database, metric_streamer, data_submission, metric_database,
       &heartbeat_manager, portal);

   app_service->serve_static_resources(true);

   if (!app_service->start()) {
      LOG(ERROR) << TAG("start_services")
                 << "Failed to start application server\n";
      cleanup();
      std::exit(1);
   }
}

void stop_services() {

   LOG(INFO) << TAG("stop_services") << "Stopping services\n";
   cleanup();
}

void display_version_info() {
   auto [name, hash, semver] = monolith::get_version_info().get_data();

   std::cout << name << " | Version: " << semver.major << "." << semver.minor
             << "." << semver.patch << " | Build hash: " << hash << std::endl;
}

int main(int argc, char **argv) {

   if (argc != 2) {
      std::cout << "Usage : " << argv[0] << " <config>.toml" << std::endl;
      return 1;
   }

   load_configs(argv[1]);

   crate::common::setup_logger("monolith_app", AixLog::Severity::trace);

   // setup signal handlers
   //
   signal(SIGHUP, handle_signal);  /* Hangup the process */
   signal(SIGINT, handle_signal);  /* Interrupt the process */
   signal(SIGQUIT, handle_signal); /* Quit the process */
   signal(SIGILL, handle_signal);  /* Illegal instruction. */
   signal(SIGTRAP, handle_signal); /* Trace trap. */
   signal(SIGABRT, handle_signal); /* Abort. */
   signal(SIGPIPE, signal_ignore_handler);

   start_services();

   display_version_info();

   while (active.load()) {
      std::this_thread::sleep_for(100ms);
   }

   stop_services();

   // Cleanup others
   if (twilio_backend) {
      delete twilio_backend;
   }

   return 0;
}
