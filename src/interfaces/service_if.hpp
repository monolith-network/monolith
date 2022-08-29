#ifndef MONOLITH_INTERFACE_SERVICE_HPP
#define MONOLITH_INTERFACE_SERVICE_HPP

#include <atomic>
#include <thread>

namespace monolith
{

//! \brief An impure interface for all services that
//!        can start and stop 
class service_if {
public:
   virtual ~service_if() {}

   //! \brief Start a service
   //! \returns true iff the service is running
   virtual bool start() = 0;

   //! \brief Stop a service
   //! \returns true iff the service is stopped
   virtual bool stop() = 0;

   //! \brief Check if a service is running
   //! \returns true iff the service is marked as running
   bool is_running() {
      return p_running.load();
   }
protected:
   std::atomic<bool> p_running {false};
   std::thread p_thread;
};

}

#endif