#ifndef MONOLITH_HEARTBEATS_HPP
#define MONOLITH_HEARTBEATS_HPP

#include <unordered_map>
#include <mutex>
#include <optional>
#include <crate/metrics/heartbeat_v1.hpp>

namespace monolith {

//! \brief Heartbeat management object
class heartbeats_c {
public:

   //! \brief Submit the id of something that sent in a heartbeat
   //! \param id The id of the item that sent in a heartbeat
   void submit(std::string id);

   //! \brief Retrieve the seconds since last contact of a given id
   //! \param id The id to check
   //! \returns optional number of ms since contact
   //!          if the id doesn't exist, or its data is otherwise,
   //!          invalid, nothing will be returned
   std::optional<uint64_t> sec_since_contact(std::string id); 

private:
   std::unordered_map<std::string, int64_t> _heartbeats;
   std::mutex _heartbeat_mutex;
};

} // namespace monolith

#endif