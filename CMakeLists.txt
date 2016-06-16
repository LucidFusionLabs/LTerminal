# $Id: CMakeLists.txt 1320 2014-10-26 08:12:52Z justin $

if(LFL_MOBILE OR LFL_WINDOWS OR (LFL_DEBUG AND NOT LFL_APPLE))
  set(CRYPTO_DEF -DLFL_CRYPTO)
  set(CRYPTO_LIB ${LFL_APP_CRYPTO} app_openssl_dh)
else()
  set(CRYPTO_DEF app_null_crypto)
  set(CRYPTO_LIB app_null_dh)
endif()

if(NOT LFL_MOBILE)
  lfl_project(lterm-render-sandbox)
  lfl_add_package(lterm-render-sandbox SOURCES render.cpp
                  LINK_LIBRARIES ${LFL_APP_LIB} app_null_framework app_null_graphics
                  app_null_audio app_null_camera app_null_matrix app_null_fft
                  app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                  app_libjpeg_jpeg app_giflib_gif app_null_ogg app_null_css ${LFL_APP_FONT}
                  app_null_ssl app_null_js app_null_tu
                  app_null_crypto app_null_dh ${LFL_APP_OS})
  lfl_post_build_copy_bin(lterm-render-sandbox lterm-render-sandbox LTerminal)
endif()

lfl_project(lterm)
lfl_add_package(lterm SOURCES term.cpp term.rc COMPILE_DEFINITIONS ${CRYPTO_DEF}
                LINK_LIBRARIES ${LFL_APP_LIB} ${LFL_APP_FRAMEWORK} ${LFL_APP_GRAPHICS}
                app_null_audio app_null_camera app_null_matrix app_null_fft
                app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                app_null_jpeg app_null_gif app_null_ogg app_null_css ${LFL_APP_FONT}
                ${LFL_APP_SSL} app_null_js app_null_tu
                ${CRYPTO_LIB} ${LFL_APP_OS})
set(LFL_APP_ASSET_FILES ${LFL_APP_ASSET_FILES} ${LFL_SOURCE_DIR}/core/app/shaders/shadertoy/*.frag
    ${LFL_SOURCE_DIR}/core/app/assets/VeraMoBd.*)
lfl_post_build_start(lterm lterm LTerminal)

if(NOT LFL_MOBILE)
  add_dependencies(lterm-render-sandbox lterm)
endif()
