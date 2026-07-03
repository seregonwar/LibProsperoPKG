cmake_minimum_required(VERSION 3.24)

set(LIBPROSPEROPKG_VERSION "0.1.0" CACHE STRING "LibProsperoPkg release version")

project(
  LibProsperoPkgCpp
  VERSION ${LIBPROSPEROPKG_VERSION}
  DESCRIPTION "Cross-platform C++ port of LibProsperoPkg"
  LANGUAGES CXX)

option(LIBPROSPEROPKG_BUILD_TOOLS "Build command-line tools" ON)
option(LIBPROSPEROPKG_BUILD_TESTS "Build native tests" ON)
option(LIBPROSPEROPKG_BUILD_C_API "Build the legacy-compatible C ABI shared library" ON)
option(LIBPROSPEROPKG_ENABLE_IPO "Enable interprocedural optimization for release builds" ON)
option(LIBPROSPEROPKG_OPTIMIZE_SIZE "Prefer smaller release binaries" ON)
option(LIBPROSPEROPKG_INSTALL_STATIC "Install the static C++ library" OFF)
set(LIBPROSPEROPKG_COMPARE_BASELINE "" CACHE PATH "Optional legacy C# native directory for comparison tests")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CheckIPOSupported)
include(GNUInstallDirs)

find_package(ZLIB QUIET)

if(LIBPROSPEROPKG_ENABLE_IPO)
  check_ipo_supported(RESULT LIBPROSPEROPKG_IPO_SUPPORTED OUTPUT LIBPROSPEROPKG_IPO_ERROR)
else()
  set(LIBPROSPEROPKG_IPO_SUPPORTED OFF)
endif()

function(lpp_apply_common_settings target)
  get_target_property(target_type ${target} TYPE)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(LIBPROSPEROPKG_OPTIMIZE_SIZE)
      target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Os>)
      if(NOT target_type STREQUAL "STATIC_LIBRARY")
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
      endif()
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(LIBPROSPEROPKG_OPTIMIZE_SIZE)
      target_compile_options(${target} PRIVATE
        -ffunction-sections
        -fdata-sections
        $<$<CONFIG:Release>:-Os>)
      if(NOT target_type STREQUAL "STATIC_LIBRARY")
        if(APPLE)
          target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-Wl,-dead_strip>)
        else()
          target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)
        endif()
      endif()
    endif()
  endif()

  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

  if(LIBPROSPEROPKG_IPO_SUPPORTED)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
endfunction()

add_library(prosperopkg_cpp STATIC
  src/src/aes_xts.cpp
  src/src/build.cpp
  src/src/content_id.cpp
  src/src/crc32c.cpp
  src/src/gp5.cpp
  src/src/hash.cpp
  src/src/image_digests.cpp
  src/src/pfsc.cpp
  src/src/pfs_keys.cpp
  src/src/pfs_image.cpp
  src/src/pfs_signature.cpp
  src/src/pkg.cpp
  src/src/self.cpp
  src/src/ucp.cpp)

