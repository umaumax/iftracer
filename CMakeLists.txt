cmake_minimum_required(VERSION 3.5.1)
project(iftracer)

OPTION(IFTRACER_EXAMPLE "Build iftracer example binary flag" OFF)

set(${PROJECT_NAME}_LIB_SRCS iftracer_hook.cpp)

set(CMAKE_CXX_STANDARD 11)
add_library(${PROJECT_NAME} ${${PROJECT_NAME}_LIB_SRCS})
target_link_libraries(${PROJECT_NAME}
  pthread
)
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "-ggdb3 -O3")

if(IFTRACER_EXAMPLE)
  set(${PROJECT_NAME}_EXAMPLE_SRCS main.cpp)
  add_executable(${PROJECT_NAME}_main ${${PROJECT_NAME}_EXAMPLE_SRCS})
  target_link_libraries(${PROJECT_NAME}_main
    pthread
    ${PROJECT_NAME}
  )
  set_target_properties(${PROJECT_NAME}_main PROPERTIES COMPILE_FLAGS "-ggdb3 -O3 -finstrument-functions -finstrument-functions-exclude-file-list=bits,include/c++")
  add_dependencies(${PROJECT_NAME}_main ${PROJECT_NAME})
endif(IFTRACER_EXAMPLE)
