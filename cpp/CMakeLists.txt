cmake_minimum_required(VERSION 3.13)
project(vaultusb_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_executable(vaultusb_cpp
  src/main.cpp
)

target_compile_options(vaultusb_cpp PRIVATE -O2 -Wall -Wextra -Wpedantic)

if(UNIX)
  target_link_libraries(vaultusb_cpp PRIVATE pthread)
endif()

install(TARGETS vaultusb_cpp RUNTIME DESTINATION bin)
