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
	 *  2. 시작 호길이 + SpacingCm, +2·SpacingCm, … 를 RangeCm(또는 경로 끝)까지 낸다.
	 *     발자국이 발밑에 겹치지 않도록 첫 발자국은 SpacingCm 앞에서 시작한다.
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
