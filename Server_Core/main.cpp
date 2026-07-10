#include <iostream>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#endif

// CMake에서 설정한 Include Directory 덕분에 
// ../Shared/include/ 경로를 쓰지 않아도 바로 인식되어야 합니다.
#include "Packets.h" 

int main() {
#ifdef _WIN32
    // Windows 콘솔 인코딩을 UTF-8로 설정 (한글 깨짐 방지)
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "========== [Server Core Build Test] ==========\n";

    // 1. 공용 패킷 헤더(#pragma pack) 검증
    // 명세서에 오기로 78바이트라 적혀있으나, 실제 필드 합산 시 68바이트입니다.
    std::cout << "[Test 1] FSyncTransformPacket Size: " 
              << sizeof(FSyncTransformPacket) << " bytes (Expected: 68)\n";

    if (sizeof(FSyncTransformPacket) != 68) {
        std::cerr << "[Error] 패킷 크기가 일치하지 않습니다! 메모리 패딩을 확인하세요.\n";
        return -1;
    }

    // 2. 윈도우 소켓(Winsock2) 라이브러리 링크 검증
    // CMake의 target_link_libraries가 잘 작동했는지 확인합니다.
    #ifdef _WIN32
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult == 0) {
        std::cout << "[Test 2] Winsock2 (ws2_32) Initialized Successfully!\n";
        WSACleanup();
    } else {
        std::cerr << "[Error] Winsock 초기화 실패! 라이브러리 링크를 확인하세요.\n";
        return -1;
    }
    #else
    std::cout << "[Test 2] Winsock2 (ws2_32) 검증은 Windows 전용입니다.\n";
    #endif // _WIN32

    std::cout << "==============================================\n";
    std::cout << "모든 환경 세팅이 완벽합니다. 본격적인 개발을 시작하세요!\n";

    return 0;
}