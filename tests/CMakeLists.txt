include(GoogleTest)

add_executable(test_lightroot
  test_version.cpp
  test_dependency.cpp
  test_manifest.cpp
  test_config.cpp
  test_device_table.cpp
  test_lockfile.cpp
  test_backend.cpp
  test_registry.cpp
  test_resolver.cpp
  test_doctor.cpp
  test_exec.cpp
  test_executor.cpp)

target_link_libraries(test_lightroot
  PRIVATE lightroot_core GTest::gtest_main lightroot_warnings)

# Copy fixtures next to the test binary so relative "data/..." paths resolve.
add_custom_target(lightroot_test_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_CURRENT_BINARY_DIR}/data)
add_dependencies(test_lightroot lightroot_test_data)

gtest_discover_tests(test_lightroot)
