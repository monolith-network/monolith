
-- This is an example of the function required to receive metric readings directly from the
-- rule_executor that is submitted via the data_submission service. This method is called 
-- almost as frequently as metrics are received by the server
function accept_reading_v1_from_monolith(timestamp, node_id, sensor_id, value)

   print("Lua got metric reading| ts:" .. timestamp .. ", node:" .. node_id .. ", sensor:" .. sensor_id .. ", value:" .. value)

   --monolith_trigger_alert(0, "Alert message")
   --monolith_dispatch_action("controller_id", "action_id", 42.314)
end