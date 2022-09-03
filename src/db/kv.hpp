#ifndef MONOLITH_DB_KV_HPP
#define MONOLITH_DB_KV_HPP

#include <optional>
#include <string>

#include <rocksdb/db.h>

namespace monolith {
namespace db {

//! \brief A key/value database
class kv_c {
public:
  kv_c() = delete;

  //! \brief Open/Create a database
  //! \param db_location The database location
  kv_c(const std::string &db_location);

  //! \brief Close db and destroy object
  ~kv_c();

  //! \brief Check if an item exists
  //! \param key The key to look for
  //! \returns true iff the item exists
  bool exists(const std::string &key);

  //! \brief Store an item
  //! \param key The key
  //! \param value The value to store
  //! \returns true iff the item was stored
  bool store(const std::string &key, const std::string &value);

  //! \brief Retrieve an item
  //! \param key The key to get the value for
  //! \returns Optional containing value if the item exists
  std::optional<std::string> load(const std::string &key);

  //! \brief Remove a key
  //! \returns true iff the the delete was executed
  //! \note Attempting to delete a non-existing key can return true
  bool remove(const std::string &key);

private:
  bool ensure_open();
  std::string _db_location;
  rocksdb::DB *_db{nullptr};
};

} // namespace db
} // namespace monolith

#endif