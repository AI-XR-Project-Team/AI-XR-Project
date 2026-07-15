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
#include <mutex>
#include <string>
#include <functional>

#include "Packets.h"
#include "Network/SessionManager.h"
#include "Network/LogicQueue.h"

// ──────────────────────────────────────────────
//  상수
// ──────────────────────────────────────────────
inline constexpr uint16_t SERVER_PORT        = 9000;
inline constexpr uint32_t MAX_UDP_PAYLOAD    = 1472;   // 이더넷 MTU(1500) - IP(20) - UDP(8)
inline constexpr uint32_t RECV_BUFFER_COUNT  = 64;     // 사전 할당 수신 버퍼 수
inline constexpr uint32_t SEND_BUFFER_COUNT  = 64;     // 사전 할당 송신 버퍼 수 (부족 시 동적 증설)
inline constexpr uint32_t WORKER_THREAD_COUNT = 4;     // I/O Worker(생산자) 스레드 수 (2~4)
inline constexpr uint32_t LOGIC_THREAD_COUNT  = 4;     // Logic(소비자) 스레드 수
inline constexpr uint32_t BROADCAST_HZ        = 30;    // 월드 스테이트 브로드캐스트 주기(Hz)
inline constexpr int64_t  SESSION_TIMEOUT_MS  = 10000; // 무통신 세션 타임아웃 (하트비트 1Hz 기준 10초)

// ── RUDP(신뢰 UDP) 타이밍 ─────────────────────
inline constexpr int64_t  RUDP_TICK_MS         = 50;    // Main 루프 틱 주기(재전송 스캔 해상도)
inline constexpr int64_t  RUDP_RETRANS_TIMEOUT_MS = 200;// ACK 미수신 시 재전송 대기 시간
inline constexpr uint32_t RUDP_MAX_RETRIES     = 10;    // 최대 재전송 횟수 (초과 시 세션 포기)

// 패킷 매직 바이트 ("AR")
inline constexpr uint8_t MAGIC_BYTE_0 = 0x41;  // 'A'
inline constexpr uint8_t MAGIC_BYTE_1 = 0x52;  // 'R'

// ──────────────────────────────────────────────
//  IOCP 완료 키 및 오버랩 확장 구조체
// ──────────────────────────────────────────────

// IOCP 완료 포트에서 식별하는 완료 키 종류
enum class ECompletionKey : ULONG_PTR {
    SocketIo = 1,  // 소켓 I/O 완료 (Recv/Send 공통 - 종류는 오버랩의 Operation 으로 구분)
    Shutdown = 0   // Worker 스레드 종료 신호
};

// I/O 연산 종류.
//   UDP 는 하나의 소켓에서 Recv/Send 완료가 같은 완료 키로 올라오므로,
//   Worker 스레드가 완료된 오버랩이 수신인지 송신인지 구분할 수 있도록
//   모든 오버랩 구조체 선두에 이 태그를 둡니다.
enum class EIoOperation : uint8_t {
    Recv = 0,
    Send = 1
};

// 모든 오버랩 확장 구조체의 공통 선두부.
//   Overlapped 가 offset 0 이므로 GQCS 가 돌려주는 OVERLAPPED* 를
//   FIoOverlappedBase* / 각 파생 구조체* 로 안전하게 캐스팅할 수 있습니다.
struct FIoOverlappedBase {
    WSAOVERLAPPED Overlapped{};   // 반드시 첫 번째 멤버
    EIoOperation  Operation;      // Recv / Send 구분 태그
};

// WSARecvFrom 에 넘기는 수신용 오버랩 구조체
struct FRecvOverlapped {
    FIoOverlappedBase Base{ {}, EIoOperation::Recv };
    WSABUF          WsaBuf{};              // 버퍼 디스크립터
    char            Buffer[MAX_UDP_PAYLOAD]{};  // 실제 수신 데이터
    SOCKADDR_IN     RemoteAddr{};          // 송신자 주소
    INT             RemoteAddrLen{ sizeof(SOCKADDR_IN) };
};

// WSASendTo 에 넘기는 송신용 오버랩 구조체
//   송신 버퍼 풀에서 획득 → 데이터 복사 → WSASendTo →
//   완료 통지 시 다시 풀로 반납하는 재사용 사이클을 가집니다.
struct FSendOverlapped {
    FIoOverlappedBase Base{ {}, EIoOperation::Send };
    WSABUF          WsaBuf{};              // 버퍼 디스크립터
    char            Buffer[MAX_UDP_PAYLOAD]{};  // 전송 데이터
    SOCKADDR_IN     RemoteAddr{};          // 목적지 주소
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

    /**
     * @brief 임의의 목적지로 원시 바이트를 비동기 전송(WSASendTo)합니다.
     *        송신 풀에서 버퍼를 획득해 데이터를 복사한 뒤 IOCP 로 위임하며,
     *        완료 처리(버퍼 반납)는 Worker 스레드가 담당합니다.
     * @return 송신 요청을 정상 큐잉했으면 true
     */
    bool SendTo(const SOCKADDR_IN& to, const void* data, int len);

    /** @brief 세션의 원격 주소로 비동기 전송. */
    bool SendToSession(const Session& session, const void* data, int len);

    /**
     * @brief 세션으로 '신뢰' 패킷을 전송합니다.
     *        헤더에 송신 SeqNum/RELIABLE 플래그를 채우고 복사본을 재전송 큐에
     *        보관한 뒤 전송합니다. ACK가 올 때까지 타이머가 재전송을 담당합니다.
     * @param type    EPacketType 값
     * @param body    바디 포인터 (헤더 제외)
     * @param bodyLen 바디 길이
     */
    bool SendReliableToSession(Session& session, uint8_t type, const void* body, int bodyLen);

private:
    // 세션 스마트 포인터 별칭 (핸들러/디스패처에서 공용 사용)
    using SessionPtr = SessionManager::SessionPtr;

