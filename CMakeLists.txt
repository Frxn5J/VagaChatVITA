cmake_minimum_required(VERSION 3.16)

# VitaSDK supplies the cross-compiler and the Vita-specific CMake helpers.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE
      "$ENV{VITASDK}/share/vita.toolchain.cmake"
      CACHE FILEPATH "VitaSDK toolchain file"
    )
  else()
    message(FATAL_ERROR "Please define VITASDK before configuring the project")
  endif()
endif()

if(DEFINED ENV{VITASDK})
  set(VITASDK "$ENV{VITASDK}")
else()
  message(FATAL_ERROR "Please define VITASDK before configuring the project")
endif()

project(VagaChatVITA LANGUAGES C)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VagaRoute AI")
# Vita title IDs are exactly nine characters and should be unique per app.
set(VITA_TITLEID "VAGROUTE1")
set(VITA_VERSION "01.00")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_executable(${PROJECT_NAME}
  src/main.c
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall
  -Wextra
)

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  SceTouch_stub
  SceIme_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceIofilemgr_stub
  freetype
  bz2
  png16
  jpeg
  z
  m
)

# Build the executable formats used by VitaShell and the Vita home screen.
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/vagaroute-logo.svg assets/vagaroute-logo.svg
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/vagaroute-logo.png assets/vagaroute-logo.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/Manrope.ttf assets/Manrope.ttf
)
