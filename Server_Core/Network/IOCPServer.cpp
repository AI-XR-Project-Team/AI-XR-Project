// ============================================================
//  IOCPServer.cpp
//  역할: IOCP 기반 UDP 서버 구현부
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Network/IOCPServer.h"
#include "Utils/GLog.h"

#include <cassert>
#include <cstring>

// ──────────────────────────────────────────────
//  생성자 / 소멸자
// ──────────────────────────────────────────────

IOCPServer::IOCPServer() = default;

IOCPServer::~IOCPServer()
{
    Shutdown();
}

// ──────────────────────────────────────────────
//  Public API
// ──────────────────────────────────────────────

bool IOCPServer::Start(uint16_t port)
{
    if (!InitWinsock())       return false;
    if (!CreateSocket())      return false;
    if (!BindSocket(port))    return false;
    if (!CreateIOCP())        return false;
    if (!AssociateSocketWithIOCP()) return false;
    if (!InitSendPool())      return false;

    m_bRunning.store(true, std::memory_order_release);

    // ── I/O Worker(생산자) 스레드 풀 생성 ──────
    m_WorkerThreads.reserve(WORKER_THREAD_COUNT);
    for (uint32_t i = 0; i < WORKER_THREAD_COUNT; ++i)
    {
        m_WorkerThreads.emplace_back(&IOCPServer::WorkerThread, this);
    }
    GLog::Info("[Server] {} I/O Worker thread(s) started.", WORKER_THREAD_COUNT);

    // ── Logic(소비자) 큐 배열 + 스레드 풀 생성 ──
    //   세션 어피니티: 큐/스레드를 LOGIC_THREAD_COUNT 만큼 1:1 로 만들고,
    //   각 스레드는 자신과 같은 인덱스의 큐만 전담합니다.
    //   패킷 도착 전(PostInitialRecvs 이전)에 먼저 띄워 즉시 처리되게 합니다.
    m_LogicQueues.reserve(LOGIC_THREAD_COUNT);
    for (uint32_t i = 0; i < LOGIC_THREAD_COUNT; ++i)
    {
        m_LogicQueues.push_back(std::make_unique<LogicQueue>());
    }

    m_LogicThreads.reserve(LOGIC_THREAD_COUNT);
    for (uint32_t i = 0; i < LOGIC_THREAD_COUNT; ++i)
    {
        m_LogicThreads.emplace_back(&IOCPServer::LogicThread, this, i);
    }
    GLog::Info("[Server] {} Logic thread(s)/queue(s) started.", LOGIC_THREAD_COUNT);

    // ── 월드 스테이트 브로드캐스트 스레드 기동 ──
    m_BroadcastThread = std::thread(&IOCPServer::BroadcastLoop, this);
    GLog::Info("[Server] Broadcast thread started ({}Hz).", BROADCAST_HZ);

    // ── 초기 비동기 수신 등록 ──────────────────
    if (!PostInitialRecvs()) return false;

    GLog::Info("[Server] Listening on UDP port {}.", port);
    return true;
}

void IOCPServer::Shutdown()
{
    // 이미 종료된 경우 중복 호출 방지
    bool expected = true;
    if (!m_bRunning.compare_exchange_strong(expected, false,
            std::memory_order_acq_rel, std::memory_order_acquire))
    {
        return;
    }

    GLog::Warn("[Server] Shutting down...");

    // Worker 스레드에 종료 신호 전달
    // IOCP 완료 키 0 (ECompletionKey::Shutdown) 을 스레드 수만큼 POST
    for (size_t i = 0; i < m_WorkerThreads.size(); ++i)
    {
        ::PostQueuedCompletionStatus(
            m_hIOCP,
            0,
            static_cast<ULONG_PTR>(ECompletionKey::Shutdown),
            nullptr
        );
    }

    // I/O Worker(생산자) 스레드 종료 대기 → 이후 큐로 신규 유입 없음
    for (auto& t : m_WorkerThreads)
    {
        if (t.joinable())
            t.join();
    }
    m_WorkerThreads.clear();

    // Logic(소비자) 스레드 종료: 모든 큐에 종료 신호 → 잔여 항목 소진 후 자연 종료
    for (auto& pQueue : m_LogicQueues)
    {
        pQueue->Shutdown();
    }
    for (auto& t : m_LogicThreads)
    {
        if (t.joinable())
            t.join();
    }
    m_LogicThreads.clear();
    m_LogicQueues.clear();

    // 브로드캐스트 스레드 종료 대기 (m_bRunning=false 로 다음 틱에서 자연 종료)
    if (m_BroadcastThread.joinable())
        m_BroadcastThread.join();

    // 소켓 닫기
    if (m_Socket != INVALID_SOCKET)
    {
        ::closesocket(m_Socket);
        m_Socket = INVALID_SOCKET;
    }

    // IOCP 핸들 닫기
    if (m_hIOCP != nullptr)
    {
        ::CloseHandle(m_hIOCP);
        m_hIOCP = nullptr;
    }

    // 수신 풀 해제 (unique_ptr 이 자동 해제)
    m_RecvPool.clear();

    // 송신 풀 해제 (Worker 합류 + 소켓 종료 이후이므로 in-flight 송신 없음)
    {
        std::lock_guard<std::mutex> lock(m_SendPoolMutex);
        m_SendFreeList.clear();
        m_SendPool.clear();
    }

    ::WSACleanup();
    GLog::Info("[Server] Shutdown complete.");
}

