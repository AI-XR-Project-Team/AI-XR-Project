cmake_minimum_required(VERSION 3.15)

project(CoreServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"           # 서버 자체 헤더 (include 폴더)
        "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/include" # 공용 패킷 헤더 (Shared 폴더)
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock)
endif()