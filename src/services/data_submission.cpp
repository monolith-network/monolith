#include "data_submission.hpp"

#include <chrono>
#include <crate/externals/aixlog/logger.hpp>
#include <crate/registrar/entry/node_v1.hpp>

using namespace std::chrono_literals;

namespace monolith {
namespace services {

data_submission_c::data_submission_c(const monolith::networking::ipv4_host_port_s& host_port,
                                     monolith::db::kv_c* registrar,
                                     monolith::services::metric_streamer_c* metric_streamer,
                                     monolith::db::metric_db_c* metric_db) 
                                          : _host_port(host_port),
                                            _registrar(registrar),
                                            _stream_server(metric_streamer),
                                            _database(metric_db) {}

bool data_submission_c::start() {

   if (p_running.load()) {
      LOG(WARNING) << TAG("data_submission_c::start") 
                     << "Server already started\n";
      return true;
   }

   if (!_message_server) {
      _message_server = new crate::networking::message_server(
         _host_port.address, 
         _host_port.port, 
         this
      );
   }

   if (!_message_server->start()) {
      LOG(WARNING) << TAG("data_submission_c::start") 
                     << "Unable to start message server\n";
      return false;
   }

   p_running.store(true);
   p_thread = std::thread(&data_submission_c::run, this);

   LOG(INFO) << TAG("data_submission_c::start") 
               << "Server started\n";
   return true;
}

bool data_submission_c::stop() {

   if (!p_running.load()) {
      return true;
   }

   if (_message_server) {
      _message_server->stop();
      delete _message_server;
      _message_server = nullptr;
   }

   {
      // Check if there still exists data in the metric queue. 
      // If there is, go through and attempt to store / stream them one last time
      //
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);

      if (!_metric_queue.empty()) {
         LOG(INFO) << TAG("data_submission_c::stop") 
                     << "Attempting to submit the last " 
                     << _metric_queue.size() 
                     << " enqueued data before stop\n";

         while(!_metric_queue.empty()) {
            auto entry = _metric_queue.front();
            _database->store(entry.metric);
            _stream_server->submit_metric(entry.metric);
            _metric_queue.pop();
         }
      }
   }

   p_running.store(false);

   if (p_thread.joinable()) {
      p_thread.join();
   }

   return true;
}

void data_submission_c::run() {
   auto last_prune = std::chrono::high_resolution_clock::now();

   while(p_running.load()) {
      std::this_thread::sleep_for(500ms);

      // Validate / submit metrics to database and streamers
      //
      submit_metrics();
   }
}

void data_submission_c::receive_message(std::string metric_data) {

   LOG(INFO) << TAG("data_submission_c::receive_message") << metric_data << "\n";

   if (metric_data.empty()) {
      return;
   }

   // Validate the reading
   crate::metrics::sensor_reading_v1 reading;
   if (!reading.decode_from(metric_data)) {
      LOG(WARNING) << TAG("data_submission_c::receive_message") << "Failed to decode data: " << metric_data << "\n";
      return;
   }
   
   // Put the reading in the queue
   {
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
      _metric_queue.push({
         .submission_attempts = 0,
         .metric = reading
      });
   }
}

void data_submission_c::submit_metrics() {

   {
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
      if (_metric_queue.empty()) {
         return;
      }
   }

   /*
      Here we will store a subset of the metrics queued up into a new container
      to ensure we don't keep the mutex busy too long. This will take some 
      expense in processing, but since we are reaching out to the cache, and
      potentially making http requests to the registrar we want to ensure that
      we move the processing of metrics without the lock
   */
   std::vector<db_entry_queue> metrics;
   uint8_t count = 0;
   {
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
      while (!_metric_queue.empty() && count++ < MAX_METRICS_PER_BURST) {
         metrics.push_back(
            _metric_queue.front()
         );
         _metric_queue.pop();
      }
   }

   // Now we have up-to MAX_METRICS_PER_BURST metrics to process. Any metrics that
   // for some reason can't be validated will be returned to the queue for later
   // processing up-to MAX_SUBMISSION_ATTEMPTS times

   std::vector<db_entry_queue> re_enqueue;

   for( auto& entry : metrics) {
      
      // Count this as an attempt to submit the metric
      entry.submission_attempts++;

      // Break apart the metric
      auto [ts, node_id, sensor_id, value] = entry.metric.getData();

      // Retrieve the node 

      // TODO: This decodes the node every time we look for something
      //       which is a bit slow. Before monolith, we used crate::registrar::cache
      //       and we could implement something like that in th future
      //       but we'd want to do that in a shared place so others can use
      //       a cache mechanism like that - maybe we can update crate
      //       to have a general cache like that

      std::optional<std::string> node_info = _registrar->load(node_id);
      if (!node_info.has_value()) {
         LOG(WARNING) << TAG("data_submission_c::submit_metrics")
                        << "No node data found for id: "
                        << node_id 
                        << "\n";
         continue; 
      }

      crate::registrar::node_v1 raw_node;
      if (!raw_node.decode_from(*node_info)) {
         LOG(WARNING) << TAG("data_submission_c::submit_metrics")
                        << "Failed to decode node : "
                        << node_id 
                        << "\n";
         continue; 
      }

      bool found {false};
      auto [id, desc, sensors] = raw_node.get_data();
      for (auto& s : sensors) {
         if (s.id == sensor_id) {
            found = true;
         }
      }

      if (!found) {
         LOG(WARNING) << TAG("data_submission_c::submit_metrics")
                        << "Unable to locate sensor : "
                        << sensor_id 
                        << " for node : " 
                        << node_id
                        << "\n";
         continue;
      }
      
      // Store the metric in the local database  
      //
      _database->store(entry.metric);

      // Submit to stream server - it may be stopped or otherwise not accepting metrics
      // so we re enqueue it if thats the case
      //
      if (!_stream_server->submit_metric(entry.metric)) {

         // Check to see if the submission attempts indicate that we need to drop the thing
         if (entry.submission_attempts >= MAX_SUBMISSION_ATTEMPTS) {
            LOG(INFO) << TAG("data_submission_c::submit_metrics") 
                        << "Dropping metric (too many submission attempts)\n";
            continue;
         }

         // If we reach here that means we can re-enqueue the metric for trying later
         // but since we are doing a lot we put it in a different storage medium until
         // we iterate through this entire burst. Once this burst is complete we will
         // take the lock from the queue and re-enqueue them
         re_enqueue.push_back(entry);
      }
   }

   // Now we need to see if we want to re-enqueue anything
   if (re_enqueue.empty()) {
      return;
   }
   
   // Re-enqueue the metric
   {  
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
      for(auto& entry : re_enqueue) {
         _metric_queue.push(entry);
      }
   }
}



} // namespace services
} // namespace monolith