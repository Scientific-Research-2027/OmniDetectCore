cmake_minimum_required(VERSION 3.24)

project(OmniDetectCore VERSION 0.1.0 LANGUAGES CXX)

option(OMNIDETECT_BUILD_DESKTOP "Build the Qt 6 desktop application" ON)
option(OMNIDETECT_BUILD_EDGE "Build the headless edge application" ON)
option(OMNIDETECT_BUILD_TESTS "Build unit tests" ON)
option(OMNIDETECT_BUILD_BENCHMARK "Build the portable benchmark" ON)
option(OMNIDETECT_ENABLE_NCNN "Enable the NCNN inference backend" OFF)
option(OMNIDETECT_ENABLE_ONNX "Enable the ONNX Runtime inference backend" OFF)
option(OMNIDETECT_ENABLE_MQTT "Enable the MQTT output adapter" OFF)
option(OMNIDETECT_ENABLE_TRACKING "Enable the built-in IoU tracker" ON)
option(OMNIDETECT_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectWarnings)

find_package(Threads REQUIRED)
find_package(OpenCV QUIET COMPONENTS core imgcodecs imgproc videoio)
find_package(spdlog QUIET)

if(OMNIDETECT_ENABLE_NCNN)
  find_package(ncnn CONFIG QUIET)
  if(NOT ncnn_FOUND)
    message(FATAL_ERROR "OMNIDETECT_ENABLE_NCNN=ON, but NCNN was not found. Set ncnn_DIR or install the ncnn CMake package.")
  endif()
  if(TARGET ncnn::ncnn)
    set(OMNIDETECT_NCNN_TARGET ncnn::ncnn)
  elseif(TARGET ncnn)
    set(OMNIDETECT_NCNN_TARGET ncnn)
  else()
    message(FATAL_ERROR "The NCNN package was found but exports neither ncnn::ncnn nor ncnn target.")
  endif()
endif()

if(OMNIDETECT_ENABLE_ONNX)
  find_package(onnxruntime CONFIG QUIET)
  if(NOT onnxruntime_FOUND)
    message(FATAL_ERROR "OMNIDETECT_ENABLE_ONNX=ON, but ONNX Runtime was not found. Set onnxruntime_DIR to its CMake package.")
  endif()
  if(TARGET onnxruntime::onnxruntime)
    set(OMNIDETECT_ONNX_TARGET onnxruntime::onnxruntime)
  elseif(TARGET onnxruntime)
    set(OMNIDETECT_ONNX_TARGET onnxruntime)
  else()
    message(FATAL_ERROR "The ONNX Runtime package was found but exports neither onnxruntime::onnxruntime nor onnxruntime target.")
  endif()
endif()

