#ifndef MONOLITH_PORTAL_HPP
#define MONOLITH_PORTAL_HPP

#include <httplib.h>
#include <crate/metrics/streams/stream_receiver_if.hpp>
#include "services/metric_db.hpp"
#include "db/kv.hpp"

namespace monolith {
namespace portal {

//! \brief The user portal
class portal_c : public crate::metrics::streams::stream_receiver_if {

public:
   portal_c(monolith::db::kv_c* registrar_db,
            monolith::services::metric_db_c* metric_db);

   //! \brief Setup the portal and endpoints
   //! \param http_server The server item to use
   //! \post Portal functionality will be setup
   //! \returns true iff the portal was able to be setup
   bool setup_portal(httplib::Server* http_server);

   // From crate::metrics::streams::stream_receiver_if
   virtual void receive_data(crate::metrics::streams::stream_data_v1_c data) override final;

private:
   httplib::Server* _http_server {nullptr};
   monolith::db::kv_c* _registrar_db {nullptr};
   monolith::services::metric_db_c* _metric_db {nullptr};
   
   void portal_root(const httplib::Request& req, httplib:: Response& res);

};

} // namespace portal
} // namespace monolith

#endif