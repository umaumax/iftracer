cmake_minimum_required(VERSION 3.5.1)
project(iftracer)

OPTION(IFTRACER_EXAMPLE "Build iftracer example binary flag" OFF)
OPTION(IFTRACER_TEST "Build iftracer test binary flag" OFF)
OPTION(IFTRACER_LOCK_FREE_QUEUE "Enable lock-free-queue munmap() for flush file (note: spwan another thread)" OFF)
OPTION(IFTRACER_DISABLE_CPU_ID  "Disable cpu id recording" OFF)
set(CMAKE_CXX_STANDARD 11)

####
# for static(.a) library
set(${PROJECT_NAME}_LIB_SRCS iftracer_hook.cpp mmap_writer.cpp)
add_library(${PROJECT_NAME}_OBJECT OBJECT ${${PROJECT_NAME}_LIB_SRCS})
set_property(TARGET ${PROJECT_NAME}_OBJECT PROPERTY POSITION_INDEPENDENT_CODE ON)
set(${PROJECT_NAME}_CXX_FLGAS "")
if(IFTRACER_LOCK_FREE_QUEUE)
  list(APPEND ${PROJECT_NAME}_CXX_FLGAS "-DIFTRACER_LOCK_FREE_QUEUE")
endif()
if(IFTRACER_DISABLE_CPU_ID)
  list(APPEND ${PROJECT_NAME}_CXX_FLGAS "-DIFTRACER_DISABLE_CPU_ID")
endif()

set_target_properties(${PROJECT_NAME}_OBJECT PROPERTIES COMPILE_FLAGS "-g3 -O3 ${${PROJECT_NAME}_CXX_FLGAS}")

add_library(${PROJECT_NAME} STATIC $<TARGET_OBJECTS:${PROJECT_NAME}_OBJECT>)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

# for shared(.so) library
add_library(${PROJECT_NAME}_SHARED SHARED $<TARGET_OBJECTS:${PROJECT_NAME}_OBJECT>)
set_target_properties(${PROJECT_NAME}_SHARED PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_SHARED)

####
# example iftracer_main app
if(IFTRACER_EXAMPLE)
  set(${PROJECT_NAME}_EXAMPLE_SRCS main.cpp)
  add_executable(${PROJECT_NAME}_main ${${PROJECT_NAME}_EXAMPLE_SRCS})
  target_link_libraries(${PROJECT_NAME}_main
    pthread
    ${PROJECT_NAME}
    )
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_target_properties(${PROJECT_NAME}_main PROPERTIES COMPILE_FLAGS "-g1 -O3 -DIFTRACER_ENABLE_API -finstrument-functions-after-inlining")
  else()
    set_target_properties(${PROJECT_NAME}_main PROPERTIES COMPILE_FLAGS "-g1 -O3 -DIFTRACER_ENABLE_API -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++")
  endif()
  add_dependencies(${PROJECT_NAME}_main ${PROJECT_NAME})
endif(IFTRACER_EXAMPLE)

####
# mmap_writer_test
if(IFTRACER_TEST)
  enable_testing()
  set(${PROJECT_NAME}_MMAP_WRITER_TEST_SRCS mmap_writer_test.cpp)
  add_executable(${PROJECT_NAME}_mmap_writer_test ${${PROJECT_NAME}_MMAP_WRITER_TEST_SRCS})
  target_link_libraries(${PROJECT_NAME}_mmap_writer_test
    ${PROJECT_NAME}
    )
  add_dependencies(${PROJECT_NAME}_mmap_writer_test ${PROJECT_NAME})
  add_test(
    NAME mmap_writer_test
    COMMAND $<TARGET_FILE:${PROJECT_NAME}_mmap_writer_test>
    )
endif(IFTRACER_TEST)
