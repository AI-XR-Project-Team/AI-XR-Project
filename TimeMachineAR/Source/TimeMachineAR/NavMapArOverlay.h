// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavMapArOverlay.generated.h"

/** 전시섬(축정렬 사각형, 맵 cm). 리플렉션 불필요. */
struct FNavOverlayObstacle
{
	FVector2D Min = FVector2D::ZeroVector;
	FVector2D Max = FVector2D::ZeroVector;
};

/** 서버 cloud_anchors 행 1건(맵 cm · heading 도). */
struct FNavOverlayAnchor
{
	int32 PointNo = 0;
	FVector2D Pos = FVector2D::ZeroVector;
	float HeadingDeg = 0.f;
};

/** 그래프 노드 1건(맵 cm). 라벨 = UUID 앞 4자리 코드(목적지면 뒤에 '*') — 2D 미리보기와 같은 값. */
struct FNavOverlayNode
{
	FString Id;
	FString ShortLabel;
	FVector2D Pos = FVector2D::ZeroVector;
};

/**
 * 13-3 현장 검증 — **지도(벽·전시섬)와 앵커 DB 좌표를 AR 에 겹쳐 그린다.**
 *
 * ## 왜
 * 앵커 좌표를 지도에 얹는 정합(13-3 §G)은 계획점·궤적에 기대 수십 cm 가 남는다. 현장에서 지도 벽선을
 * 실제 벽 위에 그려 보면 **남은 어긋남이 cm 단위로 보인다** — 전체가 밀렸는지(전역 이동·회전),
 * 한 앵커만 틀렸는지(개별)를 가를 수 있다. 보정은 서버 좌표만 고치면 되고 APK 는 다시 굽지 않는다
 * (앱 재실행 → 다시 받아 그린다).
 *
 * ## 그리는 것 (측위가 선 뒤에만)
 *  - 벽 외곽선 · 전시섬 — 바닥(+WallLowCm)과 눈높이(+WallHighCm) 두 겹 + 꼭짓점 기둥 (하늘색·주황)
 *  - 앵커 DB 좌표 — 노란 구 + 기둥 + `#번호`
 *  - 인식된 앵커의 **실제 핀 ↔ DB 좌표** — 빨간 선 + 차이(cm). 기준 앵커는 정의상 0 이다
 *  - 노드·엣지 — bDrawGraph (초록 · 거리와 무관하게 **전부** · 노드는 구 + node_id 끝토막).
 *    GraphRefreshSeconds 마다 서버에서 다시 받으므로, 현장에서 노드를 옮기면(서버 좌표 수정)
 *    앱을 다시 켜지 않아도 바닥 그림이 따라온다
 *
 * ## 로그 — 보정량 계산의 입력
 *   [NavMapOverlay] RESID t=<초> ref=<CA번호> #<n> dx=<cm> dy=<cm> d=<cm> dyaw=<도>
 *   [NavMapOverlay] POS t=<초> x=<cm> y=<cm> yaw=<도>     (1초마다 · 카메라의 맵 좌표 — "노드 N 은 지금 선 자리" 보정용)
 * 인식된 앵커마다 ResidualLogIntervalSeconds 간격. **지금 기준 앵커로 본 맵 좌표 잔차**(실제 − DB)와
 * heading 잔차(핀 yaw − 기대 yaw). 사람 눈대중과 별개로 이 줄들로 앵커별 보정량을 계산한다.
 *
 * ## 공지 트리거 회피 (CLAUDE.md §3)
 * 새 파일 · 월드 공간 디버그 선(UMG 위젯 아님 → 화면 배치·z-order·입력 포커스와 무관) · ini **새 섹션**.
 * NavLocalizer·NavClient 는 공개 함수만 읽는다(수정 0). 디버그 그리기는 Shipping 빌드에선 빠진다.
 * 서버 데이터는 NavClient 캐시를 건드리지 않도록 **직접** 받는다(`/graph` · `/cloud-anchors`).
 */
UCLASS(Config = Game)
class TIMEMACHINEAR_API UNavMapArOverlaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 켜기 스위치. 기본 꺼짐 — 현장 검증 빌드의 ini 에서만 켠다. */
	UPROPERTY(Config)
	bool bEnabled = false;

	/** 카메라에서 이 거리(cm) 안의 선만 그린다(겹침·부하 방지). */
	UPROPERTY(Config, meta = (ClampMin = "100.0"))
	float MaxDrawDistanceCm = 1500.0f;

	/** 벽선 높이(cm, 앵커 바닥 기준). 낮은 선은 바닥 맞춤 확인용, 높은 선은 멀리서 보이게. */
	UPROPERTY(Config)
	float WallLowCm = 5.0f;

	UPROPERTY(Config)
	float WallHighCm = 120.0f;

	/** 노드·엣지를 그릴지(거리와 무관하게 전부). */
	UPROPERTY(Config)
	bool bDrawGraph = false;

	/** 그래프(벽·노드·엣지)를 다시 받는 주기(초). 현장 노드 보정이 재시작 없이 반영된다. */
	UPROPERTY(Config, meta = (ClampMin = "5.0"))
	float GraphRefreshSeconds = 20.0f;

	/** 카메라 맵 좌표를 1초마다 POS 로그로 남길지. */
	UPROPERTY(Config)
	bool bLogCameraMapPose = true;

	/** 잔차 로그 주기(초). */
	UPROPERTY(Config, meta = (ClampMin = "1.0"))
	float ResidualLogIntervalSeconds = 5.0f;

private:
	/** ini(NavClient 섹션)와 NavClient 서브시스템에서 서버 주소·맵 id 를 읽는다(수정 없이 읽기만). */
	void TryResolveConfig();
	/** GET /maps/{id}/graph — 벽 외곽선·전시섬·노드·엣지. GraphRefreshSeconds 마다 다시 받는다. */
	void FetchGraph();
	/** GET /maps/{id}/cloud-anchors?state=bound — 주기적으로 다시 받아 서버 보정을 반영한다. */
	void FetchAnchors();
	/** 한 프레임 분량의 선을 그린다(수명은 다음 그리기와 살짝 겹치게). */
	void DrawOverlay();
	/** 인식된 앵커마다 RESID 한 줄. */
	void LogResiduals(double Now);

	bool bConfigured = false;
	bool bConfigWarned = false;
	bool bGraphLoaded = false;
	bool bGraphInFlight = false;
	bool bAnchorsInFlight = false;
	bool bAnnounced = false;
	double NextGraphFetch = 0.0;
	double NextAnchorFetch = 0.0;
	double NextDraw = 0.0;
	double NextResidualLog = 0.0;
	double NextPoseLog = 0.0;

	FString ServerBaseUrl;
	FString MapId;

	TArray<FVector2D> Outline;
	TArray<FNavOverlayObstacle> Obstacles;
	/** bDrawGraph 용 — 엣지 양 끝 좌표(맵 cm). */
	TArray<TPair<FVector2D, FVector2D>> GraphEdges;
	TArray<FNavOverlayNode> GraphNodes;
	/** 받은 그래프가 바뀌었는지(로그 스로틀) — 노드 id·좌표·엣지 수를 섞은 해시. */
	uint32 GraphHash = 0;
	TArray<FNavOverlayAnchor> Anchors;
};
