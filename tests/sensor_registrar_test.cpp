
#include <crate/common/common.hpp>
#include <crate/registrar/helper.hpp>
#include <crate/registrar/node_v1.hpp>
#include "services/app.hpp"
#include "services/metric_streamer.hpp"
#include "services/data_submission.hpp"
#include <filesystem>

// This has to be included last as there is a known issue
// described here: https://github.com/cpputest/cpputest/issues/982
//
#include "CppUTest/TestHarness.h"

namespace {
   
   static constexpr char ADDRESS[] = "0.0.0.0";
   static constexpr uint32_t HTTP_PORT = 8080;
   static constexpr uint32_t DATA_PORT = 4096;
   static constexpr char REGISTRAR_DB[] = "test_registrar.db";
   static constexpr char LOGS[] = "test";

   monolith::db::kv_c* registrar_db {nullptr};
   monolith::services::metric_streamer_c* metric_streamer {nullptr};
   monolith::services::data_submission_c* data_submission {nullptr};
   monolith::db::metric_db_c* metric_db {nullptr};
   monolith::services::app_c* app {nullptr};
}
TEST_GROUP(sensor_registrar_test)
{
   void setup() {
      crate::common::setup_logger(LOGS, AixLog::Severity::trace);
      registrar_db = new monolith::db::kv_c(REGISTRAR_DB);
      metric_db = new monolith::db::metric_db_c();
      metric_streamer = new monolith::services::metric_streamer_c();
      data_submission = new monolith::services::data_submission_c(
         monolith::networking::ipv4_host_port_s{
            ADDRESS, DATA_PORT
         },
         registrar_db,
         metric_streamer,
         metric_db
      );
      app = new monolith::services::app_c(
         monolith::networking::ipv4_host_port_s{
            ADDRESS, HTTP_PORT
         },
         registrar_db,
         metric_streamer,
         data_submission
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

   // Submit nodes
   //

   // Ensure they all exist
   //

   // Delete a couple
   //

   // Make sure they don't exist
   //

   FAIL("TEST NOT WRITTEN");

   // ---------- Stop all the services ----------
   CHECK_TRUE(app->stop());
   CHECK_TRUE(data_submission->stop());
   CHECK_TRUE(metric_streamer->stop());
}