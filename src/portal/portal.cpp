#include "portal.hpp"
#include <crate/externals/aixlog/logger.hpp>

namespace monolith {
namespace portal {

portal_c::portal_c(monolith::db::kv_c *registrar_db,
                   monolith::services::metric_db_c *metric_db)
    : _registrar_db(registrar_db), _metric_db(metric_db) {}

bool portal_c::setup_portal(httplib::Server *http_server) {
  LOG(TRACE) << TAG("portal_c::setup_portal") << "Setup portal\n";

  _http_server = http_server;

  // Endpoint to add metric stream destination
  _http_server->Get("/portal",
                    std::bind(&portal_c::portal_root, this,
                              std::placeholders::_1, std::placeholders::_2));

  return true;
}

void portal_c::receive_data(crate::metrics::streams::stream_data_v1_c data) {
  LOG(TRACE) << TAG("portal_c::receive_data") << "Got data\n";
}

void portal_c::portal_root(const httplib::Request &req,
                           httplib::Response &res) {
  std::string body = R"(
<!DOCTYPE html>
<html lang="en">
<head>
<title>Portal</title>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="static/portal.css">
</head>
<body>

<header>
  <h2>Monolith Portal</h2>
</header>

<section>
  <nav>
    <ul>
      <li><a href="#">Add Nodes</a></li>
      <li><a href="#">Add Controllers</a></li>
      <li><a href="#">Rule Manager</a></li>
    </ul>
  </nav>
  
  <article>
    <h1>Todo: Node View</h1>
    <p>In this section I would like to display all nodes and sensors that are registered with the system as well as their last contact / etc</p>
    <h1>Todo: Controller View</h1>
    <p>In this section I would like to display all controllers and actions that are registered with the system as well as their last contact / etc</p>
  </article>
</section>

<footer>
  <p>Footer</p>
</footer>

</body>
</html>
   )";

  res.set_content(body, "text/html");
}

} // namespace portal
} // namespace monolith