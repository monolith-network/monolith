#include "version.hpp"

namespace monolith {

#ifndef COMPILED_GIT_HASH
#define COMPILED_GIT_HASH "unknown"
#endif

namespace {
constexpr char APPLICATION_NAME[] = "Monolith";
constexpr char VERSION_MAJOR[] = "0";
constexpr char VERSION_MINOR[] = "0";
constexpr char VERSION_PATCH[] = "0";
} // namespace

crate::app::version_v1_c get_version_info() {
   return crate::app::version_v1_c(
       APPLICATION_NAME, std::string(COMPILED_GIT_HASH),
       {VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH});
}

} // namespace monolith
