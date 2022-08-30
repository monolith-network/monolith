#include <libutil/random/entry.hpp>
#include <crate/common/common.hpp>
#include <crate/registrar/helper.hpp>
#include <crate/registrar/node_v1.hpp>
#include <crate/registrar/controller_v1.hpp>
#include "services/app.hpp"
#include "services/metric_streamer.hpp"
#include "services/data_submission.hpp"
#include "heartbeats.hpp"
#include <filesystem>
#include <queue>
#include <vector>
#include <iostream>

// This has to be included last as there is a known issue
// described here: https://github.com/cpputest/cpputest/issues/982
//
#include "CppUTest/TestHarness.h"

namespace {
   
   static constexpr char ADDRESS[] = "0.0.0.0";
   static constexpr uint32_t HTTP_PORT = 8080;
   static constexpr uint32_t DATA_PORT = 4096;
   static constexpr uint32_t RECEIVE_PORT = 5042;
   static constexpr char REGISTRAR_DB[] = "test_registrar.db";
   static constexpr char LOGS[] = "test_registrar";
   static constexpr size_t NUM_NODES = 10;
   static constexpr size_t NUM_SENSORS_PER_NODE = 2;
   static constexpr size_t NUM_NODES_DELETE = NUM_NODES / 2;
   static constexpr size_t NUM_CONTROLLERS = 10;
   static constexpr size_t NUM_ACTIONS_PER_NODE = 2;
   static constexpr size_t NUM_CONTROLLERS_DELETE = NUM_NODES / 2;

   monolith::db::kv_c* registrar_db {nullptr};
   monolith::services::metric_streamer_c* metric_streamer {nullptr};
   monolith::services::data_submission_c* data_submission {nullptr};
   monolith::db::metric_db_c* metric_db {nullptr};
   monolith::services::app_c* app {nullptr};
   monolith::heartbeats_c heartbeat_manager;
   std::vector<crate::registrar::node_v1_c> nodes;
   std::vector<crate::registrar::controller_v1_c> controllers;
}
TEST_GROUP(sensor_registrar_test)
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
         &heartbeat_manager,
         nullptr // We don't need a portal for testing
      );
   }

   void teardown() {
      delete registrar_db;
      delete metric_db;
      delete metric_streamer;
      delete data_submission;
      delete app;

      std::filesystem::remove_all(std::string(LOGS) + std::string(".log"));
      std::filesystem::remove_all(REGISTRAR_DB);
   }
};