void IOCPServer::RunRecvLoop()
{
    // NOTE: 이 설계에서는 WSARecvFrom 을 PostRecv 로 IOCP에 등록하고,
    //       실제 완료 처리는 Worker 스레드가 담당합니다.
    //       Main 스레드는 이 함수를 통해 종료 신호를 대기하거나
    //       추후 추가 로직(통계, 타임아웃 관리 등)을 수행할 수 있습니다.
    GLog::Info("[Server] Main recv loop running. Press Ctrl+C to stop.");

    int64_t lastTimeoutSweepMs = Session::NowMs();

    while (m_bRunning.load(std::memory_order_acquire))
    {
        // RUDP 재전송 해상도에 맞춰 짧은 주기로 틱
        std::this_thread::sleep_for(std::chrono::milliseconds(RUDP_TICK_MS));

        const int64_t nowMs = Session::NowMs();

        // (1) 매 틱: 재전송 큐 스캔 → 타임아웃분 재전송
        RetransmitSweep(nowMs);

        // (2) 1초마다: 무통신 세션 정리 (마지막 통신 후 SESSION_TIMEOUT_MS 초과)
        if (nowMs - lastTimeoutSweepMs >= 1000)
        {
            lastTimeoutSweepMs = nowMs;
            const auto removed = m_SessionManager.RemoveTimedOut(SESSION_TIMEOUT_MS);
            if (!removed.empty())
            {
                GLog::Info("[Server] {} session(s) timed out. Active sessions: {}",
                    removed.size(), m_SessionManager.Count());
            }
        }
    }
}

// ──────────────────────────────────────────────
//  초기화 헬퍼
// ──────────────────────────────────────────────

bool IOCPServer::InitWinsock()
{
    WSADATA wsaData{};
    int result = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        GLog::Error("[Server] WSAStartup failed: {}", result);
        return false;
    }
    GLog::Info("[Server] Winsock2 initialized.");
    return true;
}

bool IOCPServer::CreateSocket()
{
    // WSA_FLAG_OVERLAPPED: IOCP 비동기 I/O 필수 플래그
    m_Socket = ::WSASocketW(
        AF_INET,
        SOCK_DGRAM,
        IPPROTO_UDP,
        nullptr,
        0,
        WSA_FLAG_OVERLAPPED
    );

    if (m_Socket == INVALID_SOCKET)
    {
        GLog::Error("[Server] WSASocket failed: {}", ::WSAGetLastError());
        return false;
    }
    GLog::Info("[Server] UDP socket created.");
    return true;
}

bool IOCPServer::BindSocket(uint16_t port)
{
    SOCKADDR_IN addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = ::htons(port);

    if (::bind(m_Socket, reinterpret_cast<SOCKADDR*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        GLog::Error("[Server] bind failed: {}", ::WSAGetLastError());
        return false;
    }
    GLog::Info("[Server] Socket bound to port {}.", port);
    return true;
}

bool IOCPServer::CreateIOCP()
{
    // NumberOfConcurrentThreads = 0 → CPU 코어 수에 따라 OS가 자동 결정
    m_hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (m_hIOCP == nullptr)
    {
        GLog::Error("[Server] CreateIoCompletionPort failed: {}", ::GetLastError());
        return false;
    }
    GLog::Info("[Server] IOCP handle created.");
    return true;
}

bool IOCPServer::AssociateSocketWithIOCP()
{
    HANDLE result = ::CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(m_Socket),
        m_hIOCP,
        static_cast<ULONG_PTR>(ECompletionKey::SocketIo),
        0
    );

    if (result == nullptr)
    {
        GLog::Error("[Server] Socket <-> IOCP association failed: {}", ::GetLastError());
        return false;
    }
    GLog::Info("[Server] Socket associated with IOCP.");
    return true;
}

bool IOCPServer::PostInitialRecvs()
{
    m_RecvPool.reserve(RECV_BUFFER_COUNT);
    for (uint32_t i = 0; i < RECV_BUFFER_COUNT; ++i)
    {
        auto pOvlp = std::make_unique<FRecvOverlapped>();
        PostRecv(pOvlp.get());
        m_RecvPool.push_back(std::move(pOvlp));
    }
    GLog::Info("[Server] {} async recv(s) posted.", RECV_BUFFER_COUNT);
    return true;
}

bool IOCPServer::InitSendPool()
{
    m_SendPool.reserve(SEND_BUFFER_COUNT);
    m_SendFreeList.reserve(SEND_BUFFER_COUNT);
    for (uint32_t i = 0; i < SEND_BUFFER_COUNT; ++i)
    {
        auto pOvlp = std::make_unique<FSendOverlapped>();
        m_SendFreeList.push_back(pOvlp.get());
        m_SendPool.push_back(std::move(pOvlp));
    }
    GLog::Info("[Server] Send pool initialized with {} buffer(s).", SEND_BUFFER_COUNT);
    return true;
}

// ──────────────────────────────────────────────
//  비동기 수신 등록
// ──────────────────────────────────────────────

