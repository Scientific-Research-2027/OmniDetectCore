cmake_minimum_required(VERSION 3.24)

# Allow find_package() to honor upper-case <PACKAGENAME>_ROOT variables
# such as ONNXRUNTIME_ROOT on newer CMake versions.
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

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

  # First try a normal CMake package installation.
  find_package(onnxruntime CONFIG QUIET)

  if(TARGET onnxruntime::onnxruntime)

    set(OMNIDETECT_ONNX_TARGET onnxruntime::onnxruntime)

  elseif(TARGET onnxruntime)

    set(OMNIDETECT_ONNX_TARGET onnxruntime)

  elseif(ONNXRUNTIME_ROOT)

    message(STATUS "Using ONNX Runtime binary distribution from: ${ONNXRUNTIME_ROOT}")

    find_path(
      ONNXRUNTIME_INCLUDE_DIR
      NAMES onnxruntime_cxx_api.h
      PATHS "${ONNXRUNTIME_ROOT}/include"
      NO_DEFAULT_PATH
    )

    find_library(
      ONNXRUNTIME_LIBRARY
      NAMES onnxruntime
      PATHS "${ONNXRUNTIME_ROOT}/lib"
      NO_DEFAULT_PATH
    )

    if(WIN32)
      find_file(
        ONNXRUNTIME_DLL
        NAMES onnxruntime.dll
        PATHS
        "${ONNXRUNTIME_ROOT}/lib"
        "${ONNXRUNTIME_ROOT}/bin"
        NO_DEFAULT_PATH)   
    endif()

    if(NOT ONNXRUNTIME_INCLUDE_DIR)
      message(FATAL_ERROR
        "ONNX Runtime headers were not found under "
        "${ONNXRUNTIME_ROOT}/include")
    endif()

    if(NOT ONNXRUNTIME_LIBRARY)
      message(FATAL_ERROR
        "onnxruntime.lib was not found under "
        "${ONNXRUNTIME_ROOT}/lib")
    endif()

    if(WIN32 AND NOT ONNXRUNTIME_DLL)
      message(FATAL_ERROR
        "onnxruntime.dll was not found under "
        "${ONNXRUNTIME_ROOT}/lib or ${ONNXRUNTIME_ROOT}/bin")
    endif()

    if(WIN32)
      add_library(onnxruntime::onnxruntime SHARED IMPORTED)

      set_target_properties(
        onnxruntime::onnxruntime
        PROPERTIES
          IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
          IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
          INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
      )

      # Optional ONNX Runtime provider DLLs distributed with the Windows package.
      file(GLOB ONNXRUNTIME_PROVIDER_DLLS
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers*.dll"
        "${ONNXRUNTIME_ROOT}/bin/onnxruntime_providers*.dll"
      )
    else()
      add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)

      set_target_properties(
        onnxruntime::onnxruntime
        PROPERTIES
          IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
      )
    endif()

    set(OMNIDETECT_ONNX_TARGET onnxruntime::onnxruntime)

  else()

    message(FATAL_ERROR
      "ONNX Runtime was not found. "
      "Set onnxruntime_DIR to a CMake package or "
      "ONNXRUNTIME_ROOT to an extracted Windows binary distribution.")

  endif()

endif()

# Copy ONNX Runtime beside Windows executables so Windows loads the same
# runtime version that the project was compiled and linked against.
function(omnidetect_copy_onnx_runtime target_name)
  if(NOT WIN32 OR NOT OMNIDETECT_ENABLE_ONNX)
    return()
  endif()

  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "Cannot deploy ONNX Runtime: target '${target_name}' does not exist.")
  endif()

  if(NOT OMNIDETECT_ONNX_TARGET OR NOT TARGET "${OMNIDETECT_ONNX_TARGET}")
    message(FATAL_ERROR "Cannot deploy ONNX Runtime: inference target is not available.")
  endif()

  add_custom_command(
    TARGET "${target_name}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:${OMNIDETECT_ONNX_TARGET}>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying ONNX Runtime DLL for ${target_name}"
    VERBATIM
  )

  foreach(provider_dll IN LISTS ONNXRUNTIME_PROVIDER_DLLS)
    add_custom_command(
      TARGET "${target_name}"
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${provider_dll}"
        "$<TARGET_FILE_DIR:${target_name}>"
      COMMENT "Copying ONNX Runtime provider DLL for ${target_name}: ${provider_dll}"
      VERBATIM
    )
  endforeach()
