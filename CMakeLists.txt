cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenOni VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(ONI_WARN -Wall -Wextra -Wno-unused-parameter)

# The portable core: everything except host backends (no platform headers).
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.c)
set(HOST_SRC src/vfs_host.c)
foreach(f ${HOST_SRC})
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

add_library(oni_core STATIC ${CORE_SRC})
target_include_directories(oni_core PUBLIC src)
target_compile_options(oni_core PRIVATE ${ONI_WARN})
if(NOT APPLE AND NOT WIN32)
  target_link_libraries(oni_core PUBLIC m)   # libm is separate from libc on Linux
endif()

# openoni-extract: the game folder from the Oni CD (folder or ISO), without the Windows installer.
add_executable(openoni-extract tools/openoni-extract/main.c ${HOST_SRC})
target_link_libraries(openoni-extract PRIVATE oni_core)
target_compile_options(openoni-extract PRIVATE ${ONI_WARN})

# Tests read the CD through the file layer (OPENONI_CD=<dir|image>) or the game folder (./game, or
# OPENONI_DATA=<dir>), and skip without data.
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*_test.c)
foreach(src ${TEST_SRC})
  get_filename_component(t ${src} NAME_WE)
  add_executable(${t} ${src} ${HOST_SRC})
  target_link_libraries(${t} PRIVATE oni_core)
  target_compile_options(${t} PRIVATE ${ONI_WARN})
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