void IOCPServer::PostRecv(FRecvOverlapped* pOvlp)
{
    assert(pOvlp != nullptr);

    // 구조체 재사용 시 초기화 (단, Buffer 내용은 덮어써지므로 불필요)
    ::ZeroMemory(&pOvlp->Base.Overlapped, sizeof(WSAOVERLAPPED));
    pOvlp->Base.Operation = EIoOperation::Recv;
    pOvlp->WsaBuf.buf = pOvlp->Buffer;
    pOvlp->WsaBuf.len = MAX_UDP_PAYLOAD;
    pOvlp->RemoteAddrLen = sizeof(SOCKADDR_IN);

    DWORD flags = 0;
    int result = ::WSARecvFrom(
        m_Socket,
        &pOvlp->WsaBuf,
        1,                                          // 버퍼 개수
        nullptr,                                    // 동기 완료 바이트 (IOCP에선 nullptr 사용)
        &flags,
        reinterpret_cast<SOCKADDR*>(&pOvlp->RemoteAddr),
        &pOvlp->RemoteAddrLen,
        &pOvlp->Base.Overlapped,
        nullptr                                     // 완료 루틴 없음 (IOCP 방식)
    );

    if (result == SOCKET_ERROR)
    {
        const int err = ::WSAGetLastError();
        // WSA_IO_PENDING: 비동기 요청이 정상적으로 큐에 올라간 상태 (정상)
        if (err != WSA_IO_PENDING)
        {
            GLog::Error("[Server] WSARecvFrom failed: {}", err);
        }
    }
}

// ──────────────────────────────────────────────
//  비동기 송신
// ──────────────────────────────────────────────

FSendOverlapped* IOCPServer::AcquireSendOverlapped()
{
    std::lock_guard<std::mutex> lock(m_SendPoolMutex);

    if (!m_SendFreeList.empty())
    {
        FSendOverlapped* pOvlp = m_SendFreeList.back();
        m_SendFreeList.pop_back();
        return pOvlp;
    }

    // free list 소진 → 동적 증설 (부하 급증 시 ACK 유실 방지)
    // 주의: 실제 오버랩 객체는 힙에 있고 벡터엔 unique_ptr(포인터)만 저장되므로,
    //       벡터 재할당이 일어나도 in-flight 송신의 raw 포인터는 유효합니다.
    auto pNew = std::make_unique<FSendOverlapped>();
    FSendOverlapped* pOvlp = pNew.get();
    m_SendPool.push_back(std::move(pNew));
    GLog::Warn("[Server] Send pool grew to {} buffer(s).", m_SendPool.size());
    return pOvlp;
}

void IOCPServer::ReleaseSendOverlapped(FSendOverlapped* pOvlp)
{
    std::lock_guard<std::mutex> lock(m_SendPoolMutex);
    m_SendFreeList.push_back(pOvlp);
}

bool IOCPServer::SendTo(const SOCKADDR_IN& to, const void* data, int len)
{
    if (len <= 0 || len > static_cast<int>(MAX_UDP_PAYLOAD))
    {
        GLog::Warn("[Send] Invalid payload length: {}", len);
        return false;
    }
    if (!m_bRunning.load(std::memory_order_acquire))
        return false;

    // 1) 송신 버퍼 획득 및 데이터 준비
    FSendOverlapped* pOvlp = AcquireSendOverlapped();

    ::ZeroMemory(&pOvlp->Base.Overlapped, sizeof(WSAOVERLAPPED));
    pOvlp->Base.Operation = EIoOperation::Send;
    std::memcpy(pOvlp->Buffer, data, static_cast<size_t>(len));
    pOvlp->WsaBuf.buf = pOvlp->Buffer;
    pOvlp->WsaBuf.len = static_cast<ULONG>(len);
    pOvlp->RemoteAddr = to;

    // 2) 비동기 송신 등록
    DWORD bytesSent = 0;
    int result = ::WSASendTo(
        m_Socket,
        &pOvlp->WsaBuf,
        1,
        &bytesSent,
        0,                                          // flags
        reinterpret_cast<const SOCKADDR*>(&pOvlp->RemoteAddr),
        sizeof(SOCKADDR_IN),
        &pOvlp->Base.Overlapped,
        nullptr                                     // 완료 루틴 없음 (IOCP 방식)
    );

    if (result == SOCKET_ERROR)
    {
        const int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            // 진짜 실패: 이 경우 완료 통지가 IOCP 로 오지 않으므로 즉시 반납
            GLog::Error("[Send] WSASendTo failed: {}", err);
            ReleaseSendOverlapped(pOvlp);
            return false;
        }
    }

    // 즉시 완료(result==0) 또는 WSA_IO_PENDING:
    //   두 경우 모두 완료 패킷이 IOCP 에 큐잉되므로(SKIP_ON_SUCCESS 미설정),
    //   버퍼 반납은 Worker 스레드의 OnSendCompleted 에서 수행합니다.
    return true;
}

bool IOCPServer::SendToSession(const Session& session, const void* data, int len)
{
    return SendTo(session.GetRemoteAddr(), data, len);
}

void IOCPServer::OnSendCompleted(FSendOverlapped* pOvlp)
{
    // 전송 완료 → 버퍼를 풀로 반납하여 재사용
    // (필요 시 여기서 송신 통계/완료 로그를 남길 수 있음)
    ReleaseSendOverlapped(pOvlp);
}

// ──────────────────────────────────────────────
//  Worker 스레드
// ──────────────────────────────────────────────