TEST(sensor_registrar_test, submit_fetch_probe_delete)
{
   using namespace std::chrono_literals;

   // ---------- Start all the services ----------
   CHECK_TRUE(metric_streamer->start());
   CHECK_TRUE(data_submission->start());
   CHECK_TRUE(app->start());

   // Create nodes/sensors
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

   std::this_thread::sleep_for(20ms);

   // Ensure they all exist
   //
   for (auto& node : nodes) {

      auto [id, desc, sensor_list ] = node.get_data();

      crate::registrar::node_v1_c remote_node;
      if (registrar_helper.retrieve(id, remote_node) != crate::registrar::helper_c::result::SUCCESS) {
         FAIL("Failed to retrieve node from registrar");
      }

      auto [r_id, r_desc, r_sensor_list ] = remote_node.get_data();

      CHECK_EQUAL_TEXT(id, r_id, "Node IDs not matched");
      CHECK_EQUAL_TEXT(desc, r_desc, "Node DESC not matched");
      CHECK_EQUAL_TEXT(sensor_list.size(), r_sensor_list.size(), "Sensor list retrieved does not match length of list sent");

      for(auto i = 0; i < sensor_list.size(); i++) {
         CHECK_EQUAL_TEXT(sensor_list[i].id, r_sensor_list[i].id, "Sensor ID did not match that of sensor ID sent");
         CHECK_EQUAL_TEXT(sensor_list[i].description, r_sensor_list[i].description, "Sensor DESC did not match that of sensor DESC sent");
         CHECK_EQUAL_TEXT(sensor_list[i].type, r_sensor_list[i].type, "Sensor TYPE did not match that of sensor TYPE sent");
      }
   }

   // Delete a couple
   //
   libutil::random::random_entry_c<crate::registrar::node_v1_c> random_entry(nodes);
   std::vector<crate::registrar::node_v1_c> deleted_nodes;
   for(size_t i = 0; i < NUM_NODES_DELETE; i++) {

      auto delete_node = random_entry.get_value();

      auto [id, desc, sensor_list] = delete_node.get_data();

      if (registrar_helper.remove(id) != crate::registrar::helper_c::result::SUCCESS) {
         FAIL("Failed to run delete for node");
      }
      deleted_nodes.push_back(delete_node);
   }

   // Make sure they don't exist
   //
   for(auto& node : deleted_nodes) {

      auto [id, desc, sensor_list] = node.get_data();

      crate::registrar::node_v1_c remote_node;
      if (registrar_helper.retrieve(id, remote_node) != crate::registrar::helper_c::result::NOT_FOUND) {
         FAIL("Retrieved deleted node");
      }
   }

   // ---------------------- Now do the same with controllers / actions ----------------------
   
   // Create nodes/sensors
   //
   for (size_t i = 0; i < NUM_CONTROLLERS; i++) {
      
      std::string controller_id = std::to_string(i + (NUM_NODES * 2));
      crate::registrar::controller_v1_c controller;
      controller.set_id(controller_id);
      
      for (size_t j = 0; j < NUM_ACTIONS_PER_NODE; j++) {
         crate::registrar::controller_v1_c::action action;
         action.id = controller_id + ":" + std::to_string(j);
         action.description = "[desc]";

         controller.add_action(action);
      }
      controllers.push_back(controller);
   }

   // Submit nodes
   //
   for (auto& controller : controllers) {
      auto res = registrar_helper.submit(controller);
      if (res != crate::registrar::helper_c::result::SUCCESS) {
         std::cout << "res: " << (int)res << std::endl;
         FAIL("Failed to submit controller to registrar ");
      }
   }

   std::this_thread::sleep_for(20ms);

   // Ensure they all exist
   //
   for (auto& controller : controllers) {

      auto [id, desc, action_list ] = controller.get_data();

      crate::registrar::controller_v1_c remote_controller;
      if (registrar_helper.retrieve(id, remote_controller) != crate::registrar::helper_c::result::SUCCESS) {
         FAIL("Failed to retrieve controller from registrar");
      }

      auto [r_id, r_desc, r_action_list ] = remote_controller.get_data();

      CHECK_EQUAL_TEXT(id, r_id, "Controller IDs not matched");
      CHECK_EQUAL_TEXT(desc, r_desc, "Controller DESC not matched");
      CHECK_EQUAL_TEXT(action_list.size(), r_action_list.size(), "Action list retrieved does not match length of list sent");

      for(auto i = 0; i < action_list.size(); i++) {
         CHECK_EQUAL_TEXT(action_list[i].id, r_action_list[i].id, "ACtion ID did not match that of action ID sent");
         CHECK_EQUAL_TEXT(action_list[i].description, r_action_list[i].description, "Action DESC did not match that of action DESC sent");
      }
   }

   // Delete a couple
   //
   libutil::random::random_entry_c<crate::registrar::controller_v1_c> random_controller(controllers);
   std::vector<crate::registrar::controller_v1_c> deleted_controllers;
   for(size_t i = 0; i < NUM_CONTROLLERS_DELETE; i++) {

      auto deleted_controller = random_controller.get_value();

      auto [id, desc, action_list] = deleted_controller.get_data();

      if (registrar_helper.remove(id) != crate::registrar::helper_c::result::SUCCESS) {
         FAIL("Failed to run delete for controller");
      }
      deleted_controllers.push_back(deleted_controller);
   }

   // Make sure they don't exist
   //
   for(auto& controller : deleted_controllers) {

      auto [id, desc, action_list] = controller.get_data();

      crate::registrar::controller_v1_c remote_controller;
      if (registrar_helper.retrieve(id, remote_controller) != crate::registrar::helper_c::result::NOT_FOUND) {
         FAIL("Retrieved deleted controller");
      }
   }

   // ---------- Stop all the services ----------
   CHECK_TRUE(app->stop());
   CHECK_TRUE(data_submission->stop());
   CHECK_TRUE(metric_streamer->stop());
}