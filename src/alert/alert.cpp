#include "alert.hpp"

#include <crate/externals/aixlog/logger.hpp>

namespace monolith {
namespace alert {

void alert_manager_c::trigger(const int id, const std::string message) {

   LOG(INFO) << TAG("alert_manager_c::trigger") << "Alert ID: " << id << " => " << message << "\n";
}

} // namespace alert
} // namespace monolith
