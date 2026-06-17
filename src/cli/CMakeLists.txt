add_executable(lightroot src/main.cpp)

target_link_libraries(lightroot
  PRIVATE lightroot_core CLI11::CLI11 fmt::fmt lightroot_warnings)
