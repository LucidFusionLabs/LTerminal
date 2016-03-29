# $Id: CMakeLists.txt 1320 2014-10-26 08:12:52Z justin $

if(NOT LFL_MOBILE)
  lfl_project(lterm-render-sandbox)
  lfl_add_target(lterm-render-sandbox render.cpp)
  target_compile_definitions(lterm-render-sandbox PUBLIC ${LFL_APP_DEF})
  target_include_directories(lterm-render-sandbox PUBLIC ${LFL_APP_INCLUDE})
  target_link_libraries(lterm-render-sandbox ${LFL_APP_LIB} app_null_framework app_null_graphics
                        app_null_audio app_null_camera app_null_matrix app_null_fft
                        app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                        app_libjpeg_jpeg app_giflib_gif app_null_ogg app_null_css app_null_ttf
                        app_null_ssl app_null_js app_null_tu
                        app_null_crypto app_null_dh app_null_os)
  lfl_post_build_copy_bin(lterm-render-sandbox lterm-render-sandbox LTerminal)
endif()

lfl_project(lterm)
lfl_add_target(lterm term.cpp term.rc)
target_compile_definitions(lterm PUBLIC ${LFL_APP_DEF})
target_include_directories(lterm PUBLIC ${LFL_APP_INCLUDE})
target_link_libraries(lterm ${LFL_APP_LIB} ${LFL_APP_FRAMEWORK} ${LFL_APP_GRAPHICS}
                      app_null_audio app_null_camera app_null_matrix app_null_fft
                      app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                      app_null_jpeg app_null_gif app_null_ogg app_null_css app_null_ttf
                      app_openssl_ssl app_null_js app_null_tu
                      ${LFL_APP_CRYPTO} app_openssl_dh ${LFL_APP_OS})
set(LFL_APP_ASSETS ${LFL_APP_ASSETS} ${LFL_SOURCE_DIR}/core/app/shaders/shadertoy/*.frag
    ${LFL_SOURCE_DIR}/core/app/assets/VeraMoBd.*)
lfl_post_build_start(lterm lterm LTerminal)

if(NOT LFL_MOBILE)
  add_dependencies(lterm-render-sandbox lterm)
endif()
