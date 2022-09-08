#ifndef MONOLITH_RELOADABLE_INTERFACE_HPP
#define MONOLITH_RELOADABLE_INTERFACE_HPP

#include <string>

namespace monolith {

//! \brief An interface representing soemthing that can be reloaded
class reloadable_if {
public:
   virtual bool reload() = 0;   
};

} // namespace monolith

#endif