endfunction()

# Copy OpenCV runtime DLLs beside Windows executables that use the input/output
# adapters. This keeps clean builds runnable without editing PATH manually.
function(omnidetect_copy_opencv_runtime target_name)
  if(NOT WIN32 OR NOT OpenCV_FOUND)
    return()
  endif()

  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "Cannot deploy OpenCV runtime: target '${target_name}' does not exist.")
  endif()

  # Official OpenCV Windows packages usually place runtime DLLs under:
  #   <OpenCV_DIR>/x64/vcXX/bin
  # Copy all release runtime DLLs (world + videoio/ffmpeg helpers, if present).
  file(GLOB_RECURSE OMNIDETECT_OPENCV_RUNTIME_DLLS
    "${OpenCV_DIR}/x64/*/bin/*.dll"
  )

  if(NOT OMNIDETECT_OPENCV_RUNTIME_DLLS)
    message(WARNING
      "OpenCV was found, but no runtime DLLs were found below '${OpenCV_DIR}/x64/*/bin'. "
      "The target '${target_name}' may require OpenCV DLLs on PATH.")
    return()
  endif()

  foreach(opencv_dll IN LISTS OMNIDETECT_OPENCV_RUNTIME_DLLS)
    get_filename_component(opencv_dll_name "${opencv_dll}" NAME)

    # Skip common Debug DLLs such as opencv_world4140d.dll for Release builds.
    if(opencv_dll_name MATCHES "d\\.dll$")
      continue()
    endif()

    add_custom_command(
      TARGET "${target_name}"
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${opencv_dll}"
        "$<TARGET_FILE_DIR:${target_name}>"
      COMMENT "Copying OpenCV runtime for ${target_name}: ${opencv_dll_name}"
      VERBATIM
    )
  endforeach()
endfunction()

# Deploy Qt runtime and plugins automatically for the Windows desktop app.
function(omnidetect_deploy_qt target_name)
  if(NOT WIN32)
    return()
  endif()

  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "Cannot deploy Qt runtime: target '${target_name}' does not exist.")
  endif()

  if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Cannot deploy Qt runtime: Qt 6 was not found.")
  endif()

  # Qt6_DIR normally points to:
  #   <QtRoot>/lib/cmake/Qt6
  # therefore ../../../bin contains windeployqt.exe.
  find_program(
    OMNIDETECT_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt.exe
    HINTS "${Qt6_DIR}/../../../bin"
    NO_DEFAULT_PATH
  )

  if(NOT OMNIDETECT_WINDEPLOYQT_EXECUTABLE)
    find_program(
      OMNIDETECT_WINDEPLOYQT_EXECUTABLE
      NAMES windeployqt windeployqt.exe
    )
  endif()

  if(NOT OMNIDETECT_WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR
      "Qt 6 was found, but windeployqt was not found. "
      "Set Qt6_DIR to the MSVC Qt package, for example "
      "C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6.")
  endif()

  add_custom_command(
    TARGET "${target_name}"
    POST_BUILD
    COMMAND "${OMNIDETECT_WINDEPLOYQT_EXECUTABLE}"
      --release
      --no-translations
      --no-compiler-runtime
      "$<TARGET_FILE:${target_name}>"
    COMMENT "Deploying Qt runtime for ${target_name}"
    VERBATIM
  )
endfunction()

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
  omnidetect_copy_onnx_runtime(omnidetect_edge)
  omnidetect_copy_opencv_runtime(omnidetect_edge)
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
    omnidetect_copy_onnx_runtime(omnidetect_desktop)
    omnidetect_copy_opencv_runtime(omnidetect_desktop)
    omnidetect_deploy_qt(omnidetect_desktop)
  else()
    message(STATUS "Qt 6 Widgets not found: desktop target disabled. Set Qt6_DIR and reconfigure to enable it.")
  endif()
endif()

if(OMNIDETECT_BUILD_BENCHMARK)
  add_executable(omnidetect_benchmark tools/benchmark/main.cpp)
  target_link_libraries(omnidetect_benchmark PRIVATE omnidetect_core)
  omnidetect_set_warnings(omnidetect_benchmark)
  omnidetect_copy_onnx_runtime(omnidetect_benchmark)
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
  omnidetect_copy_onnx_runtime(omnidetect_tests)
  omnidetect_copy_opencv_runtime(omnidetect_tests)
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
