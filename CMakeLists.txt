cmake_minimum_required(VERSION 3.11)
project(disco)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(${PROJECT_NAME}
  src/Disk.cpp
  src/Interpreter.cpp
  src/BufferManager.cpp
  src/Table.cpp
  main.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE include) 
