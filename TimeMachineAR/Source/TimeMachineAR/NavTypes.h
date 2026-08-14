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
