#pragma once

// ============================================================
//  IOCPServer.h
//  역할: IOCP 기반 UDP 서버의 핵심 클래스 선언
//  스레드 모델:
//    - Main 스레드  : WSARecvFrom 비동기 수신 루프 (Dispatcher 역할)
//    - Worker 스레드: GetQueuedCompletionStatus 대기 → 패킷 처리
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <cstdint>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <string>
#include <functional>

#include "Packets.h"
#include "Network/SessionManager.h"

// ──────────────────────────────────────────────
//  상수
// ──────────────────────────────────────────────
inline constexpr uint16_t SERVER_PORT        = 9000;
inline constexpr uint32_t MAX_UDP_PAYLOAD    = 1472;   // 이더넷 MTU(1500) - IP(20) - UDP(8)
inline constexpr uint32_t RECV_BUFFER_COUNT  = 64;     // 사전 할당 수신 버퍼 수
inline constexpr uint32_t WORKER_THREAD_COUNT = 4;     // Worker 스레드 수 (2~4)
inline constexpr int64_t  SESSION_TIMEOUT_MS  = 10000; // 무통신 세션 타임아웃 (하트비트 1Hz 기준 10초)

// 패킷 매직 바이트 ("AR")
inline constexpr uint8_t MAGIC_BYTE_0 = 0x41;  // 'A'
inline constexpr uint8_t MAGIC_BYTE_1 = 0x52;  // 'R'

// ──────────────────────────────────────────────
//  IOCP 완료 키 및 오버랩 확장 구조체
// ──────────────────────────────────────────────

// IOCP 완료 포트에서 식별하는 완료 키 종류
enum class ECompletionKey : ULONG_PTR {
    RecvCompleted = 1,  // WSARecvFrom 완료
    Shutdown      = 0   // Worker 스레드 종료 신호
};

// WSARecvFrom 에 넘기는 OVERLAPPED 확장 구조체
struct FRecvOverlapped {
    WSAOVERLAPPED   Overlapped{};           // 반드시 첫 번째 멤버
    WSABUF          WsaBuf{};              // 버퍼 디스크립터
    char            Buffer[MAX_UDP_PAYLOAD]{};  // 실제 수신 데이터
    SOCKADDR_IN     RemoteAddr{};          // 송신자 주소
    INT             RemoteAddrLen{ sizeof(SOCKADDR_IN) };
};

// ──────────────────────────────────────────────
//  IOCPServer 클래스
// ──────────────────────────────────────────────
class IOCPServer {
public:
    IOCPServer();
    ~IOCPServer();

    // 복사·이동 금지 (소켓/핸들 RAII 관리)
    IOCPServer(const IOCPServer&)            = delete;
    IOCPServer& operator=(const IOCPServer&) = delete;
    IOCPServer(IOCPServer&&)                 = delete;
    IOCPServer& operator=(IOCPServer&&)      = delete;

    // ── Public API ──────────────────────────────
    /**
     * @brief 서버를 초기화하고 비동기 수신을 시작합니다.
     * @param port 바인딩할 UDP 포트 (기본값: SERVER_PORT)
     * @return 성공 시 true
     */
    bool Start(uint16_t port = SERVER_PORT);

    /**
     * @brief 서버를 정상 종료합니다. Worker 스레드 합류 대기 후 반환.
     */
    void Shutdown();

    /**
     * @brief Main 스레드에서 호출되는 수신 루프.
     *        외부에서 직접 호출하거나 별도 스레드에서 구동할 수 있습니다.
     */
    void RunRecvLoop();

private:
    // ── 초기화 헬퍼 ────────────────────────────
    bool InitWinsock();
    bool CreateSocket();
    bool BindSocket(uint16_t port);
    bool CreateIOCP();
    bool AssociateSocketWithIOCP();
    bool PostInitialRecvs();

    // ── Worker 스레드 ──────────────────────────
    /**
     * @brief Worker 스레드 진입점. GetQueuedCompletionStatus 루프.
     */
    void WorkerThread();

    /**
     * @brief 비동기 수신 완료 후 처리를 Worker 스레드에서 수행.
     * @param pOvlp  완료된 FRecvOverlapped 포인터
     * @param bytes  수신된 바이트 수
     */
    void OnRecvCompleted(FRecvOverlapped* pOvlp, DWORD bytes);

    /**
     * @brief 다음 비동기 수신을 IOCP에 등록합니다.
     * @param pOvlp 재사용할 오버랩 구조체 포인터
     */
    void PostRecv(FRecvOverlapped* pOvlp);

    // ── 패킷 Dispatcher ───────────────────────
    /**
     * @brief 수신 버퍼에서 헤더를 검증하고, 송신자 주소로 세션을 해석한 뒤
     *        패킷 종류별 핸들러에 세션을 위임합니다.
     * @param data       원시 바이트 배열
     * @param byteLen    수신 바이트 수
     * @param senderAddr 송신자 주소
     */
    void DispatchPacket(const char* data, DWORD byteLen, const SOCKADDR_IN& senderAddr);

    // ── 패킷 핸들러 (stub) ────────────────────
    //   Dispatcher 가 미리 해석한 세션을 함께 전달합니다.
    //   (주소·UserId 등은 session 객체에서 조회)
    using SessionPtr = SessionManager::SessionPtr;

    void HandleJoin       (const FJoinPacket&          pkt, const SessionPtr& session);
    void HandleLeave      (const FLeavePacket&         pkt, const SessionPtr& session);
    void HandleTransform  (const FSyncTransformPacket& pkt, const SessionPtr& session);
    void HandleEraChange  (const FEraChangePacket&     pkt, const SessionPtr& session);
    void HandleHeartbeat  (const FHeartbeatPacket&     pkt, const SessionPtr& session);
    void HandleAiTrigger  (const FAiTriggerPacket&     pkt, const SessionPtr& session);
    void HandleAck        (const FAckPacket&           pkt, const SessionPtr& session);
    void HandleServerState(const FServerStatePacket&   pkt, const SessionPtr& session);

    // ── 멤버 변수 ─────────────────────────────
    SOCKET   m_Socket{ INVALID_SOCKET };
    HANDLE   m_hIOCP{ nullptr };

    std::atomic<bool> m_bRunning{ false };

    // Worker 스레드 풀
    std::vector<std::thread> m_WorkerThreads;

    // 사전 할당된 오버랩 버퍼 풀 (RECV_BUFFER_COUNT 개)
    std::vector<std::unique_ptr<FRecvOverlapped>> m_RecvPool;

    // 접속 세션 관리자 (여러 Worker 스레드가 공유 접근)
    SessionManager m_SessionManager;
};
