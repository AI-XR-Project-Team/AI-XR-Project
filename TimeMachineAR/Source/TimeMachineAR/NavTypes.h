#pragma once

#include "CoreMinimal.h"
#include "NavTypes.generated.h"

/**
 * 네비게이션 서버(FastAPI)와 주고받는 DTO 모음. **헤더 전용**(로직 없음).
 *
 * 각 구조체의 필드는 서버 Pydantic 스키마와 1:1 로 대응한다. 서버 JSON 키는
 * snake_case(pos_x_cm, heading_deg, node_id ...)이고 UE 필드명과 다르므로,
 * FJsonObjectConverter 자동 매핑에 의존하지 말고 NavClient 에서 **수동 파싱**한다.
 * 서버 스키마(`backend_server/app/schemas/navigation.py`)가 바뀌면 이 파일도 같이 고친다.
 *
 * 계약 원문: docs/nav-server-integration-guide.md §2.
 * 좌표 규약: UE5 Z-up 왼손, cm, heading = +X축 기준 CCW(도).
 */

/** GET /maps/{id}/markers 의 원소. QR 코드 ↔ 맵 pose·heading. */
USTRUCT(BlueprintType)
struct FNavMarker
{
	GENERATED_BODY()

	/** 마커 식별 코드. 예: "MARK-NEUTI4-START" */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Code;

	/** 마커 종류. 현재는 "qr". */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString MarkerType;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosXCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosYCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosZCm = 0.f;

	/** 마커 정면 방향. +X축 기준 CCW(도). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float HeadingDeg = 0.f;

	/** 사람이 읽는 설치 메모. 없을 수 있다. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Note;
};

/** GET /maps/{id}/destinations 의 원소. 목적지 선택 UI 용. */
USTRUCT(BlueprintType)
struct FNavDestination
{
	GENERATED_BODY()

	/** 목적지 노드 UUID. RequestRoute 의 to 로 넘긴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NodeId;

	/** 화면 표시 이름. 예: "주방" */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Label;

	/** UI 분류 태그. "facility" | "junction" | "entrance" ... */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NodeType;
};

/** 경로 응답의 waypoints 원소. 앱은 이 맵 좌표를 월드로 변환해 경로/화살표를 렌더한다. */
USTRUCT(BlueprintType)
struct FNavWaypoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosXCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosYCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosZCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NodeType;
};

/** 경로 응답의 steps 원소. 사람이 읽는 턴바이턴 안내 한 줄. */
USTRUCT(BlueprintType)
struct FNavStep
{
	GENERATED_BODY()

	/** 안내 문구. 예: "앞으로 6m 직진하세요" */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Instruction;

	/**
	 * 직진 step 의 이동 거리(cm). 회전/도착 step 은 서버가 null 로 준다.
	 * null 은 -1 로 표기하되, 진짜 값인지 없음인지는 bHasDistance 로 가른다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float DistanceCm = -1.f;

	/** distance_cm 가 null(없음)이 아니라 실제 값이면 true. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bHasDistance = false;

	/** 회전 방향. "left" | "right" | "straight" | ""(도착 시 서버가 null → 빈 문자열). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Turn;

	/** 이 step 이 도착 안내면 true. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bArrive = false;
};

/** POST /navigation/route · /reroute 의 응답 전체. */
USTRUCT(BlueprintType)
struct FNavRoute
{
	GENERATED_BODY()

	/** 총 이동 거리(cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float TotalDistanceCm = 0.f;

	/** 출발 스냅 노드 → 목적지, 순서대로. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	TArray<FNavWaypoint> Waypoints;

	/** 사람이 읽는 안내 목록. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	TArray<FNavStep> Steps;
};

/**
 * 경로 요청의 from(현재 위치 pose). 측위 계층이 값을 채워 넣는다(연결 계층은 값을 모른다).
 * heading 은 선택 — bHasHeading=false 면 요청 JSON 에서 heading_deg 를 생략한다
 * (서버는 출발 회전 안내를 건너뛴다).
 */
USTRUCT(BlueprintType)
struct FNavMapPose
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Nav")
	float PosXCm = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Nav")
	float PosYCm = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Nav")
	float PosZCm = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Nav")
	float HeadingDeg = 0.f;

	/** false 면 heading 이 없는 것으로 보고 요청에서 heading_deg 를 뺀다. */
	UPROPERTY(BlueprintReadWrite, Category = "Nav")
	bool bHasHeading = false;
};

// ==================================================================== 전체 지도(그래프)
//
// GET /maps/{id}/graph 응답. 전체 미니맵(UNavFullMapWidget)이 벽·구조물·전체 노드·
// 전체 엣지를 한 번에 그리는 데 쓴다. 서버 schemas/maps.py 의 GraphRead 와 1:1.

/** 그래프 노드 1개. 전체 지도에 링으로 찍고, 터치하면 목적지로 고른다. */
USTRUCT(BlueprintType)
struct FNavMapNode
{
	GENERATED_BODY()

