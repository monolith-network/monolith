#include <libutil/random/entry.hpp>
#include <crate/common/common.hpp>
#include <crate/registrar/helper.hpp>
#include <crate/registrar/node_v1.hpp>
#include <crate/metrics/streams/helper.hpp>
#include <crate/metrics/streams/stream_data_v1.hpp>
#include <crate/networking/message_writer.hpp>
#include <crate/networking/message_server.hpp>
#include <crate/networking/message_receiver_if.hpp>
#include "services/app.hpp"
#include "services/metric_streamer.hpp"
#include "services/data_submission.hpp"
#include <crate/metrics/helper.hpp>
#include "heartbeats.hpp"
#include <filesystem>
#include <queue>
#include <vector>

// This has to be included last as there is a known issue
// described here: https://github.com/cpputest/cpputest/issues/982
//
#include "CppUTest/TestHarness.h"

namespace {
   
   static constexpr char ADDRESS[] = "0.0.0.0";
   static constexpr uint32_t HTTP_PORT = 8080;
   static constexpr uint32_t DATA_PORT = 4096;
   static constexpr uint32_t RECEIVE_PORT = 5042;
   static constexpr char REGISTRAR_DB[] = "test_streaming_registrar.db";
   static constexpr char LOGS[] = "test_streaming";
   static constexpr size_t NUM_NODES = 2;
   static constexpr size_t NUM_SENSORS_PER_NODE = 2;
   static constexpr size_t NUM_READINGS_PER_SENSOR = 50;

   monolith::db::kv_c* registrar_db {nullptr};
   monolith::services::metric_streamer_c* metric_streamer {nullptr};
   monolith::services::data_submission_c* data_submission {nullptr};
   monolith::db::metric_db_c* metric_db {nullptr};
   monolith::services::app_c* app {nullptr};
   monolith::heartbeats_c heartbeat_manager;
   crate::networking::message_server_c* metric_stream_server {nullptr};
   std::vector<crate::registrar::node_v1_c> nodes;
   std::vector<crate::metrics::sensor_reading_v1_c> readings;
   std::vector<crate::metrics::sensor_reading_v1_c> received_readings;

   class metric_stream_receiver_c : public crate::networking::message_receiver_if {
   public:
      virtual void receive_message(std::string message) override final {
         crate::metrics::streams::stream_data_v1_c data;
         data.decode_from(message);

         // Copy received data into vectors
         auto [timestamp, sequence, metric_data] = data.get_data();
         received_readings.insert(
            received_readings.end(),
            metric_data.begin(),
            metric_data.end()
         );
      }
   };

   metric_stream_receiver_c metric_stream_receiver;
}
TEST_GROUP(stream_test)
{
   void setup() {
      crate::common::setup_logger(LOGS, AixLog::Severity::error);
      registrar_db = new monolith::db::kv_c(REGISTRAR_DB);
      metric_db = new monolith::db::metric_db_c();
      metric_streamer = new monolith::services::metric_streamer_c();
      data_submission = new monolith::services::data_submission_c(
         monolith::networking::ipv4_host_port_s{
            ADDRESS, DATA_PORT
         },
         registrar_db,
         metric_streamer,
         metric_db,
         &heartbeat_manager
      );
      app = new monolith::services::app_c(
         monolith::networking::ipv4_host_port_s{
            ADDRESS, HTTP_PORT
         },
         registrar_db,
         metric_streamer,
         data_submission,
         &heartbeat_manager
      );
      metric_stream_server = new crate::networking::message_server_c(ADDRESS, RECEIVE_PORT, &metric_stream_receiver);
   }

   void teardown() {
      delete registrar_db;
      delete metric_db;
      delete metric_streamer;
      delete data_submission;
      delete app;
      delete metric_stream_server;

      std::filesystem::remove_all(std::string(LOGS) + std::string(".log"));
      std::filesystem::remove_all(REGISTRAR_DB);
   }
};

