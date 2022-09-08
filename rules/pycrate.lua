
-- This is an example of the function required to receive metric readings directly from the
-- rule_executor that is submitted via the data_submission service. This method is called 
-- almost as frequently as metrics are received by the server
function accept_reading_v1_from_monolith(timestamp, node_id, sensor_id, value)

   monolith_dispatch_action("test_controller", "test", 42.314)
end