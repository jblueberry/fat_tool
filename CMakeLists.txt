cmake_minimum_required(VERSION 3.21)
project(fat)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
option(ENABLE_ASAN "Enable address sanitizer" OFF)

add_compile_options(-Og -g -Wall -Wno-unused-result)

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address -fno-omit-frame-pointer)
endif()

set(SOURCE_FILES main.cpp)
set(LIBRARY_FILES fat_manager)

add_library(${LIBRARY_FILES} STATIC fat_manager.cpp)

add_executable(fat ${SOURCE_FILES})

target_link_libraries(fat ${LIBRARY_FILES})