TEST(stream_test, stream_test_full)
{
   using namespace std::chrono_literals;

   // ---------- Start all the services ----------
   CHECK_TRUE(metric_stream_server->start());
   CHECK_TRUE(metric_streamer->start());
   CHECK_TRUE(data_submission->start());
   CHECK_TRUE(app->start());

   struct test_params {
      crate::metrics::helper_c::endpoint_type_e endpoint_type;
      uint32_t metric_port {0};
   };

   std::vector<test_params> tests = {
      {crate::metrics::helper_c::endpoint_type_e::TCP, DATA_PORT},
      {crate::metrics::helper_c::endpoint_type_e::HTTP, HTTP_PORT}
   };

   // Test metric submission on TCP HTTP endpoints
   //
   for (auto& test : tests) {

      nodes.clear();
      readings.clear();
      received_readings.clear();

      // Create nodes/sensors/readings
      //
      for (size_t i = 0; i < NUM_NODES; i++) {
         
         crate::registrar::node_v1_c node;
         node.set_id(std::to_string(i));
         
         for (size_t j = 0; j < NUM_SENSORS_PER_NODE; j++) {
            crate::registrar::node_v1_c::sensor sensor;
            sensor.id = std::to_string(i) + ":" + std::to_string(j);
            sensor.description = "[desc]";
            sensor.type = "[type]";

            node.add_sensor(sensor);

            for (size_t k = 0; k < NUM_READINGS_PER_SENSOR; k++) {

               crate::metrics::sensor_reading_v1_c reading(0, std::to_string(i), sensor.id, static_cast<double>(k));
               reading.stamp();

               readings.push_back(reading);
            }
         }
         nodes.push_back(node);
      }

      // Submit nodes
      //
      crate::registrar::helper_c registrar_helper(ADDRESS, HTTP_PORT);
      for (auto& node : nodes) {
         if (registrar_helper.submit(node) != crate::registrar::helper_c::result::SUCCESS) {
            FAIL("Failed to submit node to registrar");
         }
      }

      // Register metric receiver
      //
      crate::metrics::streams::helper_c helper(ADDRESS, HTTP_PORT);
      auto result = helper.register_as_metric_stream_receiver(
         ADDRESS ,
         RECEIVE_PORT
      );

      if (result != crate::metrics::streams::helper_c::result::SUCCESS) {
         FAIL("Failed to register object as a metric stream receiver");
      }

      std::this_thread::sleep_for(4s);

      auto metric_helper = crate::metrics::helper_c(
         test.endpoint_type,
         ADDRESS,
         test.metric_port
      );

      // Submit metrics
      // 
      for(auto& reading : readings) {
         std::this_thread::sleep_for(100ms);

         if (metric_helper.submit(reading) != crate::metrics::helper_c::result::SUCCESS) {
            FAIL("Failed to write reading");
         }
      }

      std::this_thread::sleep_for(4s);

      // Verify received data
      //
      CHECK_EQUAL_TEXT(readings.size(), received_readings.size(), "Did not recieve all metrics sent to the server");

      for(size_t i = 0; i < readings.size(); i++) {

         std::string sent;
         readings[i].encode_to(sent);

         std::string received;
         received_readings[i].encode_to(received);

         CHECK_EQUAL_TEXT(sent, received, "Reading sent does not match reading received");
      }

      // Remove self as a metric receiver
      //
      result = helper.deregister_as_metric_stream_receiver(
         ADDRESS ,
         RECEIVE_PORT
      );

      if (result != crate::metrics::streams::helper_c::result::SUCCESS) {
         FAIL("Failed to deregister object as a metric stream receiver");
      }
   }

   // ---------- Stop all the services ----------
   metric_stream_server->stop();
   CHECK_TRUE(app->stop());
   CHECK_TRUE(data_submission->stop());
   CHECK_TRUE(metric_streamer->stop());
}