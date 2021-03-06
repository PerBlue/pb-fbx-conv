cmake_minimum_required(VERSION 2.8)
project(pb-fbx-conv)

message("Source " "${CMAKE_SOURCE_DIR}")

set(INCLUDE "${CMAKE_SOURCE_DIR}/include")

if (WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11") # some string functions aren't defined by cygwin without this.
    add_definitions(-DWINDOWS -DPERF) # perf doesn't work anywhere other than windows yet
elseif(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    add_definitions(-DAPPLE)
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    add_definitions(-DLINUX)
endif()
add_definitions(-DOFBX_DEFINE_MAKE_UNIQUE)

include_directories(${INCLUDE})

file(GLOB SRC_FILES "src/*.h" "src/*.c" "src/*.cpp")
set(SOURCE_FILES ${SRC_FILES})
add_executable(pb-fbx-conv ${SOURCE_FILES})

if (APPLE)
    set(LIB "${CMAKE_SOURCE_DIR}/lib/osx")
    link_directories(${LIB})
elseif (WIN32)
    set(LIB "${CMAKE_SOURCE_DIR}/lib/windows")
    link_directories(${LIB})
    target_link_libraries(pb-fbx-conv -static-libgcc -static-libstdc++)
endif()

