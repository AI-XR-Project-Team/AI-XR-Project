// ============================================================
//  RudpChannel.cpp
//  역할: RudpChannel 구현부 (신뢰성/순서 제어 상태 머신)
// ============================================================

#include "Network/RudpChannel.h"

// 재조립 버퍼 최대 항목 수 (손실/악의적 gap 로 인한 메모리 폭증 방지)
static constexpr size_t REASM_MAX_ENTRIES = 1024;

// ══════════════════════════════════════════════
//  1) 인바운드 신뢰 스트림
// ══════════════════════════════════════════════

bool RudpChannel::OnReliableReceived(uint32_t seq, const uint8_t* data, int len,
                                     std::vector<Bytes>& outDelivered)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    // (a) 이미 순서 확정된 과거/중복 패킷 → Drop
    if (seq < m_RecvNextSeq)
    {
        return false;
    }

    // (b) 순서가 딱 맞는 패킷 → 즉시 전달 후, 재조립 버퍼에서 연속분 드레인
    if (seq == m_RecvNextSeq)
    {
        outDelivered.emplace_back(data, data + len);
        ++m_RecvNextSeq;

        // 뒤이어 도착해 있던 연속 SeqNum 들을 순서대로 방출
        auto it = m_Reasm.find(m_RecvNextSeq);
        while (it != m_Reasm.end())
        {
            outDelivered.push_back(std::move(it->second));
            m_Reasm.erase(it);
            ++m_RecvNextSeq;
            it = m_Reasm.find(m_RecvNextSeq);
        }
        return true;
    }

    // (c) 미래 패킷(seq > 기대값) → 순서가 맞을 때까지 재조립 버퍼에 보관
    //     (이미 버퍼에 있으면 중복이므로 무시, 버퍼 상한 초과 시에도 무시)
    if (m_Reasm.find(seq) == m_Reasm.end() && m_Reasm.size() < REASM_MAX_ENTRIES)
    {
        m_Reasm.emplace(seq, Bytes(data, data + len));
    }
    return true;
}

uint32_t RudpChannel::GetCumulativeAck() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_RecvNextSeq - 1;
}

// ══════════════════════════════════════════════
//  2) 인바운드 비신뢰 순서 스트림
// ══════════════════════════════════════════════

bool RudpChannel::AcceptUnreliable(uint32_t seq)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    // 첫 패킷은 무조건 수용
    if (!m_HasUnreliable)
    {
        m_HasUnreliable  = true;
        m_UnreliableHigh = seq;
        return true;
    }

    // 지금까지 본 최대치 이하면 과거/중복 → Drop
    if (seq <= m_UnreliableHigh)
    {
        return false;
    }

    m_UnreliableHigh = seq;
    return true;
}

// ══════════════════════════════════════════════
//  3) 아웃바운드 신뢰 스트림
// ══════════════════════════════════════════════

uint32_t RudpChannel::AssignSendSeq()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_SendNextSeq++;
}

void RudpChannel::OnReliableSent(uint32_t seq, const uint8_t* data, int len, int64_t nowMs)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    FPendingPacket pending;
    pending.Data.assign(data, data + len);
    pending.LastSentMs = nowMs;
    pending.RetryCount = 0;
    m_RetransQ[seq] = std::move(pending);
}

void RudpChannel::OnAckReceived(uint32_t ackNum)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    // 누적 ACK: ackNum 이하의 모든 미확인 패킷은 수신 완료된 것으로 간주
    // (map 은 key 오름차순이므로 앞에서부터 제거)
    for (auto it = m_RetransQ.begin();
         it != m_RetransQ.end() && it->first <= ackNum; )
    {
        it = m_RetransQ.erase(it);
    }
}

void RudpChannel::CollectRetransmits(int64_t nowMs, int64_t timeoutMs, uint32_t maxRetries,
                                     std::vector<Bytes>& outResend,
                                     std::vector<uint32_t>& outDeadSeqs)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    for (auto it = m_RetransQ.begin(); it != m_RetransQ.end(); )
    {
        FPendingPacket& p = it->second;

        // 아직 타임아웃 전이면 유지
        if (nowMs - p.LastSentMs < timeoutMs)
        {
            ++it;
            continue;
        }

        if (p.RetryCount >= maxRetries)
        {
            // 최대 재시도 초과 → 포기(큐에서 제거). 호출자가 세션 종료 판단.
            outDeadSeqs.push_back(it->first);
            it = m_RetransQ.erase(it);
            continue;
        }

        // 재전송: 저장된 바이트 복사본을 방출하고 타이머/카운트 갱신
        outResend.push_back(p.Data);
        p.LastSentMs = nowMs;
        ++p.RetryCount;
        ++it;
    }
}

size_t RudpChannel::PendingCount() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_RetransQ.size();
}
