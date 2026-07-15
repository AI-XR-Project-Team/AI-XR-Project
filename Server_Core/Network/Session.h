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
#include <atomic>
#include <chrono>
#include <string>

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

    // ── 현재 관람 시대 (ERA_CHANGE 시 갱신) ────────
    uint8_t GetEra() const noexcept
    {
        return m_EraId.load(std::memory_order_acquire);
    }

    void SetEra(uint8_t era) noexcept
    {
        m_EraId.store(era, std::memory_order_release);
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

    // ── 가변 (멀티스레드 접근 → atomic) ────────────
    std::atomic<int64_t> m_LastRecvMs{ 0 };
    std::atomic<uint8_t> m_EraId{ 0 };

    // ── RUDP 채널 (내부적으로 mutex 보호) ──────────
    RudpChannel m_Rudp;
};
