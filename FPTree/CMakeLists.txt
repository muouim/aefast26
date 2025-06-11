# cmake version
cmake_minimum_required(VERSION 3.10)
project(DMTree)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-O3 -g")
# disable boost warning
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING)

# Compiler options
add_definitions(-w)

set(LINKS_FLAGS "-lmemcached -libverbs -lmlx5 -lpthread -lcityhash -lboost_system -lboost_coroutine -lboost_context -lprofiler -ltbb")

# head file path
set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)
include_directories(${INCLUDE_BASE})

# source directory
aux_source_directory(src SOURCE_DIR_SRCS)
aux_source_directory(src/rdma BASELINE_DIR_SRCS)

# add output lib
add_library(system STATIC ${SOURCE_DIR_SRCS} ${BASELINE_DIR_SRCS})
target_include_directories(system PUBLIC ${PROJECT_SOURCE_DIR}/include)

# add executable file
file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cc)
foreach (TEST ${TEST_SRC})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)

    # add source directory and dir directory
    add_executable(${TEST_NAME} ${TEST} ${SOURCE_DIR_SRCS} ${BASELINE_DIR_SRCS})
    
    target_link_libraries(${TEST_NAME} PUBLIC ${LINKS_FLAGS})
endforeach()

message(STATUS "### Done ###")