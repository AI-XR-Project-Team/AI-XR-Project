# ============================================================
#  CMakeLists.txt — AI-XR Server_Core
#  타겟:
#    CoreServer  : IOCP UDP 서버 (main.cpp + Network/*.cpp)
#    DummyClient : 30Hz 부하 테스트 클라이언트 (DummyClient/*.cpp)
# ============================================================

# 1. CMake 최소 요구 버전 설정
cmake_minimum_required(VERSION 3.15)

# 2. 프로젝트 이름 및 언어 설정
project(ServerCore CXX)

# 3. C++ 표준 설정
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. MSVC UTF-8 강제 설정 (한글 깨짐 방지)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ──────────────────────────────────────────────────────────────
#  공용 인클루드 경로 (Interface Library — 두 타겟이 모두 사용)
# ──────────────────────────────────────────────────────────────
add_library(SharedIncludes INTERFACE)
target_include_directories(SharedIncludes INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"               # Network/, Utils/ 등 상대 경로
    "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/include"  # Packets.h
)

# ──────────────────────────────────────────────────────────────
#  타겟 1: CoreServer (IOCP UDP 서버)
# ──────────────────────────────────────────────────────────────
set(SERVER_SOURCES
    main.cpp
    Network/IOCPServer.cpp
    Network/SessionManager.cpp
    Network/RudpChannel.cpp
)

add_executable(CoreServer ${SERVER_SOURCES})

target_link_libraries(CoreServer
    PRIVATE SharedIncludes
)

if(WIN32)
    target_link_libraries(CoreServer PRIVATE ws2_32 mswsock)
endif()

# ──────────────────────────────────────────────────────────────
#  타겟 2: DummyClient (UDP 부하 테스트 클라이언트)
# ──────────────────────────────────────────────────────────────
set(DUMMY_SOURCES
    DummyClient/DummyClient.cpp
)

add_executable(DummyClient ${DUMMY_SOURCES})

target_link_libraries(DummyClient
    PRIVATE SharedIncludes
)

if(WIN32)
    target_link_libraries(DummyClient PRIVATE ws2_32)
endif()