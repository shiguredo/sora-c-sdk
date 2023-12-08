cmake_minimum_required(VERSION 3.23)

cmake_policy(SET CMP0074 NEW)

project(sorac C CXX)

add_subdirectory(_source/libdatachannel libdatachannel EXCLUDE_FROM_ALL)
add_subdirectory(_source/libdatachannel/deps/json json EXCLUDE_FROM_ALL)
add_subdirectory(_source/opus opus EXCLUDE_FROM_ALL)
# set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
# add_subdirectory(_source/libdatachannel/deps/plog plog EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# 指定したライブラリを自身の静的ライブラリにバンドルする
function(bundle_static_library target static_libs bundled_target)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bundled)
  set(bundled_tgt_full_name
    ${CMAKE_BINARY_DIR}/bundled/${CMAKE_STATIC_LIBRARY_PREFIX}${target}${CMAKE_STATIC_LIBRARY_SUFFIX})

  if (MSVC)
    add_custom_command(
      COMMAND ${CMAKE_AR} /NOLOGO /OUT:${bundled_tgt_full_name} $<TARGET_FILE:${target}> ${static_libs}
      OUTPUT ${bundled_tgt_full_name}
      VERBATIM
      COMMENT "Bundling libs: ${static_libs} to $<TARGET_FILE:${target}>")
  elseif (APPLE)
    add_custom_command(
      COMMAND libtool -static -o ${bundled_tgt_full_name} $<TARGET_FILE:${target}> ${static_libs}
      OUTPUT ${bundled_tgt_full_name}
      VERBATIM
      COMMENT "Bundling libs: ${static_libs} to $<TARGET_FILE:${target}>")
  else ()
    file(WRITE  ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "CREATE ${bundled_tgt_full_name}\n" )
    file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "ADDLIB $<TARGET_FILE:${target}>\n")
    foreach(lib IN LISTS static_libs)
      file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "ADDLIB ${lib}\n")
    endforeach()
    file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "SAVE\n")
    file(APPEND ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in "END\n")
    file(GENERATE
      OUTPUT ${CMAKE_BINARY_DIR}/${bundled_target}.ar
      INPUT ${CMAKE_BINARY_DIR}/${bundled_target}.ar.in)

    add_custom_command(
      COMMAND ${CMAKE_AR} -M < ${CMAKE_BINARY_DIR}/${bundled_target}.ar
      OUTPUT ${bundled_tgt_full_name}
      VERBATIM
      COMMENT "Bundling libs: ${static_libs} to $<TARGET_FILE:${target}>")
  endif()

  add_custom_target(${bundled_target}_bundling ALL DEPENDS ${bundled_tgt_full_name})
  add_dependencies(${bundled_target}_bundling ${target})

  add_library(${bundled_target} STATIC IMPORTED)
  set_target_properties(${bundled_target}
    PROPERTIES
      IMPORTED_LOCATION ${bundled_tgt_full_name}
      INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${target},INTERFACE_INCLUDE_DIRECTORIES>)
  add_dependencies(${bundled_target} ${bundled_target}_bundling)
endfunction()

add_library(sorac STATIC)

configure_file(src/version.gen.h.template ${CMAKE_CURRENT_BINARY_DIR}/version.gen.h)
target_include_directories(sorac PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac")
if (WIN32)
  set(EXE_EXT ".exe")
endif()
add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.h"
    "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.c.h"
    "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.c.hpp"
    "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.c.cpp"
  COMMAND
    "${PROTOBUF_DIR}/bin/protoc${EXE_EXT}"
  ARGS
    --jsonif-cpp_out "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/"
    --jsonif-c_out "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/"
    --plugin=protoc-gen-jsonif-cpp="${PROTOC_GEN_JSONIF_DIR}/bin/protoc-gen-jsonif-cpp${EXE_EXT}"
    --plugin=protoc-gen-jsonif-c="${PROTOC_GEN_JSONIF_DIR}/bin/protoc-gen-jsonif-c${EXE_EXT}"
    -I "${CMAKE_CURRENT_SOURCE_DIR}/proto/"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/soracp.proto"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/soracp.proto"
)
target_sources(sorac
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.c.cpp"
    src/current_time.cpp
    src/data_channel.cpp
    src/open_h264_video_encoder.cpp
    src/opus_audio_encoder.cpp
    src/signaling.cpp
    src/simulcast_encoder_adapter.cpp
    src/simulcast_media_handler.cpp
    src/sorac.cpp
    src/types.cpp
    src/util.cpp
    src/version.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      "${CMAKE_CURRENT_BINARY_DIR}/proto/"
      "include"
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.h"
      "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.c.h"
      "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac/soracp.json.c.hpp"
      include/sorac/bitrate.hpp
      include/sorac/current_time.hpp
      include/sorac/data_channel.hpp
      include/sorac/open_h264_video_encoder.hpp
      include/sorac/opus_audio_encoder.hpp
      include/sorac/signaling.hpp
      include/sorac/simulcast_encoder_adapter.hpp
      include/sorac/simulcast_media_handler.hpp
      include/sorac/sorac.h
      include/sorac/types.hpp
      include/sorac/version.hpp
      include/sorac/video_encoder.hpp
)
target_include_directories(sorac PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(sorac PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/proto/sorac")
target_compile_definitions(sorac PRIVATE JSONIF_USE_NLOHMANN_JSON)

# OpenH264
target_include_directories(sorac PRIVATE ${OPENH264_ROOT_DIR}/include)

set_target_properties(sorac PROPERTIES CXX_STANDARD 20 C_STANDARD 20)

set(_LIBS
  $<TARGET_FILE:LibDataChannel::LibDataChannelStatic>
  ${MbedTLS_LIBRARY}
  ${MbedCrypto_LIBRARY}
  ${MbedX509_LIBRARY}
  $<TARGET_FILE:Opus::opus>
  $<TARGET_FILE:srtp2>
  $<TARGET_FILE:LibJuice::LibJuiceStatic>
  $<TARGET_FILE:Usrsctp::Usrsctp>
)
bundle_static_library(sorac "${_LIBS}" bundled_sorac)

target_link_libraries(sorac
  PRIVATE
    "$<BUILD_INTERFACE:LibDataChannel::LibDataChannelStatic>"
    "$<BUILD_INTERFACE:Opus::opus>"
    "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>"
    "$<BUILD_INTERFACE:plog::plog>"
)
target_link_libraries(sorac
  PUBLIC
    dl
    Threads::Threads
    ZLIB::ZLIB
)

if (SORAC_TARGET STREQUAL "macos_arm64")
  enable_language(OBJCXX)
  target_link_options(sorac PRIVATE -ObjC)
  target_sources(sorac
    PRIVATE
      src/vt_h26x_video_encoder.cpp
      src/mac_version.mm
    PUBLIC
      FILE_SET HEADERS
      BASE_DIRS
        "include"
      FILES
        "include/sorac/vt_h26x_video_encoder.hpp"
  )
  target_link_libraries(sorac
    PUBLIC
      "-framework VideoToolbox"
  )
endif()

add_library(sorac::sorac ALIAS bundled_sorac)

# インストール

include(GNUInstallDirs)

install(TARGETS sorac
  EXPORT sorac-config
  FILE_SET HEADERS
)
install(EXPORT sorac-config
  DESTINATION ${CMAKE_INSTALL_DATADIR}/sorac
  NAMESPACE Sorac::
)
