#ifndef MONOLITH_VERSION_HPP
#define MONOLITH_VERSION_HPP

#include <crate/app/version_v1.hpp>
#include <string>

namespace monolith {

//! \brief Retrieve the version information
extern crate::app::version_v1_c get_version_info();

} // namespace monolith

#endif