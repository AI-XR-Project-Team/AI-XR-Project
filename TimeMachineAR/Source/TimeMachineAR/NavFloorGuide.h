#pragma once

#include "CoreMinimal.h"

/**
 * 바닥 AR 발자국을 **경로 어디에 어느 방향으로** 놓을지 정하는 순수 계산(9단계 §C-1).
 *
 * AR 도 액터도 머티리얼도 모른다 — 입력은 맵 좌표(cm)뿐이라 PIE 없이 헤드리스 자동화
 * 테스트로 검증한다(`TimeMachineAR.Nav.FloorGuide`). `ANavFloorGuideActor` 가 이 결과를
 * `UNavLocalizer::MapToWorld()` 로 월드에 얹기만 한다. NavRouteProgress·NavDestinations 와
 * 같은 "순수 로직 분리" 원칙(spec §F).
 *
 * ## 미니맵 안내선과 규칙이 다르다 (의도됨, final §C-1 보강)
 *
 * 미니맵 경로선은 평활된 사용자 위치에서 시작해 휜다. 바닥 발자국은 **정적 경로 폴리라인
 * (엣지)에 고정**되고, 사용자를 폴리라인에 투영해 그 **앞쪽 구간만** 낸다. 사용자가 걸으면
 * 투영점이 앞으로 가 발자국이 앞에서 사라지고 새 발자국이 멀리서 들어온다.
 *
 * ## 발자국은 월드 고정(사용자를 따라 미끄러지지 않는다, final §C-1 보강)
 *
 * 자리는 **경로 시작 기준 k·SpacingCm 의 고정 그리드**에만 잡힌다. 사용자를 따라다니는 건
 * "보이는 창"(사용자 앞 RangeCm)뿐이라, 걸어가면 뒤쳐진 발자국은 빠지고 앞에서 새 발자국이
 * 들어오되 각 발자국의 월드 위치는 그대로다. (예전엔 자리를 StartArc 기준으로 잡아 발자국
 * 전부가 사용자와 함께 슬라이드했다.)
 */

/** 발자국 하나를 놓을 자리. */
struct FNavFloorPlacement
{
	/** 맵 좌표(cm). 경로 폴리라인 위의 점. */
	FVector2D MapPos = FVector2D::ZeroVector;

	/** 진행 방향 yaw(도). 맵 +X 축 기준 CCW = UE 월드 yaw(축 리맵 없음, NavLocalizer §). */
	float YawDeg = 0.f;
};

struct TIMEMACHINEAR_API FNavFloorGuide
{
	/**
	 * 정적 경로 폴리라인 위, 사용자 앞 RangeCm 까지 SpacingCm 간격으로 발자국 자리를 낸다.
	 *
	 *  1. UserXY 를 폴리라인에 투영(수직거리 최소 세그먼트) → 시작 호길이.
	 *  2. 경로 시작 기준 k·SpacingCm 의 고정 그리드 중 [시작 호길이+½Spacing(발밑 여유),
	 *     시작 호길이+RangeCm] 창 안에 드는 자리만 낸다(월드 고정 — 사용자 따라 안 미끄러짐).
	 *  3. 각 자리의 진행 방향 = 그 지점이 속한 세그먼트의 방향.
	 *
	 * @param RoutePts   경로 폴리라인(맵 cm), 시작→목적지 순. 2점 미만이면 빈 결과.
	 * @param UserXY     현재 사용자 위치(맵 cm).
	 * @param SpacingCm  발자국 간격(cm). ≤0 이면 빈 결과.
	 * @param RangeCm    사용자 앞으로 낼 최대 거리(cm). ≤0 이면 빈 결과.
	 * @param Out        결과(사용자에 가까운 것부터).
	 */
	static void BuildPlacements(const TArray<FVector2D>& RoutePts, const FVector2D& UserXY,
		float SpacingCm, float RangeCm, TArray<FNavFloorPlacement>& Out);

	/**
	 * 점 P 를 선분 A-B 에 투영. OutT 는 [0,1] 클램프, OutLateral 은 수직거리(cm).
	 * NavRouteProgress 의 같은 헬퍼와 규약이 같다(중복이지만 순수 헤더 독립성을 위해 자체 보유).
	 */
	static void ProjectToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B,
		float& OutT, float& OutLateral);
};
