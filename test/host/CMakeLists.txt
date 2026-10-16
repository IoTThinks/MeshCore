# Host tests for the RadioLib wrappers, radio drivers and radio preferences.
#   cmake -S test/host -B .pio/host -DRADIOLIB_DIR=<RadioLib 7.8.1 source>
#   cmake --build .pio/host && ctest --test-dir .pio/host
cmake_minimum_required(VERSION 3.16)
project(meshcore_host_tests CXX)
if(NOT RADIOLIB_DIR)
  message(FATAL_ERROR "Set RADIOLIB_DIR to a RadioLib 7.8.1 source tree, e.g. .pio/libdeps/<env>/RadioLib")
endif()
add_compile_definitions(RADIOLIB_GODMODE=1 RADIOLIB_STATIC_ONLY=1)
add_subdirectory(${RADIOLIB_DIR} radiolib)
enable_testing()

# Keep assertions active even when a caller selects Release.
add_executable(wrapper_lifecycle wrapper_lifecycle.cpp ../../src/helpers/radiolib/RadioLibWrappers.cpp ../../src/helpers/radiolib/RXPowerSaving.cpp)
target_compile_features(wrapper_lifecycle PRIVATE cxx_std_17)
target_compile_options(wrapper_lifecycle PRIVATE -UNDEBUG -include ${CMAKE_CURRENT_SOURCE_DIR}/platform_stub.h)
target_include_directories(wrapper_lifecycle PRIVATE ../../src ../../test/mocks)
target_link_libraries(wrapper_lifecycle RadioLib)
add_test(NAME wrapper_lifecycle COMMAND wrapper_lifecycle)

add_executable(wrapper_lifecycle_lr2021 wrapper_lifecycle.cpp ../../src/helpers/radiolib/RadioLibWrappers.cpp ../../src/helpers/radiolib/RXPowerSaving.cpp)
target_compile_definitions(wrapper_lifecycle_lr2021 PRIVATE USE_LR2021=1)
target_compile_features(wrapper_lifecycle_lr2021 PRIVATE cxx_std_17)
target_compile_options(wrapper_lifecycle_lr2021 PRIVATE -UNDEBUG -include ${CMAKE_CURRENT_SOURCE_DIR}/platform_stub.h)
target_include_directories(wrapper_lifecycle_lr2021 PRIVATE ../../src ../../test/mocks)
target_link_libraries(wrapper_lifecycle_lr2021 RadioLib)
add_test(NAME wrapper_lifecycle_lr2021 COMMAND wrapper_lifecycle_lr2021)

add_executable(driver_spi driver_spi.cpp ../../src/helpers/radiolib/RXPowerSaving.cpp)
target_compile_features(driver_spi PRIVATE cxx_std_17)
target_compile_options(driver_spi PRIVATE -UNDEBUG -include ${CMAKE_CURRENT_SOURCE_DIR}/platform_stub.h)
target_include_directories(driver_spi PRIVATE ../../test/mocks ../../src ../../src/helpers/radiolib)
target_link_libraries(driver_spi RadioLib)
add_test(NAME driver_spi COMMAND driver_spi)

# Real NodePrefs (repeater/room/sensor and companion) with CommonRadioPrefs; needs GoogleTest.
find_package(GTest REQUIRED)
add_executable(config_lifecycle config_lifecycle.cpp
  ../../src/helpers/CommonRadioPrefs.cpp ../../src/helpers/ConfigSerializer.cpp
  ../../src/helpers/DynamicConfigSerializer.cpp ../../src/helpers/TxtDataHelpers.cpp
  ../../src/Utils.cpp ../../src/Packet.cpp
  ../../src/helpers/radiolib/RXPowerSaving.cpp ../../src/helpers/radiolib/RXPowerSavingCLI.cpp)
target_compile_features(config_lifecycle PRIVATE cxx_std_17)
target_include_directories(config_lifecycle PRIVATE ../../src config_target ../../test/mocks)
target_link_libraries(config_lifecycle GTest::gtest)  # the suite has its own main()
add_test(NAME config_lifecycle COMMAND config_lifecycle)
