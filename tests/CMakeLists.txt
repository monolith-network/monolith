link_directories(${CPPUTEST_LIBRARIES})

find_package(libutil REQUIRED)

include_directories(
  ${CPPUTEST_INCLUDE_DIRS}
  ${LIBUTIL_INCLUDE_DIRS}
)

add_executable(monolith-tests
         ${APP_SOURCES}
         server_tests.cpp
         main.cpp)


target_link_libraries(monolith-tests
        ${CPPUTEST_LDFLAGS}
        ${CRATE_LIBRARIES}
        libutil
        Threads::Threads
        rocksdb dl)

add_custom_command(TARGET monolith-tests COMMAND ./monolith-tests POST_BUILD)
