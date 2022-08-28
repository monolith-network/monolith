#ifndef MONOLITH_SERVICES_METRIC_STREAMER_HPP
#define MONOLITH_SERVICES_METRIC_STREAMER_HPP

#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <crate/metrics/reading.hpp>
#include "interfaces/service_if.hpp"

namespace monolith {
namespace services {

//! \brief A server that is used to stream metrics to various registered endpoints
class metric_streamer_c : public service_if {

public:
   //! \brief Create the server
   metric_streamer_c();

   //! \brief Submit a metric to be streamed to the registered destinations
   //!        if no destinations are registered the metric will be lost to time
   //! \returns true iff the metric gets enqueues for send
   //! \note  This enqueues the metric to be streamed and may not come out immediatly,
   //!        but the metric will come out in the order they are put in
   bool submit_metric(crate::metrics::sensor_reading_v1 metric);
   
   //! \brief Start the server
   //! \returns true iff the server is started
   virtual bool  start() override final;

   //! \brief Stop the server
   //! \returns true iff the server is stopped
   virtual bool stop() override final;

   //! \brief Add a streaming destination
   //! \param address The destination address
   //! \param port The port 
   //! \note This enqueues the destination to be added, and may take a moment
   void add_destination(const std::string& address, uint32_t port);
   
   //! \brief Delete a streaming destination
   //! \param address The destination address
   //! \param port The port 
   //! \note This enqueues the destination to be deleted, and may take a moment
   void del_destination(const std::string& address, uint32_t port);

private:
   /*
      Because outside influences can add/delete endpoints we need to guard against intentional or
      acciental spam. We do this by enqueuing updates to add/delete endpoints and then, on our own
      schedule we add or remove them up-to the BURST_UPDATE_DESTINATION number of updates.

      Similarly, we need to make sure that metrics being submitted aren't keeping our mutexes locked
      for too long so we schedule BURST_STREAM_METRIC number of metrics to be burst out to destinations
   */
   static constexpr uint8_t BURST_UPDATE_DESTINATION = 10;   // Maximum amount of adds that can happen at a time
   static constexpr uint8_t BURST_STREAM_METRIC = 100;       // Maximum number of metrics that can be put into a 
                                                             // single stream send
   static constexpr double INTERVAL_DESTINATION_UPDATE = 2.5;// Interval update period
   static constexpr double INTERVAL_STREAM_METRICS = 0.25;   // Interval send out metrics

   // We should only have a handful of endpoints to service (<10) realistically so we don't need
   // a fancy map or anything to ensure we can locate items to be added/removed too quickly
   // though if that were to change we would need to update this to something that would take
   // that into consideration 
   //
   struct endpoint {
      std::string address;
      uint32_t port {0};
   };
   std::vector<endpoint> _stream_receivers;
   std::mutex _stream_receivers_mutex;

   // The updates add/delete are paired here with an endpoint and enqueued together. Consideration of having 
   // two seperate queues, one for add, one for delete, was had but deemed pointless. Here we stuff them
   // together and execute commands as needed.
   //
   enum class command {
      ADD,
      DELETE
   };

   struct update {
      command cmd;
      endpoint entry;
   };
   std::queue<update> _stream_receiver_updates;
   std::mutex _stream_receiver_updates_mutex;

   // On shutdown we may have more outbount metrics
   // in that event we want to stop accepting metrics and attempt one last time to write
   // them all out. This will help us cut off the input while this happens
   //
   std::atomic<bool> _accepting_metrics {false};
   std::queue<crate::metrics::sensor_reading_v1> _metric_queue; // Outbount queue
   std::mutex _metric_queue_mutex;
   uint64_t _metric_sequence {0};   // Monotonically increasing sequence counter

   void run();

   bool contains_endpoint(endpoint& e, size_t& idx);
   void perform_destination_updates();
   void perform_metric_streaming();
};

} // namespace services
} // namespace monolith

#endif