void IOCPServer::WorkerThread()
{
    GLog::Debug("[Worker] Thread {:x} started.",
        std::hash<std::thread::id>{}(std::this_thread::get_id()));

    while (true)
    {
        DWORD       bytesTransferred = 0;
        ULONG_PTR   completionKey    = 0;
        OVERLAPPED* pRawOvlp        = nullptr;

        BOOL success = ::GetQueuedCompletionStatus(
            m_hIOCP,
            &bytesTransferred,
            &completionKey,
            &pRawOvlp,
            INFINITE
        );

        // ── 종료 신호 확인 ─────────────────────
        if (completionKey == static_cast<ULONG_PTR>(ECompletionKey::Shutdown))
        {
            GLog::Debug("[Worker] Thread {:x} received shutdown signal.",
                std::hash<std::thread::id>{}(std::this_thread::get_id()));
            break;
        }

        // ── 완료 패킷 자체가 없는 GQCS 실패 ────
        //   (pRawOvlp == nullptr 이면 어떤 I/O 도 회수할 수 없음)
        if (pRawOvlp == nullptr)
        {
            const DWORD err = ::GetLastError();
            // 소켓 종료/강제 닫힘 시 루프 탈출
            if (err == ERROR_OPERATION_ABORTED || err == ERROR_ABANDONED_WAIT_0)
                break;

            GLog::Error("[Worker] GQCS failed (no overlapped): {}", err);
            continue;
        }

        // ── 완료된 I/O 종류 판별 (Recv / Send) ──
        //   Base 가 각 구조체의 offset 0 이므로 캐스팅이 안전합니다.
        auto* pBase = reinterpret_cast<FIoOverlappedBase*>(pRawOvlp);

        switch (pBase->Operation)
        {
            case EIoOperation::Recv:
            {
                auto* pOvlp = reinterpret_cast<FRecvOverlapped*>(pRawOvlp);

                // 정상 수신 시에만 디스패치 (실패 시 버퍼만 재사용)
                if (success && bytesTransferred > 0)
                {
                    OnRecvCompleted(pOvlp, bytesTransferred);
                }

                // 버퍼 재사용: 다음 수신 등록
                if (m_bRunning.load(std::memory_order_acquire))
                {
                    PostRecv(pOvlp);
                }
                break;
            }
            case EIoOperation::Send:
            {
                auto* pOvlp = reinterpret_cast<FSendOverlapped*>(pRawOvlp);

                if (!success)
                {
                    GLog::Warn("[Worker] Async send failed: {}", ::GetLastError());
                }

                // 성공/실패 무관하게 버퍼는 반드시 풀로 반납 (누수 방지)
                OnSendCompleted(pOvlp);
                break;
            }
            default:
            {
                GLog::Error("[Worker] Unknown IO operation: {}",
                    static_cast<int>(pBase->Operation));
                break;
            }
        }
    }
}

// ──────────────────────────────────────────────
//  수신 완료 처리
// ──────────────────────────────────────────────

void IOCPServer::OnRecvCompleted(FRecvOverlapped* pOvlp, DWORD bytes)
{
    DispatchPacket(pOvlp->Buffer, bytes, pOvlp->RemoteAddr);
}

// ──────────────────────────────────────────────
//  패킷 Dispatcher
// ──────────────────────────────────────────────

void IOCPServer::DispatchPacket(const char* data, DWORD byteLen, const SOCKADDR_IN& senderAddr)
{
    // ── 1. 최소 길이 검사 ──────────────────────
    if (byteLen < sizeof(FPacketHeader))
    {
        GLog::Warn("[Dispatcher] Packet too short: {} bytes", byteLen);
        return;
    }

    // ── 2. 헤더 역직렬화 ──────────────────────
    FPacketHeader header{};
    std::memcpy(&header, data, sizeof(FPacketHeader));

    // ── 3. Magic 바이트 검증 ("AR") ───────────
    if (header.Magic[0] != MAGIC_BYTE_0 || header.Magic[1] != MAGIC_BYTE_1)
    {
        GLog::Warn("[Dispatcher] Invalid magic bytes: 0x{:02X} 0x{:02X}",
            header.Magic[0], header.Magic[1]);
        return;
    }

    // ── 4. 버전 검사 ──────────────────────────
    if (header.Version != 0x01)
    {
        GLog::Warn("[Dispatcher] Unsupported version: 0x{:02X}", header.Version);
        return;
    }

    // ── 5. Body 길이 검증 ─────────────────────
    if (byteLen < sizeof(FPacketHeader) + header.BodyLen)
    {
        GLog::Warn("[Dispatcher] Truncated body: got {} expected {}",
            byteLen, sizeof(FPacketHeader) + header.BodyLen);
        return;
    }

    // ── 6. 패킷 타입/신뢰 여부 결정 ───────────
    const auto packetType = static_cast<EPacketType>(header.Type);
    const bool bReliable  = (header.Flags & PKT_FLAG_RELIABLE) != 0;

    // ── 7. 세션 해석 ──────────────────────────
    //   JOIN 은 세션 생성 계기이므로 UserId 를 읽어 세션을 확보하고,
    //   그 외 패킷은 주소로 조회(없으면 드롭)합니다.
    SessionPtr session;
    if (packetType == EPacketType::PKT_JOIN)
    {
        if (byteLen < sizeof(FJoinPacket))
        {
            GLog::Warn("[Dispatcher] PKT_JOIN too short: {} bytes", byteLen);
            return;
        }
        const auto* jp = reinterpret_cast<const FJoinPacket*>(data);
        session = m_SessionManager.CreateOrGet(jp->UserId, senderAddr);
    }
    else
    {
        session = m_SessionManager.FindByAddr(senderAddr);
        if (!session)
        {
            char addrBuf[INET_ADDRSTRLEN]{};
            ::inet_ntop(AF_INET, &senderAddr.sin_addr, addrBuf, sizeof(addrBuf));
            GLog::Warn("[Dispatcher] Packet 0x{:02X} from unknown sender {}:{} dropped (JOIN required).",
                header.Type, addrBuf, ::ntohs(senderAddr.sin_port));
            return;
        }
    }

    // 통신 발생 → 타임아웃 타이머 갱신
    session->Touch();

    // ── 8. 상대의 누적 ACK 반영 (모든 패킷 piggyback) ──
    //   클라이언트가 알려준 "여기까지 받았다" 번호로 서버 재전송 큐를 정리.
    session->Rudp().OnAckReceived(header.AckNum);

    // ── 9. RUDP 계층 라우팅 → 로직 큐로 위임(생산자) ──
    //   여기서는 게임 로직을 실행하지 않고, 순서가 확정된(또는 정상 비신뢰)
    //   패킷만 LogicQueue 에 Push 합니다. 실제 Handle* 실행은 소비자 몫입니다.
    if (bReliable)
    {
        // 신뢰 스트림: 중복/역전 Drop + 순서 재조립 → 순서 확정분만 큐로
        std::vector<std::vector<uint8_t>> delivered;
        const bool accepted = session->Rudp().OnReliableReceived(
            header.SeqNum,
            reinterpret_cast<const uint8_t*>(data),
            static_cast<int>(byteLen),
            delivered);

        for (auto& bytes : delivered)
        {
            EnqueueLogic(session, std::move(bytes));
        }

        // 누적 ACK 회신은 전송 계층 책임 → 로직 처리를 기다리지 않고 즉시 회신
        //   (수용/드롭과 무관: 상대가 이전 ACK를 놓쳤을 수 있음)
        SendAck(*session, session->Rudp().GetCumulativeAck());

        if (!accepted)
        {
            GLog::Debug("[Dispatcher] Reliable dup/stale dropped. Seq={}", header.SeqNum);
        }
    }
    else
    {
        // 비신뢰: Transform(30Hz) 만 순서 기반 스테일/중복 Drop (재전송/재조립 없음)
        if (packetType == EPacketType::PKT_TRANSFORM &&
            !session->Rudp().AcceptUnreliable(header.SeqNum))
        {
            GLog::Debug("[Dispatcher] Stale transform dropped. Seq={}", header.SeqNum);
            return;
        }
        // 수신 버퍼는 곧 PostRecv 로 재사용되므로 복사본을 큐잉
        EnqueueLogic(session,
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(data),
                                 reinterpret_cast<const uint8_t*>(data) + byteLen));
    }
}

