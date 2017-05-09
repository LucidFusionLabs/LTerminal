# $Id: CMakeLists.txt 1325 2014-10-29 06:08:31Z justin $

if(LFL_MOSH)
  set(CONFIG_OPTS ${CONFIGURE_OPTIONS})
  set(CONFIG_ENV ${CONFIGURE_ENV} PROTOC=${PROTOBUF_PROTOC_EXECUTABLE} protobuf_CFLAGS=-I${PROTOBUF_INCLUDE_DIR})
  if(LFL_IOS OR LFL_APPLE)
    set(CONFIG_OPTS --with-crypto-library=apple-common-crypto)
  endif()
  ExternalProject_Add(mosh PREFIX mosh LOG_CONFIGURE ON LOG_BUILD ON BUILD_IN_SOURCE ON
                      CONFIGURE_COMMAND <SOURCE_DIR>/autogen.sh
                      COMMAND ${CMAKE_COMMAND} -E env ${CONFIG_ENV} protobuf_LIBS=$<TARGET_FILE:libprotobuf>
                      <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> ${CONFIG_OPTS}
                      DOWNLOAD_COMMAND rm -rf mosh COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/mosh mosh)
  add_library(libmosh IMPORTED STATIC GLOBAL)
  add_dependencies(libmosh mosh)
  add_dependencies(mosh libprotobuf)
  set_property(TARGET libmosh PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/ogg/lib/libogg.a)
endif()
