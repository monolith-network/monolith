#ifndef MONOLITH_SERVICES_RULE_EXECUTOR_HPP
#define MONOLITH_SERVICES_RULE_EXECUTOR_HPP

#include "alert/alert.hpp"
#include "interfaces/service_if.hpp"
#include "services/action_dispatch.hpp"
#include <compare>
#include <crate/metrics/reading_v1.hpp>
#include <queue>

namespace monolith {
namespace services {

//! \brief Rule execution object
class rule_executor_c : public service_if {
public:
  rule_executor_c() = delete;

  //! \brief Construct the executor
  //! \param file Lua file to load in
  //! \param alert_config The configuration for sending alerts
  //! \param dispatcher The action dispatching object
  rule_executor_c(
      const std::string &file,
      monolith::alert::alert_manager_c::configuration_c alert_config,
      monolith::services::action_dispatch_c* dispatcher);
  virtual ~rule_executor_c() override final;

  //! \brief Open the given Lua file
  //! \returns true iff file exists, was loaded, and contains the required
  //!          function(s) to interat with
  bool open();

  //! \brief Submit a metric to the rule executor
  //! \param data The metric to submit
  //! \post The metric will be piped into the lua function(s) 
  //!       meant to handle metrics 
  void submit_metric(crate::metrics::sensor_reading_v1_c &data);

  // From service_if
  virtual bool start() override final;
  virtual bool stop() override final;

private:
  static constexpr uint8_t MAX_BURST = 100;

  std::string _file;
  std::queue<crate::metrics::sensor_reading_v1_c> _reading_queue;
  std::mutex _reading_queue_mutex;

  void run();
  void burst();
};

} // namespace services
} // namespace monolith

#endif