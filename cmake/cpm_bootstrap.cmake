# CPM.cmake bootstrap.
#
# Downloads the single-file CPM.cmake script on first configure and includes it.
# For offline / shared reuse set the CPM_SOURCE_CACHE environment variable (or
# CMake variable) to a directory that already holds a populated cache.
#
# https://github.com/cpm-cmake/CPM.cmake
set(CPM_DOWNLOAD_VERSION 0.40.0)

if(CPM_SOURCE_CACHE)
  set(_cpm_cache "${CPM_SOURCE_CACHE}")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(_cpm_cache "$ENV{CPM_SOURCE_CACHE}")
else()
  set(_cpm_cache "${CMAKE_BINARY_DIR}/cpm-cache")
  set(ENV{CPM_SOURCE_CACHE} "${_cpm_cache}")
endif()
set(CPM_SOURCE_CACHE "${_cpm_cache}" CACHE PATH "CPM shared package cache")

set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
  message(STATUS "CPM: downloading v${CPM_DOWNLOAD_VERSION} -> ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    "${CPM_DOWNLOAD_LOCATION}"
    STATUS _cpm_dl_status
    SHOW_PROGRESS)
  list(GET _cpm_dl_status 0 _cpm_dl_code)
  if(NOT _cpm_dl_code EQUAL 0)
    message(FATAL_ERROR
      "CPM: download failed (${_cpm_dl_status}). Populate CPM_SOURCE_CACHE for offline use.")
  endif()
endif()

include("${CPM_DOWNLOAD_LOCATION}")
