#ifndef MONOLITH_INTERFACE_SMS_BACKEND_HPP
#define MONOLITH_INTERFACE_SMS_BACKEND_HPP

#include <string>

namespace monolith {

//! \brief An interface representing an sms backend
class sms_backend_if {
public:
   //! \brief Setup the backend 
   //! \returns true iff the backend is setup and
   //!          ready to be used
   virtual bool setup() = 0;

   //! \brief Teardown the backend 
   //! \returns true iff the backend has been 
   //!          disabled and made not usable
   virtual bool teardown() = 0;

   //! \brief Send an SMS message
   //! \param message The message to send
   //! \returns true iff the backend has honestly attempted
   //!          to send the message
   virtual bool send_message(std::string message) = 0;
};

} // namespace monolith

#endif