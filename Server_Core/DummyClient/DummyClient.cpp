// ============================================================
//  DummyClient/DummyClient.cpp
//  역할: UDP 서버 부하 테스트 및 통신 검증용 더미 클라이언트
//
//  동작 흐름:
//    1. WSAStartup + UDP 소켓 생성
//    2. FJoinPacket 1회 전송
//    3. 33ms(≈30Hz) 간격으로 FSyncTransformPacket 무한 전송
//    4. Ctrl+C → Graceful Shutdown (closesocket + WSACleanup)
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstring>
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>

#include "Packets.h"
#include "Utils/GLog.h"

// ──────────────────────────────────────────────
//  설정 상수
// ──────────────────────────────────────────────
static constexpr const char*    SERVER_IP      = "127.0.0.1";
static constexpr uint16_t       SERVER_PORT    = 9000;
static constexpr uint32_t       DUMMY_USER_ID  = 99001;      // 더미 클라이언트 식별 ID
static constexpr uint32_t       DUMMY_SESSION  = 1;
static constexpr float          MOVE_SPEED     = 0.5f;       // 프레임당 이동 거리 (단위: cm)
static constexpr auto           SEND_INTERVAL  = std::chrono::milliseconds(33); // ≈30Hz

// ──────────────────────────────────────────────
//  전역 상태
// ──────────────────────────────────────────────
static std::atomic<bool> g_bRunning{ true };
static SOCKET            g_Socket{ INVALID_SOCKET };

// ──────────────────────────────────────────────
//  헬퍼: 패킷 헤더 채우기
// ──────────────────────────────────────────────
static void FillHeader(FPacketHeader& header, EPacketType type, uint16_t bodyLen,
                       uint32_t seqNum, uint8_t flags) noexcept
{
    header.Magic[0] = 0x41;   // 'A'
    header.Magic[1] = 0x52;   // 'R'
    header.Version  = 0x01;
    header.Type     = static_cast<uint8_t>(type);
    header.BodyLen  = bodyLen;
    header.SeqNum   = seqNum;  // RELIABLE: 신뢰 스트림 순서 / 그 외: 비신뢰 순서
    header.AckNum   = 0;       // 더미는 서버의 신뢰 송신을 ACK하지 않으므로 0
    header.Flags    = flags;   // PKT_FLAG_RELIABLE 등
}

// ──────────────────────────────────────────────
//  Ctrl+C 핸들러
// ──────────────────────────────────────────────
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            GLog::Warn("Interrupt received. Initiating graceful shutdown...");
            g_bRunning.store(false, std::memory_order_release);
            return TRUE;
        default:
            return FALSE;
    }
}

// ──────────────────────────────────────────────
//  초기화
// ──────────────────────────────────────────────
static bool InitWinsock()
{
    WSADATA wsaData{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        GLog::Error("WSAStartup failed: {}", ::WSAGetLastError());
        return false;
    }
    GLog::Info("Winsock2 initialized.");
    return true;
}

static bool CreateAndConnectSocket(SOCKADDR_IN& outServerAddr)
{
    g_Socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_Socket == INVALID_SOCKET)
    {
        GLog::Error("socket() failed: {}", ::WSAGetLastError());
        return false;
    }

    ::ZeroMemory(&outServerAddr, sizeof(outServerAddr));
    outServerAddr.sin_family = AF_INET;
    outServerAddr.sin_port   = ::htons(SERVER_PORT);
    if (::inet_pton(AF_INET, SERVER_IP, &outServerAddr.sin_addr) != 1)
    {
        GLog::Error("inet_pton failed. Check SERVER_IP: {}", SERVER_IP);
        return false;
    }

    GLog::Info("UDP socket created. Target: {}:{}", SERVER_IP, SERVER_PORT);
    return true;
}

// ──────────────────────────────────────────────
//  패킷 전송 헬퍼
// ──────────────────────────────────────────────
static bool SendPacket(const void* pData, int size, const SOCKADDR_IN& serverAddr)
{
    const int sent = ::sendto(
        g_Socket,
        static_cast<const char*>(pData),
        size,
        0,
        reinterpret_cast<const SOCKADDR*>(&serverAddr),
        sizeof(SOCKADDR_IN)
    );
    if (sent == SOCKET_ERROR)
    {
        GLog::Error("sendto() failed: {}", ::WSAGetLastError());
        return false;
    }
    return true;
}

// ──────────────────────────────────────────────
//  접속 패킷 전송
// ──────────────────────────────────────────────
static void SendJoinPacket(const SOCKADDR_IN& serverAddr)
{
    FJoinPacket pkt{};
    // JOIN 은 신뢰 이벤트 → RELIABLE 플래그, 신뢰 스트림 SeqNum=1 로 시작
    FillHeader(pkt.Header, EPacketType::PKT_JOIN,
               static_cast<uint16_t>(sizeof(FJoinPacket) - sizeof(FPacketHeader)),
               1, PKT_FLAG_RELIABLE);

    pkt.UserId        = DUMMY_USER_ID;
    pkt.LocationId    = 1001;          // 더미 유적지 ID
    pkt.ClientVersion = 0x00010000;    // v1.0.0
    const char nickname[] = "DummyBot";
    std::memcpy(pkt.Nickname, nickname, sizeof(nickname));

    if (SendPacket(&pkt, static_cast<int>(sizeof(FJoinPacket)), serverAddr))
    {
        GLog::Info("[TX] PKT_JOIN  | UserId={} LocationId={}", pkt.UserId, pkt.LocationId);
    }
}

