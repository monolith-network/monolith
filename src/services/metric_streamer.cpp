#include "metric_streamer.hpp"
#include <crate/externals/aixlog/logger.hpp>
#include <crate/metrics/streams/stream_data.hpp>
#include <crate/networking/message_writer.hpp>

namespace monolith {
namespace services {
   
using namespace std::chrono_literals;

metric_streamer_c::metric_streamer_c() {}

bool metric_streamer_c::start() {
   _accepting_metrics.store(true);
   p_running.store(true);

   p_thread = std::thread(&metric_streamer_c::run, this);

   return true;
}

bool metric_streamer_c::stop() {
   _accepting_metrics.store(false);

   p_running.store(false);

   if (p_thread.joinable()) {
      p_thread.join();
   }

   return true;
}

void metric_streamer_c::add_destination(const std::string& address, uint32_t port) {
   
   const std::lock_guard<std::mutex> lock(_stream_receiver_updates_mutex);
   _stream_receiver_updates.push({
      command::ADD,
      {
         address,
         port
      }
   });
}

void metric_streamer_c::del_destination(const std::string& address, uint32_t port) {
   
   const std::lock_guard<std::mutex> lock(_stream_receiver_updates_mutex);
   _stream_receiver_updates.push({
      command::DELETE,
      {
         address,
         port
      }
   });
}

bool metric_streamer_c::submit_metric(crate::metrics::sensor_reading_v1 metric)
{
   if (!_accepting_metrics.load()) {
      LOG(INFO) << TAG("metric_streamer_c::submit_metric") << "Not accepting metrics at this time\n";
      return false;
   }

   const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
   _metric_queue.push(metric);
   return true;
}

void metric_streamer_c::run() {

   auto last_destination_update = std::chrono::high_resolution_clock::now();
   auto last_metric_data_burst = std::chrono::high_resolution_clock::now();

   while(p_running.load()) {

      // Check passed time since last destination update to see if we need
      // to go ahead and burst out some stream destination changes
      //
      std::chrono::duration<double> update_delta = 
         std::chrono::high_resolution_clock::now() - last_destination_update;

      if (update_delta.count() >= INTERVAL_DESTINATION_UPDATE) {
         perform_destination_updates();
         last_destination_update = std::chrono::high_resolution_clock::now();
      }

      // Check passed time since last metric data burst to see if we need 
      // to go ahead and burst out some metrics
      //
      std::chrono::duration<double> metric_burst_delta = 
         std::chrono::high_resolution_clock::now() - last_metric_data_burst;

      if (metric_burst_delta.count() >= INTERVAL_STREAM_METRICS) {
         perform_metric_streaming();
         last_metric_data_burst = std::chrono::high_resolution_clock::now();
      }

      // Thread sleep - Everything is in terms of seconds in operation so this won't hold
      // anything up, but it will keep the thread from grinding
      //
      std::this_thread::sleep_for(1ms);

      // Check to see if we need to purge metrics from memory
      //
      check_purge();
   }
}

void metric_streamer_c::check_purge() {

   const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
   if (_metric_queue.size() < MAX_QUEUED_METRICS) {
      return;
   }
   
   uint32_t removed {0};
   while(removed++ < NUM_DROP_METRICS) {
      _metric_queue.pop();
   }
}

// Check if an entry exists within the stream receivers, if it does
// return true and set the idx to the spot that its in so someone
// can erase it or w/e
//
bool metric_streamer_c::contains_endpoint(endpoint& e, size_t& idx) {
   idx = 0;
   size_t c {0};
   const std::lock_guard<std::mutex> lock(_stream_receivers_mutex);
   for(auto& entry : _stream_receivers) {
      if ( (entry.address == e.address) && (entry.port == e.port) ) {
         idx = c;
         return true;
      }
      c++;
   }
   return false;
}

void metric_streamer_c::perform_destination_updates() {
   {
      const std::lock_guard<std::mutex> lock(_stream_receiver_updates_mutex);
      if (_stream_receiver_updates.empty()) {
         return;
      }
   }

   LOG(INFO) << TAG("metric_streamer_c::perform_destination_updates") << "Updating destinations\n";

   size_t number_completed {0};
   const std::lock_guard<std::mutex> lock(_stream_receiver_updates_mutex);
   while (number_completed++ < BURST_UPDATE_DESTINATION && 
          !_stream_receiver_updates.empty()) {

      size_t idx {0};
      auto update = _stream_receiver_updates.front();
      _stream_receiver_updates.pop();
      switch(update.cmd) {
         case command::ADD: 
         {
            // Ensure that the thing doesn't exist before trying to add it so
            // we don't get dups
            //
            if (contains_endpoint(update.entry, idx)) {
               continue;
            } else {
               const std::lock_guard<std::mutex> lock(_stream_receivers_mutex);
               LOG(INFO) << TAG("metric_streamer_c::perform_destination_updates") << "Added: " << update.entry.address << ":" << update.entry.port << "\n" ;
               _stream_receivers.push_back(update.entry);
            }
            break;
         }
         case command::DELETE:
         {
            // Ensure that the thing exists and retrieve the idx its in so 
            // we can have a means to erase it
            //
            if (contains_endpoint(update.entry, idx)) {
               const std::lock_guard<std::mutex> lock(_stream_receivers_mutex);
               _stream_receivers.erase(_stream_receivers.begin() + idx);
            }
            break;
         }
      }
   }
}

void metric_streamer_c::perform_metric_streaming() {

   // If we have nobody to send data to why send data?
   {
      const std::lock_guard<std::mutex> lock(_stream_receivers_mutex);
      if (_stream_receivers.empty()) {
   //      LOG(INFO) << TAG("metric_streamer_c::perform_metric_streaming") << "No receivers present\n";
         return;
      }
   }

   // If we have no data we have no purpose being here
   {
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
      if (_metric_queue.empty()) {
  //       LOG(INFO) << TAG("metric_streamer_c::perform_metric_streaming") << "No metrics present\n";
         return;
      }
   }

   size_t number_completed {0};
   crate::metrics::streams::stream_data_v1 stream_package(_metric_sequence++);

   // Retrieve either a subset or all of the metrics, up to 
   // BURST_STREAM_METRIC in an anonymous scope so we don't keep
   // the mutex too long
   //
   {
      const std::lock_guard<std::mutex> lock(_metric_queue_mutex);
      
      while (number_completed++ < BURST_STREAM_METRIC && 
            !_metric_queue.empty()) {

         stream_package.add_metric(_metric_queue.front());
         _metric_queue.pop();
      }
   }

   // Stamp the package to finalize it for sending
   //
   stream_package.stamp();

   std::string encoded_package;
   if (!stream_package.encode_to(encoded_package)) {
      LOG(ERROR) << TAG("metric_streamer_c::perform_metric_streaming") 
                  << "Failed to encode stream package (repercussion: data loss)\n";
      return;
   }

   // Create a copy of the receivers so we don't hold the mutex while 
   // performing network operations
   //
   std::vector<endpoint> receivers;
   {
      const std::lock_guard<std::mutex> lock(_stream_receivers_mutex);
      receivers = _stream_receivers;
   }

   // Send data to the copy of receivers that we have now
   //
   for(auto& destination : receivers) {

      // Create a writer for the endpoint and try to send off the data
      //
      bool okay {false};
      auto writer = crate::networking::message_writer(destination.address, destination.port);

      writer.write(encoded_package, okay);

      // If the data fails to be written there is no action we can take. The endpoint might
      // be down, we really don't know.  
      //
      if (!okay) {
         LOG(WARNING) << TAG("metric_streamer_c::perform_metric_streaming")
                        << "Writer failed to send data to ["
                        << destination.address
                        << ":"
                        << destination.port
                        << "\n";
      }
   }
}

} // namespace services
} // namespace monolith