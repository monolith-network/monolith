#ifndef MONOLITH_NETWORKING_TYPES_HPP
#define MONOLITH_NETWORKING_TYPES_HPP

#include <cstdint>
#include <string>

namespace monolith {
namespace networking {

//! \brief A structure that combines address and port
//!        and indicates through its type that it is
//!        meant to be ipv4
struct ipv4_host_port_s {
   std::string address;
   uint32_t port;
};

//! \brief A structure that combines address and port
//!        and indicates through its type that it is
//!        meant to be ipv6
struct ipv6_host_port_s {
   std::string address;
   uint32_t port;
};

} // namespace networking
} // namespace monolith

#endif