    // ── 초기화 헬퍼 ────────────────────────────
    bool InitWinsock();
    bool CreateSocket();
    bool BindSocket(uint16_t port);
    bool CreateIOCP();
    bool AssociateSocketWithIOCP();
    bool PostInitialRecvs();
    bool InitSendPool();

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

    // ── 비동기 송신 ────────────────────────────
    /**
     * @brief 송신 완료 후 처리(버퍼 풀 반납)를 Worker 스레드에서 수행.
     * @param pOvlp 완료된 FSendOverlapped 포인터
     */
    void OnSendCompleted(FSendOverlapped* pOvlp);

    /** @brief 송신 풀에서 재사용 가능한 오버랩을 획득 (없으면 동적 증설). */
    FSendOverlapped* AcquireSendOverlapped();

    /** @brief 사용이 끝난 송신 오버랩을 풀로 반납. */
    void ReleaseSendOverlapped(FSendOverlapped* pOvlp);

    // ── 응답 패킷 헬퍼 ─────────────────────────
    /** @brief 누적 ACK(cumulativeAck 까지 순서대로 수신 완료)를 세션으로 전송. */
    void SendAck(const Session& session, uint32_t cumulativeAck);

    // ── 패킷 Dispatcher ───────────────────────
    /**
     * @brief 수신 버퍼에서 헤더를 검증하고 세션을 해석한 뒤, RUDP 계층
     *        (누적 ACK 처리 / 신뢰 스트림 재조립 / 비신뢰 스테일 Drop)을
     *        거쳐 순서가 확정된 패킷만 DispatchApp 으로 올려보냅니다.
     */
    void DispatchPacket(const char* data, DWORD byteLen, const SOCKADDR_IN& senderAddr);

    /**
     * @brief 순서가 확정된 패킷을 게임 로직 큐(LogicQueue)에 밀어 넣습니다.
     *        (생산자 측: I/O Worker 스레드에서 호출)
     */
    void EnqueueLogic(const SessionPtr& session, std::vector<uint8_t>&& packet);

    /**
     * @brief Logic(소비자) 스레드 진입점 (세션 어피니티).
     *        자신에게 할당된 queueIndex 번 큐에서만 항목을 꺼내 DispatchApp
     *        으로 게임 로직을 실행합니다. 네트워크 I/O 에는 관여하지 않습니다.
     * @param queueIndex 이 스레드가 전담하는 LogicQueue 인덱스
     */
    void LogicThread(uint32_t queueIndex);

    /**
     * @brief 헤더 검증/RUDP 처리가 끝난 패킷을 타입별 핸들러로 분기합니다.
     *        (소비자 측: Logic 스레드에서 호출. 재조립으로 순서가 확정된
     *         신뢰 패킷도 이 경로로 재생됩니다)
     */
    void DispatchApp(const char* data, DWORD byteLen, const SessionPtr& session);

    // ── RUDP 재전송 타이머 ────────────────────
    /** @brief 모든 세션의 재전송 큐를 스캔해 타임아웃분을 재전송합니다. */
    void RetransmitSweep(int64_t nowMs);

    // ── 월드 스테이트 브로드캐스트 (전담 스레드) ──
    /** @brief BROADCAST_HZ 주기로 BroadcastTick 을 구동하는 전담 스레드. */
    void BroadcastLoop();

    /**
     * @brief 한 틱 분량의 브로드캐스트 수행.
     *        (1) 전 세션 트랜스폼을 lock-free 로 1회 수집(스냅샷)
     *        (2) 세션락 해제 후 유저별 Era-필터 집계 패킷을 전송
     */
    void BroadcastTick();

    // ── 패킷 핸들러 (stub) ────────────────────
    //   Dispatcher 가 미리 해석한 세션을 함께 전달합니다.
    //   (주소·UserId 등은 session 객체에서 조회)
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

    // I/O Worker(생산자) 스레드 풀
    std::vector<std::thread> m_WorkerThreads;

    // Logic(소비자) 스레드 풀 + 세션 어피니티용 큐 배열.
    //   각 로직 스레드는 동일 인덱스의 큐만 전담하며, 특정 세션의 패킷은
    //   항상 (SessionId % LOGIC_THREAD_COUNT) 로 결정된 하나의 큐/스레드에서만
    //   처리되므로 세션별 처리 순서가 보존됩니다.
    //   (LogicQueue 는 복사 불가 → unique_ptr 로 소유)
    std::vector<std::thread>                 m_LogicThreads;
    std::vector<std::unique_ptr<LogicQueue>> m_LogicQueues;

    // 월드 스테이트 브로드캐스트 전담 스레드
    std::thread m_BroadcastThread;

    // 사전 할당된 수신 오버랩 버퍼 풀 (RECV_BUFFER_COUNT 개)
    std::vector<std::unique_ptr<FRecvOverlapped>> m_RecvPool;

    // ── 송신 버퍼 풀 (여러 Worker 스레드가 동시 획득/반납) ──
    //   m_SendPool     : 오버랩 객체의 소유권 보관 (해제 책임)
    //   m_SendFreeList : 재사용 가능한(반납된) 오버랩의 free list
    //   m_SendPoolMutex: 두 컨테이너 보호
    std::vector<std::unique_ptr<FSendOverlapped>> m_SendPool;
    std::vector<FSendOverlapped*>                 m_SendFreeList;
    std::mutex                                    m_SendPoolMutex;

    // 접속 세션 관리자 (여러 Worker 스레드가 공유 접근)
    SessionManager m_SessionManager;
};
