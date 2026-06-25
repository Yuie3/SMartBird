cmake_minimum_required(VERSION 3.20)

project(SMartBird LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CROSSCOMPILING)
    option(BUILD_VITA_PLAYER "Build the PS Vita player" ON)
else()
    option(BUILD_VITA_PLAYER "Build the PS Vita player" OFF)
endif()

if(BUILD_VITA_PLAYER)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    find_package(PkgConfig REQUIRED)

    set(APP_TARGET SMartBird)
    set(APP_TITLE "SMartBird")
    set(APP_TITLE_ID "SMARTBIRD")
    set(APP_VERSION "0.0.5")
    set(APP_VITA_VERSION "00.05")
    set(APP_VPK_VERSIONED "${APP_TARGET}_v${APP_VERSION}.vpk")
    set(APP_VPK_LATEST "${APP_TARGET}.vpk")

    pkg_check_modules(MPV REQUIRED mpv)
    pkg_check_modules(SMB2 REQUIRED libsmb2)

    set(nanovg_src ${CMAKE_SOURCE_DIR}/third_party/nanovg-gxm/src)
    set(VITA_SMB_SERVER "" CACHE STRING "Fixed SMB server host or host:port")
    set(VITA_SMB_SHARE "" CACHE STRING "Fixed SMB share name")
    set(VITA_SMB_PATH "" CACHE STRING "Fixed SMB directory path inside the share")
    set(VITA_SMB_USER "guest" CACHE STRING "Fixed SMB username")
    set(VITA_SMB_PASSWORD "" CACHE STRING "Fixed SMB password")
    set(VITA_SMB_DOMAIN "" CACHE STRING "Fixed SMB domain/workgroup")
    configure_file(
        ${CMAKE_SOURCE_DIR}/src/vita/SmbConfig.hpp.in
        ${CMAKE_BINARY_DIR}/generated/SmbConfig.hpp
        @ONLY
    )

    add_executable(${APP_TARGET}
        src/vita/App.cpp
        src/vita/main.cpp
        src/vita/Browser/BrowserActions.cpp
        src/vita/Browser/BrowserState.cpp
        src/vita/Browser/CopyManager.cpp
        src/vita/Browser/Scanner.cpp
        src/vita/Config/HiddenItems.cpp
        src/vita/Config/I18n.cpp
        src/vita/Config/Settings.cpp
        src/vita/Core/State.cpp
        src/vita/Image/ImageViewer.cpp
        src/vita/Network/Runtime.cpp
        src/vita/Player/PlayerMessages.cpp
        src/vita/Player/PlayerHudLayout.cpp
        src/vita/Player/PlayerOverlay.cpp
        src/vita/Player/SmbStream.cpp
        src/vita/Player/VideoPlayer.cpp
        src/vita/UI/ImeInput.cpp
        src/vita/UI/Input.cpp
        src/vita/UI/Screens.cpp
        src/vita/UI/Theme.cpp
        src/vita/UI/Widgets.cpp
        src/vita/Utils/FileTypes.cpp
        src/vita/Utils/Json.cpp
        src/vita/Utils/Math.cpp
        src/vita/Utils/Text.cpp
        ${nanovg_src}/nanovg.c
    )

    target_include_directories(${APP_TARGET} PRIVATE
        src/vita
        ${CMAKE_BINARY_DIR}/generated
        ${nanovg_src}
        ${MPV_INCLUDE_DIRS}
        ${SMB2_INCLUDE_DIRS}
    )

    target_compile_definitions(${APP_TARGET} PRIVATE
        PLATFORM_VITA=1
        __psp2__
        __PSV__
        STBI_NEON
    )

    set_source_files_properties(src/vita/App.cpp PROPERTIES
        COMPILE_DEFINITIONS "NANOVG_GXM_IMPLEMENTATION;NANOVG_GXM_UTILS_IMPLEMENTATION"
    )

    target_compile_options(${APP_TARGET} PRIVATE
        -O2 -g -fno-omit-frame-pointer -ffast-math -mtune=cortex-a9 -mfpu=neon
        ${MPV_CFLAGS_OTHER}
        ${SMB2_CFLAGS_OTHER}
    )

    target_link_directories(${APP_TARGET} PRIVATE
        ${MPV_LIBRARY_DIRS}
        ${SMB2_LIBRARY_DIRS}
    )

    target_link_libraries(${APP_TARGET} PRIVATE
        -Wl,--whole-archive pthread -Wl,--no-whole-archive
        "-Wl,--start-group"
        ${SMB2_STATIC_LIBRARIES}
        ${MPV_STATIC_LIBRARIES}
        m
        SceGxm_stub
        SceDisplay_stub
        SceLibKernel_stub
        SceSysmodule_stub
        SceIofilemgr_stub
        SceNet_stub
        SceNetCtl_stub
        SceRtc_stub
        SceSysmem_stub
        SceKernelThreadMgr_stub
        SceKernelModulemgr_stub
        SceProcessmgr_stub
        SceAppMgr_stub
        SceCommonDialog_stub
        SceIme_stub
        SceCtrl_stub
        SceTouch_stub
        SceMotion_stub
        SceAppUtil_stub
        ScePower_stub
        ScePgf_stub
        freetype
        png
        jpeg
        z
        "-Wl,--end-group"
    )

    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")

    vita_create_self(${APP_TARGET}.self ${APP_TARGET} UNSAFE NOASLR)
    vita_create_vpk(${APP_VPK_VERSIONED} ${APP_TITLE_ID} ${APP_TARGET}.self
        VERSION ${APP_VITA_VERSION}
        NAME ${APP_TITLE}
        FILE ${CMAKE_SOURCE_DIR}/third_party/nanovg-gxm/example/Roboto-Regular.ttf Roboto-Regular.ttf
        FILE ${CMAKE_SOURCE_DIR}/assets/fonts/CJK.ttf CJK.ttf
        FILE ${CMAKE_SOURCE_DIR}/assets/i18n/zh-Hant/vita_smb_player.json i18n/zh-Hant/vita_smb_player.json
        FILE ${CMAKE_SOURCE_DIR}/assets/i18n/en-US/vita_smb_player.json i18n/en-US/vita_smb_player.json
    )

    add_custom_command(TARGET ${APP_VPK_VERSIONED}-vpk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/releases
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${APP_VPK_VERSIONED}
            ${CMAKE_SOURCE_DIR}/releases/${APP_VPK_VERSIONED}
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${APP_VPK_VERSIONED}
            ${CMAKE_SOURCE_DIR}/releases/${APP_VPK_LATEST}
        COMMENT "Copying VPK to releases/"
    )
endif()
