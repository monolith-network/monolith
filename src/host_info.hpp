#ifndef MONOLITH_HOST_INFO_HPP
#define MONOLITH_HOST_INFO_HPP

#include <string>

namespace monolith {
namespace hardware {

//! \brief Retrieve a string of hardware information
//!        of the host machine
extern std::string get_info();

} // namespace hardware
} // namespace monolith

#endif