// ──────────────────────────────────────────────
//  생산자 → 소비자 큐잉 / 소비자 루프
// ──────────────────────────────────────────────

void IOCPServer::EnqueueLogic(const SessionPtr& session, std::vector<uint8_t>&& packet)
{
    FLogicWorkItem item;
    item.SessionRef = session;
    item.Packet     = std::move(packet);

    // 세션 어피니티 라우팅: 같은 세션은 항상 같은 큐(=같은 로직 스레드)로.
    //   → 세션별 패킷 처리 순서가 단일 스레드에서 직렬화되어 보존됨.
    const uint32_t targetQueueIdx = session->GetSessionId() % LOGIC_THREAD_COUNT;
    m_LogicQueues[targetQueueIdx]->Push(std::move(item));
}

void IOCPServer::LogicThread(uint32_t queueIndex)
{
    GLog::Debug("[Logic] Thread {:x} started (queue #{}).",
        std::hash<std::thread::id>{}(std::this_thread::get_id()), queueIndex);

    LogicQueue& queue = *m_LogicQueues[queueIndex];

    FLogicWorkItem item;
    while (queue.Pop(item))
    {
        // 순수 게임 로직 처리 (네트워크 I/O 관여 없음)
        DispatchApp(reinterpret_cast<const char*>(item.Packet.data()),
                    static_cast<DWORD>(item.Packet.size()), item.SessionRef);
    }

    GLog::Debug("[Logic] Thread {:x} exiting (queue #{}).",
        std::hash<std::thread::id>{}(std::this_thread::get_id()), queueIndex);
}

// ──────────────────────────────────────────────
//  응용 계층 Dispatcher (타입별 핸들러 분기)
//    헤더 검증 + RUDP 처리를 마친 패킷만 도달합니다.
//    재조립으로 순서가 확정된 신뢰 패킷도 이 경로로 재생됩니다.
// ──────────────────────────────────────────────

