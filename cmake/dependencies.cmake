# LightRoot third-party dependencies. All version pins live here.
#
# Offline: set CPM_SOURCE_CACHE (see cmake/cpm_bootstrap.cmake) to a directory
# that already holds these sources, or vendor them and switch to SOURCE_DIR.

CPMAddPackage("gh:fmtlib/fmt#10.2.1")
CPMAddPackage("gh:cliutils/cli11#v2.4.2")
CPMAddPackage("gh:marzer/tomlplusplus#v3.4.0")
CPMAddPackage("gh:nlohmann/json#v3.11.3")

if(LIGHTROOT_BUILD_TESTS)
  CPMAddPackage(
    NAME GTest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.14.0
    OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt ON")
endif()
