-- This is an example of how sensor rules and alerts can work
-- This specific example is meant to be used in conjunction with `demu`
-- the device emulator. It shows an example of how we can make object to
-- track state of of sensors and how to send off alerts when specific 
-- thresholds are met / exceeded

-- Static ids for sensors on demu node
sensor_id_internal_temperature = "internal_temperature"
sensor_id_internal_humidity = "internal_humidity"
sensor_id_internal_motion = "internal_motion"
sensor_id_internal_light = "internal_light"

-- Controllers and actions on device emulator
controller_id = "weather_station_controller"
nightlight_action = "set_night_light"
movement_action = "movement_detected"

--
-- Light sensor object
--
InternalLightSensorMonitor = {}
function InternalLightSensorMonitor:new(o)
   o = o or {}
   setmetatable(o, self)
   self.__index = self
   return o
end

function InternalLightSensorMonitor:report(value)
   if value == 0.0 then
      print("Internal light - light no longer detected")
      monolith_trigger_alert(0, "Internal light - light no longer detected")

      -- Turn on the night light
      monolith_dispatch_action(controller_id, nightlight_action, 1.0)
   end
   if value == 1.0 then 
      print("Internal light - light detected")
      monolith_trigger_alert(1, "Internal light - light detected")
      monolith_dispatch_action(controller_id, nightlight_action, 0.0)
   end
end

--
-- Motion sensor object
--
InternalMotionSensorMonitor = {}
function InternalMotionSensorMonitor:new(o)
   o = o or {}
   setmetatable(o, self)
   self.__index = self
   return o
end

function InternalMotionSensorMonitor:report(value)
   if value == 1.0 then
      monolith_trigger_alert(2, "Internal motion - motion detected")
      monolith_dispatch_action(controller_id, movement_action, 1.0)
   end
end

--
-- Temperature sensor object
--
InternaltemperatureSensorMonitor = {}
function InternaltemperatureSensorMonitor:new(o)
   o = o or {}
   setmetatable(o, self)
   self.__index = self
   return o
end

function InternaltemperatureSensorMonitor:report(value)
   if value < 55.0 then
      print("Need to turn on a heat source - NYD")
   end

   if value > 80.0 then
      print("Need to turn on a fan - NYD")
   end
end

--
-- Humidity sensor object
--
InternalHumiditySensorMonitor = {}
function InternalHumiditySensorMonitor:new(o)
   o = o or {}
   setmetatable(o, self)
   self.__index = self
   return o
end

function InternalHumiditySensorMonitor:report(value)
   if value >= 90 then
      print("Need to remove some humidity")
   end
end

-- Helper function
local function string_is_empty(s)
   return s == nil or s == ''
end

-- Create the monitor objects
local light_monitor = InternalLightSensorMonitor:new()
local motion_monitor = InternalMotionSensorMonitor:new()
local temperature_monitor = InternaltemperatureSensorMonitor:new()
local humidity_monitor = InternalHumiditySensorMonitor:new()

-- Called by monolith
function accept_reading_v1_from_monolith(timestamp, node_id, sensor_id, value)

   if string_is_empty(sensor_id) then
      print("Received an empty sensor id?")
      return
   end

   -- Check light
   if sensor_id == sensor_id_internal_light then
      light_monitor:report(value)
      return
   end

   -- Check motion
   if sensor_id == sensor_id_internal_motion then
      motion_monitor:report(value)
      return
   end
   
   -- Check temperature
   if sensor_id == sensor_id_internal_temperature then
      temperature_monitor:report(value)
      return
   end
   
   -- Check humidity
   if sensor_id == sensor_id_internal_motion then
      humidity_monitor:report(value)
      return
   end
end