void IOCPServer::DispatchApp(const char* data, DWORD byteLen, const SessionPtr& session)
{
    FPacketHeader header{};
    std::memcpy(&header, data, sizeof(FPacketHeader));

    switch (static_cast<EPacketType>(header.Type))
    {
        case EPacketType::PKT_JOIN:
        {
            if (byteLen < sizeof(FJoinPacket)) break;
            FJoinPacket pkt{};
            std::memcpy(&pkt, data, sizeof(FJoinPacket));
            HandleJoin(pkt, session);
            break;
        }
        case EPacketType::PKT_LEAVE:
        {
            if (byteLen < sizeof(FLeavePacket)) break;
            FLeavePacket pkt{};
            std::memcpy(&pkt, data, sizeof(FLeavePacket));
            HandleLeave(pkt, session);
            break;
        }
        case EPacketType::PKT_TRANSFORM:
        {
            if (byteLen < sizeof(FSyncTransformPacket)) break;
            FSyncTransformPacket pkt{};
            std::memcpy(&pkt, data, sizeof(FSyncTransformPacket));
            HandleTransform(pkt, session);
            break;
        }
        case EPacketType::PKT_ERA_CHANGE:
        {
            if (byteLen < sizeof(FEraChangePacket)) break;
            FEraChangePacket pkt{};
            std::memcpy(&pkt, data, sizeof(FEraChangePacket));
            HandleEraChange(pkt, session);
            break;
        }
        case EPacketType::PKT_HEARTBEAT:
        {
            if (byteLen < sizeof(FHeartbeatPacket)) break;
            FHeartbeatPacket pkt{};
            std::memcpy(&pkt, data, sizeof(FHeartbeatPacket));
            HandleHeartbeat(pkt, session);
            break;
        }
        case EPacketType::PKT_AI_TRIGGER:
        {
            if (byteLen < sizeof(FAiTriggerPacket)) break;
            FAiTriggerPacket pkt{};
            std::memcpy(&pkt, data, sizeof(FAiTriggerPacket));
            HandleAiTrigger(pkt, session);
            break;
        }
        case EPacketType::PKT_ACK:
        {
            if (byteLen < sizeof(FAckPacket)) break;
            FAckPacket pkt{};
            std::memcpy(&pkt, data, sizeof(FAckPacket));
            HandleAck(pkt, session);
            break;
        }
        case EPacketType::PKT_SERVER_STATE:
        {
            if (byteLen < sizeof(FServerStatePacket)) break;
            FServerStatePacket pkt{};
            std::memcpy(&pkt, data, sizeof(FServerStatePacket));
            HandleServerState(pkt, session);
            break;
        }
        default:
        {
            GLog::Warn("[Dispatcher] Unknown packet type: 0x{:02X}", header.Type);
            break;
        }
    }
}

// ──────────────────────────────────────────────
//  패킷 핸들러 (Stub 구현 - 추후 로직 채워넣기)
// ──────────────────────────────────────────────

void IOCPServer::HandleJoin(const FJoinPacket& pkt, const SessionPtr& session)
{
    GLog::Info("[RX] PKT_JOIN       | SessionId={} UserId={} from {}",
        session->GetSessionId(), pkt.UserId, session->GetAddrString());
    // 세션 등록은 Dispatcher(CreateOrGet)에서 이미 완료됨.
    // TODO: JOIN 수락 응답 전송 및 SERVER_STATE 브로드캐스트
}

void IOCPServer::HandleLeave(const FLeavePacket& pkt, const SessionPtr& session)
{
    GLog::Info("[RX] PKT_LEAVE      | SessionId={} UserId={} Reason={}",
        session->GetSessionId(), pkt.UserId, pkt.Reason);
    // 정상 퇴장 → 세션 제거
    m_SessionManager.RemoveByUserId(session->GetUserId());
    // TODO: 잔여 참가자에게 이탈 브로드캐스트
}

void IOCPServer::HandleTransform(const FSyncTransformPacket& pkt, const SessionPtr& session)
{
    // 30Hz로 가장 빈번하게 호출 → GLog::Debug는 _DEBUG 빌드에서만 출력
    // (Transform 은 비신뢰 스트림: 스테일/중복은 Dispatcher 에서 이미 Drop 됨)
    GLog::Debug("[RX] PKT_TRANSFORM  | SessionId={} Seq={} Pos=({:.2f},{:.2f},{:.2f})",
        session->GetSessionId(), pkt.SeqNum, pkt.PosX, pkt.PosY, pkt.PosZ);

    // 세션 게임 상태에 최신 트랜스폼 발행 (Seqlock 쓰기 - owner 스레드 전용).
    //   어피니티로 이 세션은 단일 로직 스레드가 순서대로 처리하므로 안전.
    //   브로드캐스트 스레드가 이 값을 lock-free 로 읽어 주변 유저에게 뿌립니다.
    Session::FTransformState t;
    t.Seq    = pkt.SeqNum;
    t.Pos[0] = pkt.PosX; t.Pos[1] = pkt.PosY; t.Pos[2] = pkt.PosZ;
    t.Rot[0] = pkt.RotX; t.Rot[1] = pkt.RotY; t.Rot[2] = pkt.RotZ; t.Rot[3] = pkt.RotW;
    t.Vel[0] = pkt.VelX; t.Vel[1] = pkt.VelY; t.Vel[2] = pkt.VelZ;
    t.Era    = pkt.EraId;
    t.Flags  = pkt.Flags;
    session->PublishTransform(t);
}

void IOCPServer::HandleEraChange(const FEraChangePacket& pkt, const SessionPtr& session)
{
    GLog::Info("[RX] PKT_ERA_CHANGE | SessionId={} Era {} -> {}",
        session->GetSessionId(), pkt.FromEra, pkt.ToEra);
    // 세션 상태에 현재 시대 반영
    session->SetEra(pkt.ToEra);
    // TODO: SyncMode 에 따른 세션 내 시대 동기화 로직
}

void IOCPServer::HandleHeartbeat(const FHeartbeatPacket& pkt, const SessionPtr& session)
{
    // Touch() 는 Dispatcher 에서 이미 호출됨 → 타임아웃 타이머 갱신 완료
    // TODO: Pong 응답 전송 (ClientTime 에코)
}

void IOCPServer::HandleAiTrigger(const FAiTriggerPacket& pkt, const SessionPtr& session)
{
    GLog::Info("[RX] PKT_AI_TRIGGER | SessionId={} ObjectId={} GazeDuration={:.2f}s",
        session->GetSessionId(), pkt.ObjectId, pkt.GazeDuration);
    // TODO: Backend 서버로 AI 도슨트 호출 전달 로직
}

