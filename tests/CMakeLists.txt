link_directories(${CPPUTEST_LIBRARIES})

find_package(libutil REQUIRED)

include_directories(
  ../src/
  ${CPPUTEST_INCLUDE_DIRS}
  ${LIBUTIL_INCLUDE_DIRS}
)

add_executable(monolith-tests
         ${DB_SOURCES}
         ${SERVICES_SOURCES}
         ${PORTAL_SOURCES}
         ${TLD_SOURCES}
         sensor_registrar_test.cpp
         streaming_tests.cpp
         server_tests.cpp
         main.cpp)


target_link_libraries(monolith-tests
        ${CPPUTEST_LDFLAGS}
        ${CRATE_LIBRARIES}
        ${NETTLE_LIBRARIES}
        ${SQLite3_LIBRARIES}
        libutil
        Threads::Threads
        rocksdb 
        dl
        rt)

add_custom_command(TARGET monolith-tests COMMAND ./monolith-tests POST_BUILD)
