#pragma once

// ============================================================
//  SessionManager.h
//  역할: 접속 중인 모든 Session 을 관리하는 스레드 안전 컨테이너
//
//  동기화 모델:
//    - 여러 Worker 스레드가 동시에 접근하므로 std::shared_mutex 사용
//    - 조회(FindXxx/Count/ForEach)   : shared_lock  (다중 읽기 허용)
//    - 변경(CreateOrGet/Remove/Sweep): unique_lock  (단독 쓰기)
//
//  인덱스:
//    - m_ByAddr   : (IP:Port) 키 → 세션.  Dispatcher 의 주 조회 경로
//    - m_ByUserId : UserId    → 세션.  LEAVE/재접속 처리용 보조 인덱스
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>

#include <cstdint>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <functional>

#include "Network/Session.h"

class SessionManager
{
public:
    using SessionPtr = std::shared_ptr<Session>;

    // ── 조회 (읽기) ────────────────────────────────
    /** @brief 송신자 주소로 세션 조회. 없으면 nullptr. */
    SessionPtr FindByAddr(const SOCKADDR_IN& addr) const;

    /** @brief UserId 로 세션 조회. 없으면 nullptr. */
    SessionPtr FindByUserId(uint32_t userId) const;

    // ── 등록 (쓰기) ────────────────────────────────
    /**
     * @brief 주소에 대한 세션을 반환. 없으면 새로 생성하여 등록.
     *        같은 UserId 가 다른 주소로 남아 있으면(재접속) 기존 세션을 정리.
     * @return 기존 또는 새로 생성된 세션 (항상 non-null)
     */
    SessionPtr CreateOrGet(uint32_t userId, const SOCKADDR_IN& addr);

    // ── 제거 (쓰기) ────────────────────────────────
    /** @brief 주소 기준 세션 제거. 제거했으면 true. */
    bool RemoveByAddr(const SOCKADDR_IN& addr);

    /** @brief UserId 기준 세션 제거. 제거했으면 true. */
    bool RemoveByUserId(uint32_t userId);

    /**
     * @brief 마지막 통신 후 timeoutMs 를 초과한 세션을 일괄 제거.
     * @return 제거된 세션 목록 (호출자가 후처리/로그에 활용)
     */
    std::vector<SessionPtr> RemoveTimedOut(int64_t timeoutMs);

    // ── 조회/순회 유틸 ─────────────────────────────
    /** @brief 현재 세션 수. */
    size_t Count() const;

    /**
     * @brief 모든 세션을 순회 (읽기 잠금 하에서 실행).
     *        브로드캐스트 등에 사용. fn 내부에서 SessionManager 의
     *        변경 API(Create/Remove)를 호출하면 데드락 → 금지.
     */
    void ForEach(const std::function<void(const SessionPtr&)>& fn) const;

private:
    // (IP:Port) → 64bit 유니크 키.
    // s_addr(네트워크 바이트 32bit)와 sin_port(네트워크 바이트 16bit)를
    // 그대로 결합해도 엔드포인트별로 유일하므로 바이트 변환 불필요.
    static uint64_t MakeKey(const SOCKADDR_IN& addr) noexcept
    {
        return (static_cast<uint64_t>(addr.sin_addr.s_addr) << 16)
             | static_cast<uint64_t>(addr.sin_port);
    }

    mutable std::shared_mutex                m_Mutex;
    std::unordered_map<uint64_t, SessionPtr> m_ByAddr;    // 주소키 → 세션
    std::unordered_map<uint32_t, SessionPtr> m_ByUserId;  // UserId → 세션

    std::atomic<uint32_t> m_NextSessionId{ 1 };  // 서버 발급 세션 ID (1부터)
};
