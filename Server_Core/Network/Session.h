#pragma once

// ============================================================
//  Session.h
//  역할: UDP 클라이언트의 "논리적 연결" 상태를 표현하는 객체
//
//  UDP는 연결 상태가 없으므로, 서버가 (IP:Port) 주소를 기준으로
//  클라이언트를 기억하기 위한 세션 객체를 유지합니다.
//
//  스레드 안전성:
//    - 여러 Worker 스레드가 동일 세션에 동시 접근할 수 있으므로
//      가변 상태(마지막 통신 시각, 시대)는 std::atomic 으로 보호합니다.
//    - 불변 식별 정보(SessionId/UserId/RemoteAddr)는 생성 후 변경되지
//      않으므로 별도 동기화가 필요 없습니다.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstring>
#include <atomic>
#include <chrono>
#include <string>
#include <type_traits>

#include "Network/RudpChannel.h"

class Session
{
public:
    Session(uint32_t sessionId, uint32_t userId, const SOCKADDR_IN& addr)
        : m_SessionId(sessionId)
        , m_UserId(userId)
        , m_RemoteAddr(addr)
    {
        Touch();  // 생성 시각을 마지막 통신 시각으로 초기화
    }

    // ── 불변 식별 정보 ─────────────────────────────
    uint32_t           GetSessionId()  const noexcept { return m_SessionId; }
    uint32_t           GetUserId()     const noexcept { return m_UserId; }
    const SOCKADDR_IN& GetRemoteAddr() const noexcept { return m_RemoteAddr; }

    // ── 마지막 통신 시각 (타임아웃 판단용) ─────────
    // 어떤 패킷이든 수신하면 호출하여 타이머를 갱신합니다.
    void Touch() noexcept
    {
        m_LastRecvMs.store(NowMs(), std::memory_order_release);
    }

    int64_t GetLastRecvMs() const noexcept
    {
        return m_LastRecvMs.load(std::memory_order_acquire);
    }

    // 마지막 통신 이후 경과 시간(ms)
    int64_t IdleMs() const noexcept
    {
        return NowMs() - GetLastRecvMs();
    }

    // ══════════════════════════════════════════════
    //  게임 상태 (Seqlock 으로 보호)
    //
    //  세션 어피니티 덕분에 이 세션의 상태를 쓰는 스레드는 항상 하나
    //  (전담 owner 로직 스레드)뿐입니다 → 라이터-라이터 락 불필요.
    //  읽기는 주로 브로드캐스트 스레드가 수행하며, Seqlock(버전 카운터)로
    //  뮤텍스 없이 lock-free 하게 일관된 스냅샷을 얻습니다.
    //
    //  m_TransformSeqLock: 짝수=안정, 홀수=쓰기 진행 중
    // ══════════════════════════════════════════════
    struct FTransformState {
        uint32_t Seq = 0;                       // 마지막 반영 트랜스폼 SeqNum (0=미수신)
        float    Pos[3] = { 0, 0, 0 };          // 위치
        float    Rot[4] = { 0, 0, 0, 1 };       // 회전(쿼터니언)
        float    Vel[3] = { 0, 0, 0 };          // 속도
        uint8_t  Era    = 0;                    // 현재 관람 시대
        uint8_t  Flags  = 0;                    // IS_MOVING/IS_GAZING ...
    };
    static_assert(std::is_trivially_copyable_v<FTransformState>,
                  "FTransformState must be trivially copyable for the seqlock memcpy path");

    // 트랜스폼 발행 (owner 스레드 전용 쓰기).
    //   RUDP 생산자(AcceptUnreliable)가 이미 스테일을 걸렀고, 어피니티로
    //   한 스레드가 순서대로 처리하므로 별도 역전 가드가 필요 없습니다.
    void PublishTransform(const FTransformState& t) noexcept
    {
        const uint32_t s = m_TransformSeqLock.load(std::memory_order_relaxed);
        m_TransformSeqLock.store(s + 1, std::memory_order_relaxed);   // 홀수: 쓰기 시작
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&m_Transform, &t, sizeof(FTransformState));       // 평문 POD 기록
        m_TransformSeqLock.store(s + 2, std::memory_order_release);   // 짝수: 발행
    }

    // 트랜스폼 읽기 (임의 스레드, lock-free 재시도).
    FTransformState ReadTransform() const noexcept
    {
        FTransformState out;
        uint32_t s1, s2;
        do {
            s1 = m_TransformSeqLock.load(std::memory_order_acquire);
            if (s1 & 1u) continue;                                    // 쓰기 중 → 재시도
            std::memcpy(&out, &m_Transform, sizeof(FTransformState));
            std::atomic_thread_fence(std::memory_order_acquire);
            s2 = m_TransformSeqLock.load(std::memory_order_relaxed);
        } while (s1 != s2);                                           // 도중 변경 → 재시도
        return out;
    }

    // ── 현재 관람 시대 (ERA_CHANGE 시 owner 스레드가 갱신) ──
    uint8_t GetEra() const noexcept { return ReadTransform().Era; }

    void SetEra(uint8_t era) noexcept
    {
        // owner 스레드 전용. Era 만 바꿔 Seqlock 프로토콜로 재발행.
        const uint32_t s = m_TransformSeqLock.load(std::memory_order_relaxed);
        m_TransformSeqLock.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        m_Transform.Era = era;
        m_TransformSeqLock.store(s + 2, std::memory_order_release);
    }

    // ── 로그용 주소 문자열 "ip:port" ───────────────
    std::string GetAddrString() const
    {
        char buf[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &m_RemoteAddr.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(::ntohs(m_RemoteAddr.sin_port));
    }

    // ── RUDP(신뢰 UDP) 채널 ────────────────────────
    // 이 세션의 신뢰성/순서 제어 상태 머신 (자체 mutex 로 스레드 안전)
    RudpChannel&       Rudp()       noexcept { return m_Rudp; }
    const RudpChannel& Rudp() const noexcept { return m_Rudp; }

    // 단조 증가 시계 기준 현재 시각(ms) - 벽시계 변경 영향 없음
    static int64_t NowMs() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    // ── 불변 (생성 후 변경 없음) ───────────────────
    const uint32_t    m_SessionId;
    const uint32_t    m_UserId;
    const SOCKADDR_IN m_RemoteAddr;

    // ── 전송 계층이 매 패킷 갱신하는 핫 필드 → atomic (lock-free) ──
    std::atomic<int64_t> m_LastRecvMs{ 0 };

    // ── 게임 상태 (Seqlock: 단일 라이터 + lock-free 리더) ──
    std::atomic<uint32_t> m_TransformSeqLock{ 0 };  // 짝수=안정, 홀수=쓰기중
    FTransformState       m_Transform{};            // 비원자 POD, Seqlock 보호

    // ── RUDP 채널 (내부적으로 mutex 보호) ──────────
    RudpChannel m_Rudp;
};
