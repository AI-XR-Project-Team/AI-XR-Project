# 1. CMake 최소 요구 버전 설정
cmake_minimum_required(VERSION 3.15)

# 2. 프로젝트 이름 및 언어 설정
project(CoreServer CXX)

# 3. C++ 표준 설정
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ★ [추가된 부분] 한글 깨짐 방지: MSVC 컴파일러를 강제로 UTF-8 모드로 설정합니다.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 4. 소스 파일 모으기
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
# 빌드 시 생성되는 임시 cpp 파일들(CMakeCXXCompilerId.cpp 등)이 포함되지 않도록 필터링합니다.
list(FILTER SOURCES EXCLUDE REGEX "build|cmake-build-debug")

# 5. 실행 파일 생성
add_executable(${PROJECT_NAME} ${SOURCES})

# 6. 포함 디렉토리(Include Directories) 설정
target_include_directories(${PROJECT_NAME} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/include"
)

# 7. 윈도우 IOCP 라이브러리 링크
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock)
endif()