#include "kv.hpp"
#include <crate/externals/aixlog/logger.hpp>

namespace monolith {
namespace db {

kv_c::kv_c(const std::string& db_location) 
   : _db_location(db_location) {

}

bool kv_c::exists(const std::string& key) {
   if (!ensure_open()) { return false; }
   
   std::string _unused;
   rocksdb::Status status = _db->Get(rocksdb::ReadOptions(), key, &_unused);
   
   if (status.IsNotFound()) {
      return false;
   }

   if (status.ok()) {
      return true;
   }

   return false;
}

bool kv_c::store(const std::string& key, const std::string& value) {
   if (!ensure_open()) { return false; }

   rocksdb::Status status = _db->Put(rocksdb::WriteOptions(), key, value);
   return status.ok();
}

std::optional<std::string> kv_c::load(const std::string& key) {
   if (!ensure_open()) { return {}; }

   std::string item_value;
   rocksdb::Status status = _db->Get(rocksdb::ReadOptions(), key, &item_value);

   if (status.IsNotFound()) {
      return {};
   }

   if (status.ok()) {
      return {item_value};
   }
   return {};
}

bool kv_c::remove(const std::string& key) {
   if (!ensure_open()) { return false; }

   rocksdb::Status status = _db->Delete(rocksdb::WriteOptions(), key);
   return status.ok();
}

bool kv_c::ensure_open() {

   if (_db) {
      return true;
   }

   LOG(INFO) << TAG("kv_c::ensure_open") << "Attempting to open : " << _db_location << "\n";

   rocksdb::Options options;
   options.create_if_missing = true;
   rocksdb::Status status = rocksdb::DB::Open(options, _db_location, &_db);

   if (!status.ok()) {
      LOG(ERROR) << TAG("kv_c::ensure_open") 
                  << "Unable to open database file : " 
                  << _db_location 
                  << "\n";
      return false;
   }

   LOG(INFO) << TAG("kv_c::ensure_open") << "Opened : " << _db_location << "\n";
   return true;
}
   
} // namespace db
} // namespace monolith