add_library(omnidetect_core
  core/logging/Logger.cpp
  core/preprocessing/Preprocessor.cpp
  core/inference/BackendFactory.cpp
  core/inference/NcnnBackend.cpp
  core/inference/OnnxBackend.cpp
  core/yolo/Yolo26Detector.cpp
  core/tracking/ObjectTracker.cpp
  core/monitoring/ObjectMonitor.cpp
  core/monitoring/KiteMonitor.cpp
  core/monitoring/EventEngine.cpp
  config/ConfigManager.cpp
)
target_include_directories(omnidetect_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(omnidetect_core PUBLIC Threads::Threads)
omnidetect_set_warnings(omnidetect_core)

if(spdlog_FOUND)
  target_link_libraries(omnidetect_core PRIVATE spdlog::spdlog)
  target_compile_definitions(omnidetect_core PRIVATE OMNIDETECT_HAS_SPDLOG=1)
endif()
if(OMNIDETECT_ENABLE_NCNN)
  target_link_libraries(omnidetect_core PRIVATE ${OMNIDETECT_NCNN_TARGET})
  target_compile_definitions(omnidetect_core PRIVATE OMNIDETECT_HAS_NCNN=1)
endif()
if(OMNIDETECT_ENABLE_ONNX)
  target_link_libraries(omnidetect_core PRIVATE ${OMNIDETECT_ONNX_TARGET})
  target_compile_definitions(omnidetect_core PRIVATE OMNIDETECT_HAS_ONNX=1)
endif()
if(OMNIDETECT_ENABLE_TRACKING)
  target_compile_definitions(omnidetect_core PUBLIC OMNIDETECT_ENABLE_TRACKING=1)
endif()

add_library(omnidetect_input
  input/ImageSource.cpp
  input/WebcamSource.cpp
  input/VideoSource.cpp
  input/CameraIdSource.cpp
  input/SourceFactory.cpp
)
target_include_directories(omnidetect_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(omnidetect_input PUBLIC omnidetect_core)
omnidetect_set_warnings(omnidetect_input)

add_library(omnidetect_output
  output/ImageSink.cpp
  output/RecorderSink.cpp
  output/MqttSink.cpp
)
target_include_directories(omnidetect_output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(omnidetect_output PUBLIC omnidetect_core)
omnidetect_set_warnings(omnidetect_output)

if(OpenCV_FOUND)
  target_link_libraries(omnidetect_input PRIVATE ${OpenCV_LIBS})
  target_include_directories(omnidetect_input PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_compile_definitions(omnidetect_input PRIVATE OMNIDETECT_HAS_OPENCV=1)
  target_link_libraries(omnidetect_output PRIVATE ${OpenCV_LIBS})
  target_include_directories(omnidetect_output PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_compile_definitions(omnidetect_output PRIVATE OMNIDETECT_HAS_OPENCV=1)
else()
  message(STATUS "OpenCV not found: image/webcam/video adapters will report a clear runtime error; portable core remains available.")
endif()

add_library(omnidetect_application
  application/DetectionPipeline.cpp
  application/AppController.cpp
)
target_include_directories(omnidetect_application PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(omnidetect_application PUBLIC omnidetect_core omnidetect_input omnidetect_output)
omnidetect_set_warnings(omnidetect_application)

if(OMNIDETECT_BUILD_EDGE)
  add_executable(omnidetect_edge apps/edge/main.cpp)
  target_link_libraries(omnidetect_edge PRIVATE omnidetect_application)
  omnidetect_set_warnings(omnidetect_edge)
endif()

if(OMNIDETECT_BUILD_DESKTOP)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(omnidetect_desktop
      apps/desktop/main.cpp
      apps/desktop/MainWindow.cpp
      apps/desktop/MainWindow.h
      output/QtWindowSink.cpp
      output/QtWindowSink.h
    )
    target_link_libraries(omnidetect_desktop PRIVATE omnidetect_application Qt6::Widgets)
    omnidetect_set_warnings(omnidetect_desktop)
  else()
    message(STATUS "Qt 6 Widgets not found: desktop target disabled. Set Qt6_DIR and reconfigure to enable it.")
  endif()
endif()

if(OMNIDETECT_BUILD_BENCHMARK)
  add_executable(omnidetect_benchmark tools/benchmark/main.cpp)
  target_link_libraries(omnidetect_benchmark PRIVATE omnidetect_core)
  omnidetect_set_warnings(omnidetect_benchmark)
endif()

if(OMNIDETECT_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  add_executable(omnidetect_tests
    tests/test_main.cpp
    tests/CoreTests.cpp
    tests/PipelineTests.cpp
  )
  target_link_libraries(omnidetect_tests PRIVATE omnidetect_application)
  if(GTest_FOUND)
    target_link_libraries(omnidetect_tests PRIVATE GTest::gtest)
    target_compile_definitions(omnidetect_tests PRIVATE OMNIDETECT_USE_GTEST=1)
    message(STATUS "GoogleTest found: tests use the GoogleTest runner")
  else()
    message(STATUS "GoogleTest not found: tests use the dependency-free compatibility runner")
  endif()
  omnidetect_set_warnings(omnidetect_tests)
  add_test(NAME omnidetect_tests COMMAND omnidetect_tests)
endif()

include(GNUInstallDirs)
install(TARGETS omnidetect_core omnidetect_input omnidetect_output omnidetect_application
  EXPORT OmniDetectCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET omnidetect_edge)
  install(TARGETS omnidetect_edge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