	/** 노드 UUID. 목적지로 고르면 RequestRoute 의 to 로 넘긴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosXCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosYCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float PosZCm = 0.f;

	/** "junction" | "waypoint" | "exhibit" | "entrance" | "facility". */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NodeType;

	/** 화면 표시 이름(있을 수 있음). 예: "A지점". */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Label;
};

/** 그래프 엣지 1개. 전체 엣지는 옅은 회색으로 그린다. */
USTRUCT(BlueprintType)
struct FNavMapEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString FromNodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString ToNodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float DistanceCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bBidirectional = true;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bAccessible = true;
};

/** 내부 구조물(축 정렬 사각형, 맵 cm). 전체 지도에 채워서 그린다. */
USTRUCT(BlueprintType)
struct FNavObstacle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float X0 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float Y0 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float X1 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float Y1 = 0.f;
};

/** GET /maps/{id}/graph 전체 응답. 세션 중 한 번 받아 캐시한다(맵은 안 바뀜). */
USTRUCT(BlueprintType)
struct FNavGraph
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString MapId;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString CoordSystem;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	TArray<FNavMapNode> Nodes;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	TArray<FNavMapEdge> Edges;

	/** 벽 폐곡선 꼭짓점(맵 cm), 순서대로. 없으면 빈 배열. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	TArray<FVector2D> Outline;

	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	TArray<FNavObstacle> Obstacles;
};

// ==================================================================== 경로 진행률
//
// UNavRouteProgress 가 매 틱 계산하는 값. 경로 폴리라인 위에서 내가 어디쯤인지.
// 미니맵·배너·이탈 판정이 공유한다. 상태 없이 매번 다시 계산한다(spec §3.3).

/** 경로 위 진행 상태 스냅샷. */
USTRUCT(BlueprintType)
struct FNavProgress
{
	GENERATED_BODY()

	/** 현재 세그먼트 s (waypoints[s] → waypoints[s+1]). 경로 없으면 INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	int32 SegmentIndex = INDEX_NONE;

	/** 그 세그먼트 안에서의 진행률 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float SegmentT = 0.f;

	/** 목적지까지 남은 거리(cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float RemainingCm = 0.f;

	/** 경로에서 옆으로 벗어난 수직거리(cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float LateralOffsetCm = 0.f;

	/** LateralOffsetCm > 임계 → true. 4단계는 로그만 남긴다(자동 reroute 는 5단계). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bOffRoute = false;

	/** 목적지 도착이면 true. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bArrived = false;

	/** 경로/투영이 유효해 값이 채워졌으면 true(경로 없음/미측위면 false). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bValid = false;
};

// ==================================================================== 턴바이턴 안내(5-D)
//
// UNavRouteProgress 가 진행 상태(FNavProgress)와 서버 steps 를 합쳐 매 틱 만드는 값.
// 배너 위젯(WBP_NavStatus)이 이걸 그대로 읽어 "직진 中 · 5m 앞 우회전" 을 그린다.
// 클라이언트는 각도를 다시 재거나 문구를 새로 만들지 않는다 — 서버 steps 를 쓰고
// "지금 어느 step 인가"만 얹는다(spec §3.5).

/** 지금 이 순간 배너에 띄울 한 줄 + 다음에 올 안내. */
USTRUCT(BlueprintType)
struct FNavGuidance
{
	GENERATED_BODY()

	/** 진행 상태·steps 가 유효해 값이 채워졌으면 true. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bValid = false;

	/** 현재 step 의 인덱스(FNavRoute.Steps 기준). 없으면 INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	int32 StepIndex = INDEX_NONE;

	/** 현재 step 의 안내 문구(서버 원문). 예: "앞으로 6m 직진하세요". */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Instruction;

	/** 현재 step 의 회전 방향. "straight" | "left" | "right" | "". */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString Turn;

	/** 현재 step 이 끝날 때(=다음 회전/도착 지점)까지 남은 거리(cm). 경계에서도 음수가 안 된다. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float StepRemainingCm = 0.f;

	/** 다음에 올 회전/도착의 방향. 곧 있을 안내를 미리 띄우는 데 쓴다. 없으면 빈 문자열. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NextTurn;

	/** 다음에 올 회전/도착의 안내 문구. 없으면 빈 문자열. */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	FString NextInstruction;

	/** 목적지까지 남은 총 거리(cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	float RemainingCm = 0.f;

	/** 목적지 도착이면 true(도착 화면 트리거). */
	UPROPERTY(BlueprintReadOnly, Category = "Nav")
	bool bArrived = false;
};