// ──────────────────────────────────────────────
//  Transform 패킷 전송 (30Hz 루프에서 호출)
// ──────────────────────────────────────────────
static void SendTransformPacket(const SOCKADDR_IN& serverAddr,
                                 uint32_t           seqNum,
                                 float              posX,
                                 float              posY,
                                 float              posZ)
{
    FSyncTransformPacket pkt{};
    // Transform 은 비신뢰 순서 스트림 → 플래그 없음, 헤더 SeqNum 으로 스테일 판정
    FillHeader(pkt.Header, EPacketType::PKT_TRANSFORM,
               static_cast<uint16_t>(sizeof(FSyncTransformPacket) - sizeof(FPacketHeader)),
               seqNum, PKT_FLAG_NONE);

    pkt.UserId    = DUMMY_USER_ID;
    pkt.SessionId = DUMMY_SESSION;
    pkt.SeqNum    = seqNum;
    pkt.Timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    // 더미 위치 데이터
    pkt.PosX = posX;
    pkt.PosY = posY;
    pkt.PosZ = posZ;

    // 더미 회전 (단위 쿼터니언 — 정방향)
    pkt.RotX = 0.0f;
    pkt.RotY = 0.0f;
    pkt.RotZ = 0.0f;
    pkt.RotW = 1.0f;

    // 더미 속도 (X축 이동)
    pkt.VelX = MOVE_SPEED;
    pkt.VelY = 0.0f;
    pkt.VelZ = 0.0f;

    pkt.EraId = 1;    // 신라 시대
    pkt.Flags = 0x01; // IS_MOVING

    if (SendPacket(&pkt, static_cast<int>(sizeof(FSyncTransformPacket)), serverAddr))
    {
        // 30Hz 로그는 너무 잦으므로 10패킷마다 1회 출력
        if (seqNum % 10 == 0)
        {
            GLog::Info("[TX] PKT_TRANSFORM | Seq={:>6} Pos=({:.2f}, {:.2f}, {:.2f})",
                       seqNum, posX, posY, posZ);
        }
    }
}

// ──────────────────────────────────────────────
//  정리
// ──────────────────────────────────────────────
static void Cleanup()
{
    if (g_Socket != INVALID_SOCKET)
    {
        ::closesocket(g_Socket);
        g_Socket = INVALID_SOCKET;
        GLog::Info("Socket closed.");
    }
    ::WSACleanup();
    GLog::Info("WSACleanup done. Goodbye.");
}

// ──────────────────────────────────────────────
//  진입점
// ──────────────────────────────────────────────
int main()
{
    // 콘솔 UTF-8 설정 (한글 닉네임 등 깨짐 방지)
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);

    GLog::Info("========================================");
    GLog::Info("  AI-XR DummyClient  |  UserId={}", DUMMY_USER_ID);
    GLog::Info("  Target: {}:{}", SERVER_IP, SERVER_PORT);
    GLog::Info("========================================");

    // ── Ctrl+C 핸들러 등록 ────────────────────
    if (!::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
    {
        GLog::Error("SetConsoleCtrlHandler failed.");
        return -1;
    }

    // ── Winsock + 소켓 초기화 ─────────────────
    if (!InitWinsock()) return -1;

    SOCKADDR_IN serverAddr{};
    if (!CreateAndConnectSocket(serverAddr))
    {
        ::WSACleanup();
        return -1;
    }

    // ── FJoinPacket 1회 전송 ──────────────────
    SendJoinPacket(serverAddr);

    // ── 30Hz 메인 루프 ─────────────────────────
    GLog::Info("Starting 30Hz transform loop. Press Ctrl+C to stop.");

    uint32_t seqNum = 0;
    float posX = 0.0f;
    float posY = 100.0f;  // Y축 기본 높이
    float posZ = 0.0f;

    // 루프 주기 정밀 제어를 위해 steady_clock 사용
    auto nextTick = std::chrono::steady_clock::now();

    while (g_bRunning.load(std::memory_order_acquire))
    {
        nextTick += SEND_INTERVAL;

        // ── 더미 좌표 갱신 ──────────────────────
        // X-Z 평면에서 원형 궤적 이동 (반지름 500cm)
        const float angle = static_cast<float>(seqNum) * 0.02f; // radian
        posX = 500.0f * std::cos(angle);
        posZ = 500.0f * std::sin(angle);

        SendTransformPacket(serverAddr, seqNum, posX, posY, posZ);
        ++seqNum;

        // ── 다음 틱까지 정밀 대기 ───────────────
        std::this_thread::sleep_until(nextTick);
    }

    GLog::Info("Main loop exited. seqNum={}", seqNum);

    // ── Graceful Shutdown ─────────────────────
    Cleanup();
    return 0;
}
