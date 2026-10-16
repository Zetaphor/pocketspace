cmake_minimum_required(VERSION 3.16)
project(sf_mvp CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK_INCLUDE "$ENV{HOME}/.sdk/include" CACHE PATH "OpenVR headers")
set(QJS_INCLUDE "$ENV{HOME}/Code/third_party/quickjs-ng" CACHE PATH "QuickJS dir")
set(QJS_LIBDIR  "$ENV{HOME}/Code/third_party/quickjs-ng/build" CACHE PATH "QuickJS lib")
set(STEAMVR_BIN "/opt/steamvr/bin/linuxarm64" CACHE PATH "SteamVR bin")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${SDK_INCLUDE} ${QJS_INCLUDE})
link_directories(${STEAMVR_BIN} ${QJS_LIBDIR})

find_package(Freetype REQUIRED)
add_executable(sf-mvp src/main.cpp)
target_link_libraries(sf-mvp PRIVATE openvr_api qjs pthread EGL GLESv2 Freetype::Freetype)

set(QUICKJS_HEADER_LIBNAME qjs)
if(NOT EXISTS "${QJS_LIBDIR}/libqjs.a")
  message(FATAL_ERROR "QuickJS static library (libqjs.a) not found in ${QJS_LIBDIR}")
endif()
