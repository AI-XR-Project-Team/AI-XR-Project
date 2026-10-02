#pragma once

#include <cstdint>

// 패킷 타입 정의
enum class EPacketType : uint8_t {
    PKT_JOIN         = 0x01,  // 세션 입장 요청 (C->S)
    PKT_LEAVE        = 0x02,  // 세션 정상 퇴장 (C->S)
    PKT_TRANSFORM    = 0x03,  // 위치/회전 동기화 핵심 패킷 (30Hz) (C<->S)
    PKT_ERA_CHANGE   = 0x04,  // 타임 슬라이더 시대 변경 (C->S->C)
    PKT_HEARTBEAT    = 0x05,  // 연결 유지 핑/퐁 (1Hz) (C<->S)
    PKT_AI_TRIGGER   = 0x06,  // POI 응시 -> AI 도슨트 호출 (C->S->BE)
    PKT_ACK          = 0x07,  // 수신 확인 (RUDP 확장 단계용) (S->C)
    PKT_SERVER_STATE = 0x08,  // 세션 현황 브로드캐스트 (S->C)
    PKT_WORLD_STATE  = 0x09   // 주변 유저 위치 집계 브로드캐스트 (S->C, 30Hz)
};

// 패킷 헤더 Flags 비트 정의 (RUDP 계층)
enum EPacketFlags : uint8_t {
    PKT_FLAG_NONE     = 0x00,
    PKT_FLAG_RELIABLE = 0x01,  // 신뢰 전송: 재전송 큐 + 순서 재조립 대상
};

#pragma pack(push, 1)

// 공통 패킷 헤더
//   RUDP(신뢰 UDP) 계층 필드(SeqNum/AckNum/Flags)를 응용 패킷 공통으로 둡니다.
//   - RELIABLE 플래그가 켜진 패킷: SeqNum 은 신뢰 스트림의 순서 번호,
//     수신측은 이 번호로 중복/역전을 판단하고 순서대로 재조립합니다.
//   - 모든 패킷: AckNum 은 "이 번호까지 순서대로 수신 완료했다"는 누적 ACK로,
//     상대의 재전송 큐를 정리하는 데 사용됩니다(piggyback).
struct FPacketHeader {
    uint8_t  Magic[2];     // { 0x41, 0x52 } ("AR") - 유효성 검사용
    uint8_t  Version;      // 현재 버전 (0x01)
    uint8_t  Type;         // EPacketType
    uint16_t BodyLen;      // 헤더를 제외한 실제 바디(Body)의 길이
    uint32_t SeqNum;       // RUDP 시퀀스 번호 (RELIABLE 시 유효, 그 외 비신뢰 순서용)
    uint32_t AckNum;       // 누적 ACK: 여기까지 순서대로 수신 완료 (piggyback)
    uint8_t  Flags;        // EPacketFlags 비트마스크 (bit0: RELIABLE)
};

// 0x01: 접속 패킷
struct FJoinPacket {
    FPacketHeader Header;
    uint32_t      UserId;         // 유저 고유 식별자
    uint32_t      LocationId;     // 유적지/전시실 ID
    uint8_t       Nickname[16];   // null-terminated UTF-8 닉네임
    uint32_t      ClientVersion;  // 클라이언트 버전 (e.g. 0x00010000 = v1.0.0)
};

// 0x02: 퇴장 패킷
struct FLeavePacket {
    FPacketHeader Header;
    uint32_t      UserId;   // 퇴장 유저 ID
    uint8_t       Reason;   // 0=정상, 1=타임아웃, 2=강제퇴장, 3=앱 종료
};

// 0x03: 동기화 패킷 - 30Hz로 가장 많이 통신하는 패킷
struct FSyncTransformPacket {
    FPacketHeader Header;
    uint32_t      UserId;
    uint32_t      SessionId;
    uint32_t      SeqNum;      // 패킷 순서 (순서 역전 방지 및 DR용)
    uint64_t      Timestamp;   // 클라이언트 송신 시각 (ms)
    
    // Transform (Unreal World Coordinate)
    float         PosX, PosY, PosZ;       // 위치
    float         RotX, RotY, RotZ, RotW; // 쿼터니언 회전
    
