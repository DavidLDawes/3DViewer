# csbridge - revo-bridge, a JSON-lines helper process over the 3DCamera SDK
# (fork addition, for mhs2revo). See README.md in this directory.

# the protocol, property table and capture writer, without the SDK library,
# so tests can drive them with a fake camera
set(CORE_TARGET csbridgecore)
add_library(${CORE_TARGET} STATIC
    bridgeserver.cpp
    propertymap.cpp
    capturewriter.cpp
    include/bridgeserver.h
    include/camerabackend.h
    include/capturewriter.h
    include/propertymap.h
)
target_include_directories(${CORE_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CAMERA_SDK_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../csutil/include)
target_link_libraries(${CORE_TARGET} PUBLIC Qt5::Core Qt5::Gui csutil)
set_target_properties(${CORE_TARGET} PROPERTIES FOLDER libs AUTOMOC OFF)

# the executable, linked against the real SDK
set(TARGET_NAME revo-bridge)
add_executable(${TARGET_NAME} main.cpp sdkbackend.cpp include/sdkbackend.h)
set_target_properties(${TARGET_NAME} PROPERTIES AUTOMOC OFF)

if(MSVC)
    set(3DCAMERA_LIBRARY
        optimized ${CAMERA_SDK_LIB}/3DCamera.lib
        debug ${CAMERA_SDK_LIB}/3DCamerad.lib)
else()
    find_library(CAMERA_SDK_LIBRARY 3DCamera PATHS ${CAMERA_SDK_LIB} NO_DEFAULT_PATH REQUIRED)
    set(3DCAMERA_LIBRARY ${CAMERA_SDK_LIBRARY})
endif()
target_link_libraries(${TARGET_NAME} PRIVATE ${CORE_TARGET} ${3DCAMERA_LIBRARY})

if(WIN32)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BIN_DIR}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BIN_DIR})

    # put the SDK and libpng DLLs next to the exe; Qt comes from PATH or windeployqt
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CAMERA_SDK_LIB}/$<IF:$<CONFIG:Debug>,3DCamerad.dll,3DCamera.dll>"
            "${LIBPNG_CPP_BIN}/$<IF:$<CONFIG:Debug>,libpng16d.dll,libpng16.dll>"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>")
else()
    set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
endif()
