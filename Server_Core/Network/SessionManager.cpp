// ============================================================
//  SessionManager.cpp
//  역할: SessionManager 구현부 (shared_mutex 기반 동기화)
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Network/SessionManager.h"
#include "Utils/GLog.h"

// ──────────────────────────────────────────────
//  조회 (shared_lock — 다중 읽기 허용)
// ──────────────────────────────────────────────

SessionManager::SessionPtr SessionManager::FindByAddr(const SOCKADDR_IN& addr) const
{
    const uint64_t key = MakeKey(addr);

    std::shared_lock<std::shared_mutex> lock(m_Mutex);
    auto it = m_ByAddr.find(key);
    return (it != m_ByAddr.end()) ? it->second : nullptr;
}

SessionManager::SessionPtr SessionManager::FindByUserId(uint32_t userId) const
{
    std::shared_lock<std::shared_mutex> lock(m_Mutex);
    auto it = m_ByUserId.find(userId);
    return (it != m_ByUserId.end()) ? it->second : nullptr;
}

// ──────────────────────────────────────────────
//  등록 (unique_lock — 단독 쓰기)
// ──────────────────────────────────────────────

SessionManager::SessionPtr SessionManager::CreateOrGet(uint32_t userId, const SOCKADDR_IN& addr)
{
    const uint64_t key = MakeKey(addr);

    std::unique_lock<std::shared_mutex> lock(m_Mutex);

    // 1) 이미 같은 주소로 등록된 세션이 있으면 그대로 재사용 (JOIN 중복 수신 등)
    if (auto it = m_ByAddr.find(key); it != m_ByAddr.end())
    {
        return it->second;
    }

    // 2) 같은 UserId 가 '다른 주소'로 남아 있으면 재접속으로 간주하고 정리
    if (auto uit = m_ByUserId.find(userId); uit != m_ByUserId.end())
    {
        const SessionPtr& stale = uit->second;
        m_ByAddr.erase(MakeKey(stale->GetRemoteAddr()));
        m_ByUserId.erase(uit);
        GLog::Warn("[SessionMgr] UserId={} reconnected from a new address. Old session {} dropped.",
            userId, stale->GetSessionId());
    }

    // 3) 신규 세션 생성 및 두 인덱스에 등록
    const uint32_t sessionId = m_NextSessionId.fetch_add(1, std::memory_order_relaxed);
    auto session = std::make_shared<Session>(sessionId, userId, addr);

    m_ByAddr[key]       = session;
    m_ByUserId[userId]  = session;

    GLog::Info("[SessionMgr] Session created. SessionId={} UserId={} Addr={} (total={})",
        sessionId, userId, session->GetAddrString(), m_ByAddr.size());

    return session;
}

// ──────────────────────────────────────────────
//  제거 (unique_lock)
// ──────────────────────────────────────────────

bool SessionManager::RemoveByAddr(const SOCKADDR_IN& addr)
{
    const uint64_t key = MakeKey(addr);

    std::unique_lock<std::shared_mutex> lock(m_Mutex);
    auto it = m_ByAddr.find(key);
    if (it == m_ByAddr.end())
        return false;

    const SessionPtr session = it->second;
    m_ByUserId.erase(session->GetUserId());
    m_ByAddr.erase(it);

    GLog::Info("[SessionMgr] Session removed. SessionId={} UserId={} (total={})",
        session->GetSessionId(), session->GetUserId(), m_ByAddr.size());
    return true;
}

bool SessionManager::RemoveByUserId(uint32_t userId)
{
    std::unique_lock<std::shared_mutex> lock(m_Mutex);
    auto it = m_ByUserId.find(userId);
    if (it == m_ByUserId.end())
        return false;

    const SessionPtr session = it->second;
    m_ByAddr.erase(MakeKey(session->GetRemoteAddr()));
    m_ByUserId.erase(it);

    GLog::Info("[SessionMgr] Session removed. SessionId={} UserId={} (total={})",
        session->GetSessionId(), userId, m_ByAddr.size());
    return true;
}

// ──────────────────────────────────────────────
//  타임아웃 정리
// ──────────────────────────────────────────────

std::vector<SessionManager::SessionPtr> SessionManager::RemoveTimedOut(int64_t timeoutMs)
{
    std::vector<SessionPtr> removed;

    std::unique_lock<std::shared_mutex> lock(m_Mutex);
    for (auto it = m_ByAddr.begin(); it != m_ByAddr.end(); /* no ++ here */)
    {
        const SessionPtr& session = it->second;
        if (session->IdleMs() > timeoutMs)
        {
            removed.push_back(session);
            m_ByUserId.erase(session->GetUserId());
            it = m_ByAddr.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (const auto& s : removed)
    {
        GLog::Warn("[SessionMgr] Session timed out. SessionId={} UserId={} idle={}ms",
            s->GetSessionId(), s->GetUserId(), s->IdleMs());
    }

    return removed;
}

// ──────────────────────────────────────────────
//  조회/순회 유틸
// ──────────────────────────────────────────────

size_t SessionManager::Count() const
{
    std::shared_lock<std::shared_mutex> lock(m_Mutex);
    return m_ByAddr.size();
}

void SessionManager::ForEach(const std::function<void(const SessionPtr&)>& fn) const
{
    std::shared_lock<std::shared_mutex> lock(m_Mutex);
    for (const auto& [key, session] : m_ByAddr)
    {
        fn(session);
    }
}