target_include_directories(prosperopkg_cpp
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(prosperopkg_cpp PUBLIC cxx_std_20)
target_compile_definitions(prosperopkg_cpp PUBLIC
  PROSPEROPKG_VERSION="${PROJECT_VERSION}")

if(ZLIB_FOUND)
  target_link_libraries(prosperopkg_cpp PUBLIC ZLIB::ZLIB)
  target_compile_definitions(prosperopkg_cpp PUBLIC PROSPEROPKG_HAS_ZLIB=1)
endif()

lpp_apply_common_settings(prosperopkg_cpp)

if(LIBPROSPEROPKG_BUILD_C_API)
  add_library(LibProsperoPkg SHARED src/src/c_api.cpp)
  target_link_libraries(LibProsperoPkg PRIVATE prosperopkg_cpp)
  target_compile_definitions(LibProsperoPkg PRIVATE
    LIBPROSPEROPKG_C_API_BUILD
    PROSPEROPKG_VERSION="${PROJECT_VERSION}")
  target_include_directories(LibProsperoPkg
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(LibProsperoPkg PRIVATE cxx_std_20)
  set_target_properties(LibProsperoPkg PROPERTIES
    OUTPUT_NAME "LibProsperoPkg"
    PREFIX ""
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  lpp_apply_common_settings(LibProsperoPkg)
endif()

if(LIBPROSPEROPKG_BUILD_TOOLS)
  add_executable(prosperopkg-inspect src/tools/pkg_inspect.cpp)
  target_link_libraries(prosperopkg-inspect PRIVATE prosperopkg_cpp)
  lpp_apply_common_settings(prosperopkg-inspect)

  add_executable(prosperopkg-fself src/tools/fself.cpp)
  target_link_libraries(prosperopkg-fself PRIVATE prosperopkg_cpp)
  lpp_apply_common_settings(prosperopkg-fself)

  add_executable(prosperopkg-gp5 src/tools/gp5.cpp)
  target_link_libraries(prosperopkg-gp5 PRIVATE prosperopkg_cpp)
  lpp_apply_common_settings(prosperopkg-gp5)

  add_executable(prosperopkg-keys src/tools/keys.cpp)
  target_link_libraries(prosperopkg-keys PRIVATE prosperopkg_cpp)
  lpp_apply_common_settings(prosperopkg-keys)
endif()

if(LIBPROSPEROPKG_BUILD_TESTS)
  enable_testing()

  add_executable(prosperopkg_tests src/tests/test_main.cpp)
  target_link_libraries(prosperopkg_tests PRIVATE prosperopkg_cpp)
  lpp_apply_common_settings(prosperopkg_tests)

  if(TARGET prosperopkg-inspect)
    target_compile_definitions(prosperopkg_tests PRIVATE
      PROSPEROPKG_INSPECT_PATH="$<TARGET_FILE:prosperopkg-inspect>")
    add_dependencies(prosperopkg_tests prosperopkg-inspect)
  endif()

  if(TARGET prosperopkg-fself)
    target_compile_definitions(prosperopkg_tests PRIVATE
      PROSPEROPKG_FSELF_PATH="$<TARGET_FILE:prosperopkg-fself>")
    add_dependencies(prosperopkg_tests prosperopkg-fself)
  endif()

  if(TARGET prosperopkg-gp5)
    target_compile_definitions(prosperopkg_tests PRIVATE
      PROSPEROPKG_GP5_PATH="$<TARGET_FILE:prosperopkg-gp5>")
    add_dependencies(prosperopkg_tests prosperopkg-gp5)
  endif()

  if(TARGET prosperopkg-keys)
    target_compile_definitions(prosperopkg_tests PRIVATE
      PROSPEROPKG_KEYS_PATH="$<TARGET_FILE:prosperopkg-keys>")
    add_dependencies(prosperopkg_tests prosperopkg-keys)
  endif()

  add_test(NAME prosperopkg.native COMMAND prosperopkg_tests)

  if(TARGET prosperopkg-inspect)
    add_test(NAME prosperopkg.inspect.selftest COMMAND prosperopkg-inspect --self-test)
  endif()

  if(LIBPROSPEROPKG_COMPARE_BASELINE AND TARGET LibProsperoPkg AND TARGET prosperopkg-inspect)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(
        NAME prosperopkg.compare.csharp
        COMMAND Python3::Interpreter
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/compare_native_parity.py
          --baseline-dir ${LIBPROSPEROPKG_COMPARE_BASELINE}
          --cpp-lib $<TARGET_FILE:LibProsperoPkg>
          --cpp-tools-dir $<TARGET_FILE_DIR:prosperopkg-inspect>
          --iterations 250)
    endif()
  endif()
endif()

if(LIBPROSPEROPKG_INSTALL_STATIC)
  install(TARGETS prosperopkg_cpp
    EXPORT LibProsperoPkgTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(TARGET LibProsperoPkg)
  install(TARGETS LibProsperoPkg
    EXPORT LibProsperoPkgTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(TARGET prosperopkg-inspect)
  install(TARGETS prosperopkg-inspect prosperopkg-fself prosperopkg-gp5 prosperopkg-keys
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY src/include/prosperopkg
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "libprosperopkg.h")