void IOCPServer::HandleAck(const FAckPacket& pkt, const SessionPtr& session)
{
    GLog::Info("[RX] PKT_ACK        | SessionId={} AckSeqNum={}",
        session->GetSessionId(), pkt.AckSeqNum);
    // TODO: RUDP 재전송 큐에서 해당 SeqNum 제거
}

void IOCPServer::HandleServerState(const FServerStatePacket& pkt, const SessionPtr& session)
{
    GLog::Info("[RX] PKT_SERVER_STATE | SessionId={} UserCount={}",
        pkt.SessionId, pkt.UserCount);
    // TODO: 서버 상태 패킷 수신 처리
}

// ──────────────────────────────────────────────
//  응답 패킷 헬퍼
// ──────────────────────────────────────────────

void IOCPServer::SendAck(const Session& session, uint32_t cumulativeAck)
{
    FAckPacket ack{};
    ack.Header.Magic[0] = MAGIC_BYTE_0;
    ack.Header.Magic[1] = MAGIC_BYTE_1;
    ack.Header.Version  = 0x01;
    ack.Header.Type     = static_cast<uint8_t>(EPacketType::PKT_ACK);
    ack.Header.BodyLen  = static_cast<uint16_t>(sizeof(FAckPacket) - sizeof(FPacketHeader));
    ack.Header.SeqNum   = 0;                       // ACK 패킷 자체는 비신뢰
    ack.Header.AckNum   = cumulativeAck;           // 누적 ACK (핵심 필드)
    ack.Header.Flags    = PKT_FLAG_NONE;
    ack.UserId    = session.GetUserId();
    ack.AckSeqNum = cumulativeAck;                 // 바디에도 동일 값(호환/가독성)

    SendToSession(session, &ack, sizeof(ack));
}

// ──────────────────────────────────────────────
//  신뢰 패킷 송신 + 재전송 큐 등록
// ──────────────────────────────────────────────

bool IOCPServer::SendReliableToSession(Session& session, uint8_t type, const void* body, int bodyLen)
{
    if (bodyLen < 0 || sizeof(FPacketHeader) + static_cast<size_t>(bodyLen) > MAX_UDP_PAYLOAD)
    {
        GLog::Warn("[Send] Reliable payload too large: bodyLen={}", bodyLen);
        return false;
    }

    const int total = static_cast<int>(sizeof(FPacketHeader)) + bodyLen;
    const uint32_t seq = session.Rudp().AssignSendSeq();

    // 전체 패킷 직렬화 (헤더 + 바디)
    std::vector<uint8_t> packet(static_cast<size_t>(total));

    FPacketHeader header{};
    header.Magic[0] = MAGIC_BYTE_0;
    header.Magic[1] = MAGIC_BYTE_1;
    header.Version  = 0x01;
    header.Type     = type;
    header.BodyLen  = static_cast<uint16_t>(bodyLen);
    header.SeqNum   = seq;
    header.AckNum   = session.Rudp().GetCumulativeAck();  // 인바운드 누적 ACK piggyback
    header.Flags    = PKT_FLAG_RELIABLE;

    std::memcpy(packet.data(), &header, sizeof(FPacketHeader));
    if (bodyLen > 0)
        std::memcpy(packet.data() + sizeof(FPacketHeader), body, static_cast<size_t>(bodyLen));

    // 재전송 큐에 복사본 보관 (ACK 수신 전까지 타이머가 재전송 담당)
    session.Rudp().OnReliableSent(seq, packet.data(), total, Session::NowMs());

    // 실제 비동기 전송
    return SendTo(session.GetRemoteAddr(), packet.data(), total);
}

// ──────────────────────────────────────────────
//  RUDP 재전송 타이머 (Main 스레드에서 주기 호출)
// ──────────────────────────────────────────────

void IOCPServer::RetransmitSweep(int64_t nowMs)
{
    // 최대 재시도를 초과해 포기해야 할 세션(UserId) 수집 → ForEach 종료 후 제거
    std::vector<uint32_t> deadSessions;

    // 주의: ForEach 는 SessionManager 를 shared_lock 으로 순회하므로
    //       콜백 내부에서 세션을 제거(unique_lock)하면 데드락 → 나중에 처리.
    m_SessionManager.ForEach([&](const SessionPtr& session)
    {
        std::vector<std::vector<uint8_t>> resend;
        std::vector<uint32_t>             deadSeqs;

        session->Rudp().CollectRetransmits(
            nowMs, RUDP_RETRANS_TIMEOUT_MS, RUDP_MAX_RETRIES, resend, deadSeqs);

        for (auto& bytes : resend)
        {
            SendTo(session->GetRemoteAddr(), bytes.data(), static_cast<int>(bytes.size()));
        }
        if (!resend.empty())
        {
            GLog::Debug("[Retransmit] SessionId={} resent {} packet(s).",
                session->GetSessionId(), resend.size());
        }

        if (!deadSeqs.empty())
        {
            GLog::Warn("[Retransmit] SessionId={} exceeded max retries on {} packet(s). Dropping session.",
                session->GetSessionId(), deadSeqs.size());
            deadSessions.push_back(session->GetUserId());
        }
    });

    for (uint32_t userId : deadSessions)
    {
        m_SessionManager.RemoveByUserId(userId);
    }
}

// ──────────────────────────────────────────────
//  월드 스테이트 브로드캐스트 (전담 스레드)
// ──────────────────────────────────────────────

