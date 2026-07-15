#pragma once

// ============================================================
//  LogicQueue.h
//  역할: I/O 계층(생산자)과 게임 로직 계층(소비자)을 분리하는
//        스레드 안전 MPMC(Multi-Producer Multi-Consumer) 패킷 큐
//
//  흐름:
//    [I/O Worker] IOCP 수신 → RudpChannel 통과 → Push(work item)
//    [Logic Thread] Pop() → DispatchApp → Handle* (게임 로직)
//
//  소비자는 조건 변수로 블로킹 대기하므로 busy-wait 이 없습니다.
//  Shutdown() 시 남은 항목을 모두 소진한 뒤 Pop() 이 false 를 반환하여
//  로직 스레드가 정상 종료됩니다.
// ============================================================

#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

class Session;  // 전방 선언 (shared_ptr 로만 사용)

// 로직 스레드가 처리할 작업 단위.
//   - SessionRef : 처리 대상 세션 (shared_ptr 로 큐잉 중 수명 보장)
//   - Packet     : 전체 패킷(헤더 포함) 바이트 복사본
//                  (수신 버퍼는 Push 직후 PostRecv 로 재사용되므로 반드시 복사)
struct FLogicWorkItem {
    std::shared_ptr<Session> SessionRef;
    std::vector<uint8_t>     Packet;
};

class LogicQueue {
public:
    LogicQueue() = default;

    LogicQueue(const LogicQueue&)            = delete;
    LogicQueue& operator=(const LogicQueue&) = delete;

    /** @brief 생산자: 작업 항목을 큐에 넣고 대기 중인 소비자 1명을 깨움. */
    void Push(FLogicWorkItem&& item)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Shutdown) return;               // 종료 중이면 신규 항목 무시
            m_Queue.push(std::move(item));
        }
        m_Cv.notify_one();
    }

    /**
     * @brief 소비자: 항목이 생길 때까지 블로킹 후 하나 꺼냄.
     * @return true  = out 에 항목이 채워짐
     *         false = 종료 신호 + 큐가 비어 더 이상 처리할 항목 없음
     */
    bool Pop(FLogicWorkItem& out)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Cv.wait(lock, [this] { return !m_Queue.empty() || m_Shutdown; });

        // 종료 중이라도 남은 항목은 모두 소진한 뒤 종료
        if (m_Queue.empty())
            return false;

        out = std::move(m_Queue.front());
        m_Queue.pop();
        return true;
    }

    /** @brief 종료 신호: 모든 소비자를 깨워 잔여 항목 소진 후 빠져나가게 함. */
    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Shutdown = true;
        }
        m_Cv.notify_all();
    }

    /** @brief 현재 대기 중인 항목 수 (통계/모니터링용). */
    size_t Size() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Queue.size();
    }

private:
    mutable std::mutex          m_Mutex;
    std::condition_variable     m_Cv;
    std::queue<FLogicWorkItem>  m_Queue;
    bool                        m_Shutdown = false;
};
