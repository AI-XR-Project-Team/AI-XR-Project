#pragma once

// ============================================================
//  RudpChannel.h
//  역할: 세션 1개의 RUDP(신뢰 UDP) 상태 머신
//
//  세 가지 축을 캡슐화합니다 (양방향, 서로 독립):
//    1) 인바운드 신뢰 스트림 (Client -> Server)
//       - 중복/스테일 패킷 Drop
//       - 순서 재조립(Reassembly): 순서가 맞을 때까지 버퍼링 후 순서대로 전달
//       - 누적 ACK 번호 산출
//    2) 인바운드 비신뢰 순서 스트림 (Transform 30Hz)
//       - 최신 SeqNum 만 수용, 과거/중복은 Drop (재전송/재조립 없음)
//    3) 아웃바운드 신뢰 스트림 (Server -> Client)
//       - 송신 SeqNum 발급
//       - 재전송 큐: 복사본 보관, 타임아웃 시 재전송, 누적 ACK로 정리
//
//  스레드 안전성:
//    여러 Worker 스레드 + 재전송 타이머 스레드가 동시에 접근하므로
//    모든 공개 메서드는 내부 mutex 로 보호됩니다.
// ============================================================

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

class RudpChannel
{
public:
    using Bytes = std::vector<uint8_t>;

    // ── 재전송 큐 항목 ─────────────────────────────
    struct FPendingPacket {
        Bytes    Data;              // 직렬화된 전체 패킷(헤더 포함)
        int64_t  LastSentMs = 0;    // 마지막 송신 시각
        uint32_t RetryCount = 0;    // 재전송 횟수
    };

    // ══════════════════════════════════════════════
    //  1) 인바운드 신뢰 스트림 (Client -> Server)
    // ══════════════════════════════════════════════
    /**
     * @brief 신뢰 패킷 수신 처리 (중복 Drop + 순서 재조립).
     * @param seq          패킷의 헤더 SeqNum
     * @param data,len     패킷 전체 바이트(헤더 포함)
     * @param outDelivered 이번 수신으로 순서가 확정되어 응용 계층에 올릴
     *                     패킷들의 바이트가 순서대로 append 됨(0개 이상)
     * @return true  = 수용(즉시 전달 또는 재조립 버퍼에 보관)
     *         false = Drop(이미 처리한 과거/중복 패킷)
     * @note 반환값과 무관하게 호출측은 누적 ACK를 회신해야 합니다
     *       (상대가 이전 ACK를 못 받았을 수 있으므로).
     */
    bool OnReliableReceived(uint32_t seq, const uint8_t* data, int len,
                            std::vector<Bytes>& outDelivered);

    /** @brief 누적 ACK 번호 (연속 수신 완료한 마지막 SeqNum, 없으면 0). */
    uint32_t GetCumulativeAck() const;

    // ══════════════════════════════════════════════
    //  2) 인바운드 비신뢰 순서 스트림 (Transform)
    // ══════════════════════════════════════════════
    /**
     * @brief 비신뢰 순서 패킷 수용 여부 판정.
     * @return true  = 최신 패킷(수용) → high-water 갱신
     *         false = 과거/중복 패킷(Drop)
     */
    bool AcceptUnreliable(uint32_t seq);

    // ══════════════════════════════════════════════
    //  3) 아웃바운드 신뢰 스트림 (Server -> Client)
    // ══════════════════════════════════════════════
    /** @brief 다음 송신 SeqNum 발급. */
    uint32_t AssignSendSeq();

    /** @brief 신뢰 패킷 송신 후 복사본을 재전송 큐에 등록. */
    void OnReliableSent(uint32_t seq, const uint8_t* data, int len, int64_t nowMs);

    /** @brief 상대의 누적 ACK 수신: ackNum 이하 항목을 재전송 큐에서 제거. */
    void OnAckReceived(uint32_t ackNum);

    /**
     * @brief 타임아웃된 재전송 대상을 수집.
     * @param outResend   재전송할 패킷 바이트(복사본)들이 append 됨
     * @param outDeadSeqs 최대 재시도를 초과해 포기한 SeqNum 들(큐에서 제거됨)
     */
    void CollectRetransmits(int64_t nowMs, int64_t timeoutMs, uint32_t maxRetries,
                            std::vector<Bytes>& outResend,
                            std::vector<uint32_t>& outDeadSeqs);

    /** @brief 재전송 대기 중인 패킷 수. */
    size_t PendingCount() const;

private:
    mutable std::mutex m_Mutex;

    // 인바운드 신뢰 스트림
    uint32_t             m_RecvNextSeq = 1;   // 다음 기대 SeqNum (누적 ACK = m_RecvNextSeq - 1)
    std::map<uint32_t, Bytes> m_Reasm;        // 미래 도착 패킷(재조립 대기) : seq -> bytes

    // 인바운드 비신뢰 스트림
    bool     m_HasUnreliable   = false;       // 첫 패킷 수신 여부
    uint32_t m_UnreliableHigh  = 0;           // 지금까지 본 최대 SeqNum

    // 아웃바운드 신뢰 스트림
    uint32_t m_SendNextSeq = 1;               // 다음 발급 SeqNum
    std::map<uint32_t, FPendingPacket> m_RetransQ;  // 미확인(ACK 대기) 패킷들
};