void IOCPServer::BroadcastLoop()
{
    using namespace std::chrono;
    GLog::Debug("[Broadcast] Loop running at {}Hz.", BROADCAST_HZ);

    const auto period = duration_cast<steady_clock::duration>(
        duration<double>(1.0 / static_cast<double>(BROADCAST_HZ)));
    auto nextTick = steady_clock::now();

    while (m_bRunning.load(std::memory_order_acquire))
    {
        nextTick += period;
        BroadcastTick();
        std::this_thread::sleep_until(nextTick);
    }

    GLog::Debug("[Broadcast] Loop exiting.");
}

void IOCPServer::BroadcastTick()
{
    // 스냅샷 1건 = 한 세션의 전송에 필요한 최소 정보
    struct FPeer {
        uint32_t                UserId;
        SOCKADDR_IN             Addr;
        uint32_t                Ack;   // 이 세션의 인바운드 누적 ACK (piggyback용)
        Session::FTransformState T;
    };

    // ── (1) 짧은 shared_lock: 전 세션 스냅샷을 lock-free 로 수집 ──
    std::vector<FPeer> peers;
    peers.reserve(64);
    m_SessionManager.ForEach([&](const SessionPtr& s)
    {
        peers.push_back(FPeer{
            s->GetUserId(),
            s->GetRemoteAddr(),
            s->Rudp().GetCumulativeAck(),
            s->ReadTransform() });
    });
    // ← 여기서 SessionManager 락 해제 (Join/Leave 가 오래 막히지 않음)

    if (peers.empty())
        return;

    const uint64_t tickMs = static_cast<uint64_t>(Session::NowMs());

    // 한 프래그먼트(패킷)에 담을 수 있는 최대 엔트리 수
    constexpr size_t kMaxEntries =
        (MAX_UDP_PAYLOAD - sizeof(FPacketHeader) - sizeof(FWorldStateBody))
        / sizeof(FWorldStateEntry);
    static_assert(kMaxEntries > 0, "world-state header exceeds MTU");

    // ── (2) 락 밖: 수신자별 Era-필터 집계 패킷 조립 & 전송 ──
    std::vector<FWorldStateEntry> entries;
    entries.reserve(peers.size());

    for (const FPeer& me : peers)
    {
        // AOI: 같은 Era + 자기 자신 제외 + 트랜스폼을 1회 이상 보낸 유저만
        entries.clear();
        for (const FPeer& other : peers)
        {
            if (other.UserId == me.UserId) continue;   // 자기 자신
            if (other.T.Seq == 0)          continue;   // 아직 위치 미수신
            if (other.T.Era != me.T.Era)   continue;   // 다른 시대 → 렌더 불필요

            FWorldStateEntry e{};
            e.UserId = other.UserId;
            e.SeqNum = other.T.Seq;
            e.PosX = other.T.Pos[0]; e.PosY = other.T.Pos[1]; e.PosZ = other.T.Pos[2];
            e.RotX = other.T.Rot[0]; e.RotY = other.T.Rot[1];
            e.RotZ = other.T.Rot[2]; e.RotW = other.T.Rot[3];
            e.VelX = other.T.Vel[0]; e.VelY = other.T.Vel[1]; e.VelZ = other.T.Vel[2];
            e.Era   = other.T.Era;
            e.Flags = other.T.Flags;
            entries.push_back(e);
        }

        // 엔트리가 0개여도 1패킷은 전송 → 클라가 "이번 틱 이웃 없음"을 확정
        const size_t total = entries.size();
        const uint8_t fragCount = static_cast<uint8_t>(
            total == 0 ? 1 : (total + kMaxEntries - 1) / kMaxEntries);

        for (uint8_t frag = 0; frag < fragCount; ++frag)
        {
            const size_t start     = static_cast<size_t>(frag) * kMaxEntries;
            const size_t remaining = (total > start) ? (total - start) : 0;
            // std::min 은 <windows.h> 의 min 매크로와 충돌하므로 직접 계산
            const size_t count     = (remaining < kMaxEntries) ? remaining : kMaxEntries;

            const int bodyLen = static_cast<int>(
                sizeof(FWorldStateBody) + count * sizeof(FWorldStateEntry));

            FPacketHeader hdr{};
            hdr.Magic[0] = MAGIC_BYTE_0;
            hdr.Magic[1] = MAGIC_BYTE_1;
            hdr.Version  = 0x01;
            hdr.Type     = static_cast<uint8_t>(EPacketType::PKT_WORLD_STATE);
            hdr.BodyLen  = static_cast<uint16_t>(bodyLen);
            hdr.SeqNum   = 0;             // 비신뢰: 최신성은 ServerTimeMs 로 판단
            hdr.AckNum   = me.Ack;        // ★ 누적 ACK piggyback
            hdr.Flags    = PKT_FLAG_NONE;

            FWorldStateBody body{};
            body.ServerTimeMs = tickMs;
            body.EntryCount   = static_cast<uint16_t>(count);
            body.FragIndex    = frag;
            body.FragCount    = fragCount;

            char buf[MAX_UDP_PAYLOAD];
            std::memcpy(buf, &hdr, sizeof(hdr));
            std::memcpy(buf + sizeof(hdr), &body, sizeof(body));
            if (count > 0)
            {
                std::memcpy(buf + sizeof(hdr) + sizeof(body),
                            entries.data() + start,
                            count * sizeof(FWorldStateEntry));
            }

            SendTo(me.Addr, buf, static_cast<int>(sizeof(hdr)) + bodyLen);
        }
    }
}
