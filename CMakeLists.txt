# $Id$

if(LFL_IOS OR LFL_OSX)
  set(RESOURECE_FILES)
  set(CRASHREPORTING_LIB app_fabric_crashreporting)
elseif(LFL_WINDOWS)
  set(RESOURECE_FILES LTerminal-windows/term.rc)
  set(CRASHREPORTING_LIB app_null_crashreporting)
else()
  set(RESOURECE_FILES)
  set(CRASHREPORTING_LIB app_null_crashreporting)
endif()

if(LFL_MOBILE)
  set(BILLING_LIB ${LFL_APP_BILLING})
  set(ADVERTISING_LIB ${LFL_APP_ADVERTISING})
else()
  set(BILLING_LIB app_null_billing)
  set(ADVERTISING_LIB app_null_advertising)
endif()

if(LFL_FLATBUFFERS)
  FLATBUFFERS_GENERATE_C_HEADERS(FLATBUFFERS_SRC term.fbs)
endif()

if(LFL_OSX OR LFL_LINUX)
  set(PTY_DEF -DLFL_PTY)
endif()

lfl_set_os_toolkit(TERMINAL)
set(TERMINAL_DB_LIB app_sqlcipher_sqlite)
set(TERMINAL_CRYPTO_DEF -DLFL_CRYPTO)
set(TERMINAL_CRYPTO_LIB app_net_ssh app_net_rfb
    ${LFL_APP_CRYPTO} app_openssl_pk app_openssl_dh app_ref10_25519 app_bcrypt_pbkdf)

if(LFL_MOBILE OR LFL_WINDOWS)
  set(TERMINAL_MENUS 1)
  set(MENUS_SRC term_menu.h)
  set(MENUS_DEF -DLFL_TERMINAL_MENUS)
  set(DB_LIB ${TERMINAL_DB_LIB})
else()
  set(MENUS_SRC)
  set(MENUS_DEF)
  set(DB_LIB app_null_sqlite)
endif()

if(LFL_MOBILE OR LFL_WINDOWS)
  set(CRYPTO_DEF ${TERMINAL_CRYPTO_DEF} -DLFL_RFB)
  set(CRYPTO_LIB ${TERMINAL_CRYPTO_LIB})
else()
  set(CRYPTO_DEF)
  set(CRYPTO_LIB app_null_crypto)
endif()

if(LFL_LINUX OR LFL_WINDOWS)
  set(JSON_LIB app_jsoncpp_json)
else()
  set(JSON_LIB)
endif()

lfl_project(LTerminal com.lucidfusionlabs.LTerminal)
lfl_add_package(LTerminal SOURCES term.cpp term.h ${MENUS_SRC} ${RESOURCE_FILES} ${FLATBUFFERS_SRC_OUTPUTS}
                COMPILE_DEFINITIONS ${CRYPTO_DEF} ${MENUS_DEF} ${PTY_DEF}
                LINK_LIBRARIES ${LFL_APP_LIB} ${TERMINAL_FRAMEWORK} ${TERMINAL_GRAPHICS}
                app_null_audio app_null_camera app_null_matrix app_null_fft
                app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                app_null_jpeg app_null_gif app_null_ogg app_null_css ${LFL_APP_FONT} ${JSON_LIB}
                ${LFL_APP_SSL} app_null_js ${BILLING_LIB} ${ADVERTISING_LIB} ${LFL_APP_NAG}
                ${DB_LIB} ${CRYPTO_LIB} ${CRASHREPORTING_LIB} ${TERMINAL_TOOLKIT} ${LFL_APP_OS}
                LIB_FILES ${LFL_APP_LIB_FILES}
                ASSET_DIRS ${LFL_APP_ASSET_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/drawable-xhdpi
                ASSET_FILES ${LFL_APP_ASSET_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/assets/*
                ${LFL_SOURCE_DIR}/core/app/shaders/shadertoy/*.frag
                ${LFL_SOURCE_DIR}/core/app/assets/VeraMoBd.*)
lfl_post_build_start(LTerminal)
#add_dependencies(LTerminal mosh)

if(NOT LFL_MOBILE)
  lfl_project(LTerminal-render-sandbox)
  lfl_add_target(LTerminal-render-sandbox EXECUTABLE SOURCES render.cpp
                 LINK_LIBRARIES ${LFL_APP_LIB} app_null_framework app_null_graphics
                 app_null_audio app_null_camera app_null_matrix app_null_fft
                 app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                 app_libjpeg_jpeg app_giflib_gif app_null_ogg app_null_css ${LFL_APP_FONT}
                 app_null_ssl app_null_js
                 app_null_crypto app_null_dh ${CRASHREPORTING_LIB} app_null_toolkit ${LFL_APP_OS})
  lfl_post_build_copy_bin(LTerminal LTerminal-render-sandbox)
  add_dependencies(LTerminal LTerminal-render-sandbox)
endif()

if(FALSE AND NOT TERMINAL_MENUS)
  lfl_add_target(lssh EXECUTABLE SOURCES term.cpp term.rc ${FLATBUFFERS_SRC_OUTPUTS}
                 COMPILE_DEFINITIONS ${TERMINAL_CRYPTO_DEF} -DLFL_TERMINAL_MENUS
                 LINK_LIBRARIES ${LFL_APP_LIB} ${TERMINAL_FRAMEWORK} ${TERMINAL_GRAPHICS}
                 app_null_audio app_null_camera app_null_matrix app_null_fft
                 app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                 app_null_jpeg app_null_gif app_null_ogg app_null_css ${LFL_APP_FONT}
                 ${LFL_APP_SSL} app_null_js app_null_advertising
                 ${TERMINAL_DB_LIB} ${TERMINAL_CRYPTO_LIB} ${CRASHREPORTING_LIB} ${TERMINAL_TOOLKIT} ${LFL_APP_OS})
  lfl_post_build_copy_bin(LTerminal lssh)
  add_dependencies(LTerminal lssh)
endif()

if(CRYPTO_DEF AND NOT LFL_MOBILE)
  get_property(LFL_APP_FRAMEWORK_OS_LIB GLOBAL PROPERTY LFL_APP_FRAMEWORK_OS_LIB)
  lfl_add_target(term_tests EXECUTABLE SOURCES term_tests.cpp
	               COMPILE_DEFINITIONS ${TERMINAL_CRYPTO_DEF} -DLFL_TERMINAL_MENUS
                 INCLUDE_DIRECTORIES ${GTEST_INCLUDE}
                 LINK_LIBRARIES ${LFL_APP_LIB} ${GTEST_LIB} app_null_framework app_null_graphics
                 app_null_audio app_null_camera app_null_matrix app_null_fft
                 app_simple_resampler app_simple_loader ${LFL_APP_CONVERT}
                 app_null_png app_null_jpeg app_null_gif app_null_ogg app_null_css ${LFL_APP_FONT}
                 app_null_ssl app_null_js
								 ${TERMINAL_DB_LIB} ${TERMINAL_CRYPTO_LIB} app_null_crashreporting app_null_toolkit ${LFL_APP_OS} ${LFL_APP_FRAMEWORK_OS_LIB})
  add_dependencies(term_tests LTerminal)
endif()
