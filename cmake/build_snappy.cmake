set(snappy_PREFIX ${CMAKE_BINARY_DIR}/external/snappy)

ExternalProject_Add(
    build_snappy
    EXCLUDE_FROM_ALL 1
    PREFIX ${snappy_PREFIX}
    URL https://github.com/google/snappy/archive/1.1.7.tar.gz
    CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/external -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    BUILD_COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE}
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE} --target install
    LOG_UPDATE ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)
if(SOLID_ON_WINDOWS)
    set(SNAPPY_LIB ${CMAKE_BINARY_DIR}/external/lib/snappy.lib)
else()
    set(SNAPPY_LIB ${CMAKE_BINARY_DIR}/external/lib/libsnappy.a)
endif()
