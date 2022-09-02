-- This is an example of how sensor rules and alerts can work
-- This specific example is meant to be used in conjunction with `demu`
-- the device emulator. It shows an example of how we can make object to
-- track state of of sensors and how to send off alerts when specific 
-- thresholds are met / exceeded

-- Static ids for sensors on demu node
sensor_id_temperature = "58eb7f04-30f3-4a0c-94b7-c6f140870d1d"
sensor_id_motion = "6994e68e-77b5-4f77-a11b-87908460b7ca"
sensor_id_light = "a0e2ea19-469b-490c-b28f-4513b9c602e0"
sensor_id_humidity = "44d7dae7-49a5-4510-b589-b62c468184f0"
sensor_id_flame = "f360c409-2c4d-420f-bf2d-56c9056d6583"
sensor_id_air_pressure = "0c23501a-3fb6-42f5-88eb-071e7a74b7d6"

--
-- Light sensor object
--
LightSensorMonitor = {}
function LightSensorMonitor:new(o)
   o = o or {}
   setmetatable(o, self)
   self.__index = self
   self.current_value = 100
   return o
end

function LightSensorMonitor:report(value)
   current_bound = self.current_value
   -- Bound light range to 0 or 1
   if value <= 5 then 
      current_bound = 0
   end
   if value > 5 then 
      current_bound = 1
   end

   if current_bound ~= self.current_value then
      if current_bound == 0 then 
         monolith_trigger_alert(0, "Light sensor says its dark (value < 5)")
      end
      if current_bound == 1 then 
         monolith_trigger_alert(0, "Light sensor says its light (value > 5)")
      end
   end

   self.current_value = current_bound
end

--
-- Motion sensor object
--
MotionSensorMonitor = {}
function MotionSensorMonitor:new(o)
   o = o or {}
   setmetatable(o, self)
   self.__index = self
   self.current_value = 0
   return o
end

function MotionSensorMonitor:report(value)
   if self.current_value ~= value then 
      if value == 1 then 
         monolith_trigger_alert(1, "New motion detected")
      end
   end
   self.current_value = value
end

-- Helper function
local function string_is_empty(s)
   return s == nil or s == ''
end

-- Create the monitor objects
local light_monitor = LightSensorMonitor:new()
local motion_monitor = MotionSensorMonitor:new()

-- Called by monolith
function accept_reading_v1_from_monolith(timestamp, node_id, sensor_id, value)

   if string_is_empty(sensor_id) then
      print("Received an empty sensor id?")
      return
   end

   -- Check light
   if sensor_id == sensor_id_light then
      light_monitor:report(value)
      return
   end

   -- Check motion
   if sensor_id == sensor_id_motion then
      motion_monitor:report(value)
      return
   end
   
   -- Check flame
   if sensor_id == sensor_id_flame then
      if value > 0 then
         monolith_trigger_alert(2, "There is literally a fire detected. Intensity: " .. value)
      end
      return
   end
end