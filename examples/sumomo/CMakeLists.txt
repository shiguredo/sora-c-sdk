cmake_minimum_required(VERSION 3.23)

project(sumomo C CXX)

list(APPEND CMAKE_PREFIX_PATH ${SORAC_DIR})
list(APPEND CMAKE_MODULE_PATH ${SORAC_DIR}/share/cmake)
list(APPEND CMAKE_PREFIX_PATH ${LIBJPEG_TURBO_DIR})

find_package(Sorac REQUIRED)
find_package(Threads REQUIRED)
find_package(libjpeg-turbo REQUIRED)

add_executable(sumomo)
target_sources(sumomo
  PRIVATE
    capturer.cpp
    fake_capturer.cpp
    fake_recorder.cpp
    steady_frame_thread.cpp
    sumomo.c
    option.c
    recorder.cpp
)

if (SUMOMO_TARGET STREQUAL "ubuntu-20.04_x86_64" OR SUMOMO_TARGET STREQUAL "ubuntu-22.04_x86_64")
  target_sources(sumomo
    PRIVATE
      pulse_recorder.cpp
      v4l2_capturer.cpp
  )
endif()

set_target_properties(sumomo PROPERTIES CXX_STANDARD 20 C_STANDARD 17)

target_link_libraries(sumomo PRIVATE Sorac::sorac)

# FindLibYUV.cmake を書くのが面倒なので直接インクルードとリンクする
target_include_directories(sumomo PRIVATE ${LIBYUV_DIR}/include)
target_link_libraries(sumomo PRIVATE ${LIBYUV_DIR}/lib/libyuv.a libjpeg-turbo::jpeg-static)
