#include "twilio.hpp"
#include <locale>
#include <codecvt>
#include <string>
#include <crate/externals/aixlog/logger.hpp>
#include <curl/curl.h>

namespace monolith {
namespace sms {

namespace {
// --- From twilio example
// Given a UTF-8 encoded string return a new UCS-2 string.
inline std::u16string
utf8_to_ucs2(std::string const& input) {
   std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> convert;
   try {
      return convert.from_bytes(input);
   } catch(const std::range_error& e) {
      throw std::range_error(
         "Failed UCS-2 conversion of message body.  Check all "
         "characters are valid GSM-7, GSM 8-bit text, or UCS-2 "
         "characters."
      );
   }
}

// --- From twilio example
// Given a UCS-2 string return a new UTF-8 encoded string.
inline std::string
ucs2_to_utf8(std::u16string const& input) {
   std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> convert;
   return convert.to_bytes(input);
}

} // namespace anonymous

// --- From twilio example
// Portably ignore curl response
size_t twilio_c::_null_write(char *ptr, 
                           size_t size, 
                           size_t nmemb, 
                           void *userdata) {
   return size*nmemb;
}

// --- From twilio example
// Write curl response to a stringstream
size_t twilio_c::_stream_write( char *ptr,
                              size_t size,
                              size_t nmemb,
                              void *userdata) {
   size_t response_size = size * nmemb;
   std::stringstream *ss = (std::stringstream*)userdata;
   ss->write(ptr, response_size);
   return response_size;
}

twilio_c::twilio_c(twilio_c::configuration_c config) : _config(config) {}

bool twilio_c::setup() {
   if (_is_setup.load()) {
      return true;
   }

   if (_config.account_id.empty()) {
      LOG(ERROR) << TAG("twilio_c::send_message") << "Twilio account id not set\n";
      return false;
   }

   if (_config.auth_token.empty()) {
      LOG(ERROR) << TAG("twilio_c::send_message") << "Twilio auth token set\n";
      return false;
   }

   if (_config.from.empty()) {
      LOG(ERROR) << TAG("twilio_c::send_message") << "Twilio \"from\" not set\n";
      return false;
   }

   if (_config.to.empty()) {
      LOG(ERROR) << TAG("twilio_c::send_message") << "Twilio \"to\" not set\n";
      return false;
   }

   _is_setup.store(true);
   return true;
}

bool twilio_c::teardown() {
   _is_setup.store(false);
   return true;
}

// --- "Mostly" From twilio example
// Some adaptations were made to use local config file
// and the logging system
bool twilio_c::send_message(std::string message) {

   if (!_is_setup.load()) {
      LOG(ERROR) << TAG("twilio_c::send_message") << "Backend not yet setup\n";
      return false;
   }

   std::stringstream response_stream;
   std::u16string converted_message;

   // Assume UTF-8 input, convert to UCS-2 to check size
   // See: https://www.twilio.com/docs/api/rest/sending-messages for
   // information on Twilio body size limits.
   try {
      converted_message = utf8_to_ucs2(message);
   } catch(const std::range_error& e) {
      LOG(ERROR) << TAG("twilio_c::send_message") <<  e.what() << "\n";
      return false;
   }

   if (converted_message.size() > 1600) {
      response_stream 
         << "Message body must have 1600 or fewer"
         << " characters. Cannot send message with "
         << converted_message.size() << " characters.";

      LOG(ERROR) << TAG(" twilio_c::send_message") << response_stream.str();
      return false;
   }

   CURL *curl;
   curl_global_init(CURL_GLOBAL_ALL);
   curl = curl_easy_init();

   // Percent encode special characters
   char *message_escaped = curl_easy_escape(
      curl, 
      message.c_str(), 
      0
   );

   std::stringstream url;
   std::string url_string;
   url << "https://api.twilio.com/2010-04-01/Accounts/" 
         << _config.account_id
         << "/Messages";
   url_string = url.str();

   std::stringstream parameters;
   std::string parameter_string;
   parameters << "To=" << _config.to << "&From=" << _config.from 
            << "&Body=" << message_escaped;

   parameter_string = parameters.str();

   curl_easy_setopt(curl, CURLOPT_POST, 1);
   curl_easy_setopt(curl, CURLOPT_URL, url_string.c_str());
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, parameter_string.c_str());
   curl_easy_setopt(curl, CURLOPT_USERNAME, _config.account_id.c_str());
   curl_easy_setopt(curl, CURLOPT_PASSWORD, _config.auth_token.c_str());
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _stream_write);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_stream);

   CURLcode res = curl_easy_perform(curl);
   curl_free(message_escaped);
   curl_easy_cleanup(curl);
   long http_code = 0;
   curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

   // Check for curl errors and Twilio failure status codes.
   if (res != CURLE_OK) {
      LOG(ERROR) << TAG("twilio_c::send_message") <<curl_easy_strerror(res) << "\n";
      return false;
   } else if (http_code != 200 && http_code != 201) {
      LOG(ERROR) << TAG("twilio_c::send_message") << response_stream.str() << "\n";
      return false;
   } else {
      LOG(DEBUG) << TAG("twilio_c::send_message") << "SENT\n";
      return true;
   }
}

} // namespace sms
} // namespace monolith