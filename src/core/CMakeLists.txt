add_library(lightroot_core STATIC
  src/types.cpp
  src/version.cpp
  src/dependency.cpp
  src/config.cpp
  src/manifest.cpp
  src/device_table.cpp
  src/registry.cpp
  src/resolver.cpp
  src/exec.cpp
  src/doctor.cpp
  src/executor.cpp
  src/backend.cpp
  src/lockfile.cpp)

target_include_directories(lightroot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(lightroot_core PUBLIC cxx_std_20)
target_link_libraries(lightroot_core
  PUBLIC fmt::fmt tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json
  PRIVATE lightroot_warnings)