    // Velocity (Dead Reckoning 예측 보간용)
    float         VelX, VelY, VelZ;       // 속도 벡터
    
    uint8_t       EraId;       // 관람 시대 (0=현재, 1=신라, 2=고려, 3=조선, 4=백악기, 5=쥐라기, 6=트라이아스기)
    uint8_t       Flags;       // 비트 마스크 (bit0: IS_MOVING, bit1: IS_GAZING, bit2: IS_MENU_OPEN)
};

// 0x04: 시대 변경 패킷
struct FEraChangePacket {
    FPacketHeader Header;
    uint32_t      UserId;
    uint32_t      SessionId;
    uint8_t       FromEra;    // 이전 시대
    uint8_t       ToEra;      // 변경 시대
    uint8_t       SyncMode;   // 0=개별 모드, 1=전체 동기화 모드
    uint64_t      Timestamp;
};

// 0x05: 하트비트 패킷
struct FHeartbeatPacket {
    FPacketHeader Header;
    uint32_t      UserId;
    uint64_t      ClientTime;  // RTT 측정을 위한 에코(Echo)용
};

// 0x06: AI 도슨트 트리거 패킷
struct FAiTriggerPacket {
    FPacketHeader Header;
    uint32_t      UserId;
    uint32_t      LocationId;
    uint32_t      ObjectId;      // 응시한 유물/POI ID
    uint8_t       EraId;         // 현재 시대
    float         GazeDuration;  // 누적 응시 시간 (초, >= 2.0f 시 트리거)
};

// 0x07: 수신 확인 패킷 (RUDP 확장 단계용)
struct FAckPacket {
    FPacketHeader Header;
    uint32_t      UserId;        // 수신 확인하는 유저 ID
    uint32_t      AckSeqNum;     // 확인 응답할 seq_num (FSyncTransformPacket.SeqNum)
};

// 0x08: 서버 상태 패킷
struct FServerStatePacket {
    FPacketHeader Header;
    uint32_t      SessionId;
    uint8_t       UserCount;   // 현재 세션 접속자 수 (max 255)
    uint32_t      ServerTime;  // 서버 시각 (Unix sec)
    uint8_t       CurrentEra;  // 서버 기준 시대
};

// 0x09: 월드 스테이트 브로드캐스트
//   서버가 30Hz로 "같은 시대(Era)에 있는 주변 유저들"의 트랜스폼을 모아
//   각 클라이언트에게 집계 전송합니다(AOI = Era 필터).
//
//   와이어 레이아웃:  FPacketHeader | FWorldStateBody | FWorldStateEntry × EntryCount
//
//   MTU(1472) 초과 시 여러 프래그먼트로 분할되며, 같은 ServerTimeMs 를 공유하는
//   FragCount 개의 프래그먼트를 클라이언트가 병합해 "이번 틱의 전체 이웃 목록"을
//   구성합니다. 클라이언트는 header.SeqNum 이 아닌 ServerTimeMs 로 최신성을 판단합니다.

// 개별 유저 1명의 트랜스폼 엔트리 (50 bytes)
struct FWorldStateEntry {
    uint32_t UserId;       // 대상 유저 ID
    uint32_t SeqNum;       // 원본 트랜스폼 SeqNum (스테일 판정/디버그)
    float    PosX, PosY, PosZ;
    float    RotX, RotY, RotZ, RotW;
    float    VelX, VelY, VelZ;
    uint8_t  Era;          // 이 유저의 시대 (수신자와 동일)
    uint8_t  Flags;        // FSyncTransformPacket.Flags 와 동일 비트 정의
};

// 월드 스테이트 바디 헤더 (엔트리 배열이 뒤따름)
struct FWorldStateBody {
    uint64_t ServerTimeMs;  // 틱 식별자(단조 증가) - 프래그먼트 병합/최신성 판정
    uint16_t EntryCount;    // 이 프래그먼트에 담긴 엔트리 수
    uint8_t  FragIndex;     // 프래그먼트 인덱스 (0 .. FragCount-1)
    uint8_t  FragCount;     // 이번 틱의 총 프래그먼트 수
};

#pragma pack(pop)