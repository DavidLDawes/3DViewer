# tests - hardware-free unit and smoke tests (enabled with -DBUILD_TESTS=ON)
#
# None of these need a camera. They cover the pieces an automation layer
# (e.g. mhs2revo) depends on: depth -> point cloud math and PLY export from the
# SDK headers, 16-bit depth PNG round trip in csutil, and that the prebuilt
# 3DCamera SDK loads and enumerates cleanly with nothing attached.

find_package(Qt5 COMPONENTS Test REQUIRED)

set(TEST_BIN_DIR ${CMAKE_BINARY_DIR}/tests)

# runtime search path for the prebuilt thirdparty libraries and our own dlls
if(WIN32)
    get_target_property(_qmake Qt5::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR ${_qmake} DIRECTORY)
    set(TEST_RUNTIME_PATH
        "${BIN_DIR}/$<CONFIG>"
        "${BIN_DIR}"
        "${QT_BIN_DIR}"
        "${CAMERA_SDK_LIB}"
        "${LIBPNG_CPP_BIN}")
    string(REPLACE ";" "\\;" TEST_RUNTIME_PATH "${TEST_RUNTIME_PATH};$ENV{PATH}")
    set(TEST_ENV "PATH=${TEST_RUNTIME_PATH}")
else()
    set(TEST_ENV "LD_LIBRARY_PATH=${LIB_DIR}:${CAMERA_SDK_LIB}:${LIBPNG_CPP_LIB}:$ENV{LD_LIBRARY_PATH}")
endif()

function(cs_add_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_include_directories(${NAME} PRIVATE ${CAMERA_SDK_INC} ../csutil/include)
    target_link_libraries(${NAME} PRIVATE Qt5::Core Qt5::Test ${ARGN})
    set_target_properties(${NAME} PROPERTIES
        FOLDER tests
        RUNTIME_OUTPUT_DIRECTORY ${TEST_BIN_DIR})
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES
        ENVIRONMENT "${TEST_ENV};QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60)
endfunction()

# header-only SDK code (Processing.hpp): no library needed
cs_add_test(test_pointcloud)

# csutil: 16-bit depth PNG save/load
cs_add_test(test_imageutil csutil)

# prebuilt 3DCamera SDK: loads, reports a version, enumerates with no camera
if(MSVC)
    cs_add_test(test_sdk_smoke
        optimized ${CAMERA_SDK_LIB}/3DCamera.lib
        debug ${CAMERA_SDK_LIB}/3DCamerad.lib)
else()
    find_library(CAMERA_SDK_LIBRARY 3DCamera PATHS ${CAMERA_SDK_LIB} NO_DEFAULT_PATH REQUIRED)
    cs_add_test(test_sdk_smoke ${CAMERA_SDK_LIBRARY})
endif()
