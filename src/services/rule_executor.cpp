#include "rule_executor.hpp"
#include <crate/externals/aixlog/logger.hpp>
#include <filesystem>

extern "C" {
#include <lua5.3/lauxlib.h>
#include <lua5.3/lua.h>
#include <lua5.3/lualib.h>
} // extern "C"

using namespace std::chrono_literals;

namespace {
std::atomic<bool> setup{false};
std::atomic<bool> file_open{false};
std::atomic<uint64_t> instance_counter{0};
lua_State *L{nullptr};
monolith::alert::alert_manager_c *alert_manager{nullptr};

constexpr char LUA_FUNC_ACCEPT_READING_V1[] = "accept_reading_v1_from_monolith";

// Check return from LUA to ensure that it is LUA_OK
bool check_lua(lua_State *L, int r) {
  if (r != LUA_OK) {
    std::string err = lua_tostring(L, -1);
    LOG(ERROR) << TAG("check_lua") << "Error: " << err << "\n";
    return false;
  }
  return true;
}

bool check_file_for_reading_v1(std::string file) {

  lua_State *_L = luaL_newstate();
  luaL_openlibs(_L);

  // Load the file and check to see it it is okay
  if (!check_lua(_L, luaL_dofile(_L, file.c_str()))) {
    return false;
  }

  //
  //  Check to make sure required function exist within the script
  //
  lua_getglobal(_L, LUA_FUNC_ACCEPT_READING_V1);

  if (!lua_isfunction(_L, -1)) {
    return false;
  }

  lua_close(_L);
  return true;
}

// Send an alert to the alert system
int lua_monolith_trigger_alert(lua_State *L) {

  if (!lua_isnumber(L, 1)) {
    LOG(ERROR) << TAG("lua_monolith_trigger_alert")
               << "Error: Expected first parameter to be a number \n";
    return -1;
  }

  if (!lua_isstring(L, 2)) {
    LOG(ERROR) << TAG("lua_monolith_trigger_alert")
               << "Error: Expected firssecondt parameter to be a string \n";
    return -2;
  }

  int alert_id = lua_tonumber(L, 1);
  std::string message = lua_tostring(L, 2);
  alert_manager->trigger(alert_id, message);
  return 0;
}

/*
   Setup file statics
*/
void setup_statics(
    monolith::alert::alert_manager_c::configuration_c alert_config) {
  // Check if Lua is setup
  if (setup.load()) {
    return;
  }

  L = luaL_newstate();
  luaL_openlibs(L);
  lua_register(L, "monolith_trigger_alert", lua_monolith_trigger_alert);

  // TOOD:
  // When we start piping confige to the manager do so via the rule_executor
  // call to this method
  alert_manager = new monolith::alert::alert_manager_c(alert_config);
  setup.store(true);
}

/*
   Cleanup file statics
*/
void cleanup_statics() {
  if (!setup.load()) {
    return;
  }

  lua_close(L);
  L = nullptr;
  delete alert_manager;
  alert_manager = nullptr;
}
} // namespace

namespace monolith {
namespace services {

rule_executor_c::rule_executor_c(
    const std::string &file,
    monolith::alert::alert_manager_c::configuration_c alert_config)
    : _file(file) {

  instance_counter.fetch_add(1);
  setup_statics(alert_config);
}

rule_executor_c::~rule_executor_c() {

  // Check to see if we are last instance of rule_executor
  // if we are, we need to clearn up on the way out
  if (instance_counter.load() == 0) {
    cleanup_statics();
  } else {
    instance_counter.fetch_sub(1);
  }
}

bool rule_executor_c::open() {

  if (file_open.load()) {
    LOG(WARNING) << TAG("rule_executor_c::open") << "Lua script already open\n";
    return false;
  }

  if (!std::filesystem::is_regular_file(_file)) {
    LOG(FATAL) << TAG("rule_executor_c::open") << "Given item: " << _file
               << " is not a file\n";
    return false;
  }

  if (!check_file_for_reading_v1(_file)) {
    LOG(FATAL) << TAG("rule_executor_c::open") << "Given lua script: " << _file
               << " does not contain function to receive reading_v1 data ("
               << LUA_FUNC_ACCEPT_READING_V1 << ")\n";
    return false;
  }

  if (!check_lua(L, luaL_dofile(L, _file.c_str()))) {
    LOG(FATAL) << TAG("rule_executor_c::open")
               << "Failed to load lua script: " << _file << "\n";
    return false;
  }

  file_open.store(true);

  return true;
}

void rule_executor_c::submit_metric(crate::metrics::sensor_reading_v1_c &data) {

  LOG(TRACE) << TAG("rule_executor_c::submit_metric") << "Got metric data\n";
  {
    const std::lock_guard<std::mutex> lock(_reading_queue_mutex);
    _reading_queue.push(data);
  }
}

bool rule_executor_c::start() {

  if (p_running.load()) {
    LOG(WARNING) << TAG("rule_executor_c::start")
                 << "Executor already started\n";
    return true;
  }

  if (!setup.load()) {
    LOG(WARNING) << TAG("rule_executor_c::start")
                 << "Setup has not been done\n";
    return false;
  }

  if (!file_open.load()) {
    LOG(WARNING) << TAG("rule_executor_c::start")
                 << "Lua file has not yet been opened\n";
    return false;
  }

  p_running.store(true);
  p_thread = std::thread(&rule_executor_c::run, this);

  LOG(INFO) << TAG("rule_executor_c::start") << "Executor started\n";

  return true;
}

bool rule_executor_c::stop() {

  if (!p_running.load()) {
    return true;
  }

  p_running.store(false);

  if (p_thread.joinable()) {
    p_thread.join();
  }

  return true;
}

void rule_executor_c::run() {

  while (p_running.load()) {
    std::this_thread::sleep_for(500ms);
    burst();
  }
}

void rule_executor_c::burst() {
  {
    const std::lock_guard<std::mutex> lock(_reading_queue_mutex);
    if (_reading_queue.empty()) {
      return;
    }
  }

  uint32_t num_items{0};
  std::vector<crate::metrics::sensor_reading_v1_c> _selected_readings;
  _selected_readings.reserve(MAX_BURST);

  // Select a potential subset of readings to submit
  {
    const std::lock_guard<std::mutex> lock(_reading_queue_mutex);
    while (!_reading_queue.empty() && num_items++ < MAX_BURST) {
      _selected_readings.push_back(_reading_queue.front());
      _reading_queue.pop();
    }
  }

  // Go over selected readings
  for (auto &reading : _selected_readings) {

    // Retrieve the lua function we are going to call
    lua_getglobal(L, LUA_FUNC_ACCEPT_READING_V1);

    // Double check it exists as a function
    if (!lua_isfunction(L, -1)) {
      LOG(FATAL) << TAG("rule_executor_c::burst") << "Expected "
                 << LUA_FUNC_ACCEPT_READING_V1
                 << " to exist in given lua script as a function. Dropping "
                 << _selected_readings.size() << " readings\n";
      return;
    }

    // Peel the reading apart
    auto [timestamp, node_id, sensor_id, value] = reading.get_data();

    // Load data into lua
    lua_pushnumber(L, timestamp);
    lua_pushstring(L, node_id.c_str());
    lua_pushstring(L, sensor_id.c_str());
    lua_pushnumber(L, value);

    // Call the function that we got for reading v1s
    lua_call(L, 4, 1);
  }
}

} // namespace services
} // namespace monolith