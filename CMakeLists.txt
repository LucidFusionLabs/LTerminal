# $Id: CMakeLists.txt 1320 2014-10-26 08:12:52Z justin $

lfl_project(lterm-render-sandbox)
lfl_add_target(lterm-render-sandbox render.cpp)
target_compile_definitions(lterm-render-sandbox PUBLIC ${LFAPP_DEF})
target_include_directories(lterm-render-sandbox PUBLIC ${LFAPP_INCLUDE})
target_link_libraries(lterm-render-sandbox ${LFL_APP_LIB} app_null_os app_null_framework app_null_graphics
                      app_null_audio app_null_camera app_null_matrix app_null_fft
                      app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                      app_libjpeg_jpeg app_giflib_gif app_null_css app_null_ttf app_null_ssl
                      app_null_crypto app_null_dh)
lfl_post_build_copy_bin(lterm-render-sandbox lterm-render-sandbox LTerminal)

lfl_project(lterm)
lfl_add_target(lterm term.cpp term.rc)
target_compile_definitions(lterm PUBLIC ${LFAPP_DEF})
target_include_directories(lterm PUBLIC ${LFAPP_INCLUDE})
target_link_libraries(lterm ${LFL_APP_LIB} ${LFL_APP_OS} ${LFL_APP_FRAMEWORK} ${LFL_APP_GRAPHICS}
                      app_null_audio app_null_camera app_null_matrix app_null_fft
                      app_simple_resampler app_simple_loader ${LFL_APP_CONVERT} app_libpng_png
                      app_null_jpeg app_null_gif app_null_css app_freetype_ttf app_openssl_ssl
                      ${LFL_APP_CRYPTO} app_openssl_dh)
lfl_post_build_start(lterm lterm LTerminal)
add_dependencies(lterm-render-sandbox lterm)

