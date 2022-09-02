#ifndef MONOLITH_ALERT_HPP
#define MONOLITH_ALERT_HPP

#include <string>

namespace monolith {
namespace alert {

/*
   TODO: 

      Implement this

*/

class alert_manager_c {
public:

   void trigger(const int id, const std::string message);
};

} // namespace alert
} // namespace monolith

#endif