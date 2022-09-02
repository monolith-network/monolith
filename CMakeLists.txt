cmake_minimum_required(VERSION 3.8)

project(monolith)

set(CMAKE_CXX_STANDARD 20)

#
# Options
#
option(COMPILE_TESTS "Execute unit tests" ON)
option(WITH_ASAN     "Compile with ASAN" OFF)

#
# Setup build type 'Release vs Debug'
#
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified.")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

#
#  Find SQLite3
#
find_package (SQLite3 REQUIRED)
include_directories(${SQLite3_INCLUDE_DIRS})

#
# Find Nettle
#
find_package(Nettle REQUIRED)
include_directories(${NETTLE_INCLUDE_DIRS})

#
# Find Crate
#
find_package(crate REQUIRED)
include_directories(${CRATE_INCLUDE_DIRS})

#
# Find Threads
#
find_package (Threads REQUIRED)

#
# Locate CPPUTest
#
if(DEFINED ENV{CPPUTEST_HOME})
    message(STATUS "Using CppUTest home: $ENV{CPPUTEST_HOME}")
    set(CPPUTEST_INCLUDE_DIRS $ENV{CPPUTEST_HOME}/include)
    set(CPPUTEST_LIBRARIES $ENV{CPPUTEST_HOME}/lib)
    set(CPPUTEST_LDFLAGS CppUTest CppUTestExt)
else()
    find_package(PkgConfig REQUIRED)
    pkg_search_module(CPPUTEST REQUIRED cpputest>=3.8)
    message(STATUS "Found CppUTest version ${CPPUTEST_VERSION}")
endif()

#
# Setup ASAN
#
if(WITH_ASAN)
  set(CMAKE_CX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
  set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

#
#  Ensure that tld includes available everywhere
#
include_directories(
   ${CMAKE_SOURCE_DIR}/src)

#
#  Sources
#
set(DB_SOURCES
   ${CMAKE_SOURCE_DIR}/src/db/kv.cpp
)

set(ALERT_SOURCES
   ${CMAKE_SOURCE_DIR}/src/alert/alert.cpp
)

set(SERVICES_SOURCES
   ${CMAKE_SOURCE_DIR}/src/services/metric_db.cpp
   ${CMAKE_SOURCE_DIR}/src/services/data_submission.cpp
   ${CMAKE_SOURCE_DIR}/src/services/metric_streamer.cpp
   ${CMAKE_SOURCE_DIR}/src/services/rule_executor.cpp
   ${CMAKE_SOURCE_DIR}/src/services/app.cpp
)

set(PORTAL_SOURCES
   ${CMAKE_SOURCE_DIR}/src/portal/portal.cpp
)

set(TLD_SOURCES
   ${CMAKE_SOURCE_DIR}/src/heartbeats.cpp
)

#
#  Test
#
if(COMPILE_TESTS)
   add_subdirectory(tests)
endif()

#
#  Create the app
#
add_executable(monolith
         ${DB_SOURCES}
         ${ALERT_SOURCES}
         ${SERVICES_SOURCES}
         ${SHARED_SOURCES}
         ${PORTAL_SOURCES}
         ${TLD_SOURCES}
         src/main.cpp)

#
#  Link 
#
target_link_libraries(monolith
        PRIVATE
        ${CRATE_LIBRARIES}
        ${NETTLE_LIBRARIES}
        ${SQLite3_LIBRARIES}
        Threads::Threads
        lua5.3
        rocksdb
        dl
        rt)

#
#  Copy configs
#  
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/configs
      DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

#
#  Copy rules
#  
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/rules
      DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

#
#  Copy static files
#
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/static
      DESTINATION ${CMAKE_CURRENT_BINARY_DIR})