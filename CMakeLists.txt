cmake_minimum_required(VERSION 3.16)
project(y-lib-httplib)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(OpenSSL REQUIRED)
# y-- source tree to compile against; yenv passes its installed toolchain
set(YLANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../y---lang" CACHE PATH "y-- source directory")
add_library(y-httplib SHARED module.cpp)
# symbols come from the ymm process at load time
if(APPLE)
    target_link_options(y-httplib PRIVATE -undefined dynamic_lookup)
endif()
# output liby-http so `import Http` finds it
set_target_properties(y-httplib PROPERTIES OUTPUT_NAME y-http)
target_include_directories(y-httplib PRIVATE ${YLANG_DIR}/include ${YLANG_DIR}/include/ylang ${YLANG_DIR}/src)
target_link_libraries(y-httplib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(y-httplib PRIVATE ws2_32 crypt32)
endif()
