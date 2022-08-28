
#include <crate/common/common.hpp>


// This has to be included last as there is a known issue
// described here: https://github.com/cpputest/cpputest/issues/982
//
#include "CppUTest/TestHarness.h"

TEST_GROUP(server_test)
{
   void setup() {
      crate::common::setup_logger("test", AixLog::Severity::trace);
   }

   void teardown() {
   }
};

TEST(server_test, submit_fetch_probe_delete)
{

}