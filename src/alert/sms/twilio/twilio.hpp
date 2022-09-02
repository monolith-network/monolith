#ifndef MONOLITH_SMS_TWILIO_HPP
#define MONOLITH_SMS_TWILIO_HPP

#include "interfaces/sms_backend_if.hpp"

namespace monolith {
namespace sms {

class twilio_c : public monolith::sms_backend_if {
public:
   //! \brief Configuration for an sms alert provider
   struct configuration_c {
      std::string account_id; // The account id for login
      std::string auth_token; // The token needed to login
      std::string from;       // Number origin
      std::string to;         // Destination number
   };

   twilio_c() = delete;
   twilio_c(configuration_c config);

   // From sms_backend_if
   virtual bool setup() override final;
   virtual bool teardown() override final;
   virtual bool send_message(std::string message) override final;

private:
   configuration_c _config;
   
   // --- From twilio example
   // Portably ignore curl response
   static size_t _null_write(char *, size_t, size_t, void *);
   // Write curl response to a stringstream
   static size_t _stream_write(char *, size_t, size_t, void *);

};

} // namespace sms
} // namespace monolith

#endif