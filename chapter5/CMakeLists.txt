# 指定最小版本
cmake_minimum_required(VERSION 3.0)
# 项目名
project(leptjson_test C)
#
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGES "${CMAKE_C_FLAGES} -ansi -pedantic -Wall")
endif()

file(GLOB LEPTJSON_C ${CMAKE_CURRENT_SOURCE_DIR}/src/leptjson.c)
file(GLOB TEST_C ${CMAKE_CURRENT_SOURCE_DIR}/src/test.c)
# 指定头文件所在目录
include_directories(${PROJECT_SOURCE_DIR}/include)
# 设置可执行文件生成目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置静态库生成目录
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 生成静态库
add_library(leptjson ${LEPTJSON_C})
# 指定链接的库目录
link_directories(${PROJECT_SOURCE_DIR}/lib)
# 生成可执行文件
add_executable(leptjson_test ${TEST_C})
# 链接库
target_link_libraries(leptjson_test leptjson)