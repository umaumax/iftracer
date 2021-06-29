cmake_minimum_required(VERSION 3.5.1)
project(iftracer)

OPTION(IFTRACER_EXAMPLE "Build iftracer example binary flag" OFF)
OPTION(IFTRACER_TEST "Build iftracer test binary flag" OFF)
set(CMAKE_CXX_STANDARD 11)

####
# library
set(${PROJECT_NAME}_LIB_SRCS iftracer_hook.cpp mmap_writer.cpp)
add_library(${PROJECT_NAME} ${${PROJECT_NAME}_LIB_SRCS})
# target_link_libraries(${PROJECT_NAME}
# )
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "-ggdb3 -O3")

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
    set_target_properties(${PROJECT_NAME}_main PROPERTIES COMPILE_FLAGS "-ggdb3 -O3 -finstrument-functions-after-inlining")
  else()
    set_target_properties(${PROJECT_NAME}_main PROPERTIES COMPILE_FLAGS "-ggdb3 -O3 -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++")
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
