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
 *
 * ## 좌우 교대는 StepIndex(k) 로 정한다 — 화면 배열 인덱스 i 가 아니다 (황금 발자국)
 *
 * 왼발/오른발은 `StepIndex % 2` 로 갈린다. 결과 배열의 i 로 가르면 사용자가 한 걸음 걸어
 * 가장 가까운 발이 창에서 빠지는 순간 남은 발 전부의 좌우가 뒤집힌다. k 는 경로 시작
 * 기준이라 같은 자리의 발은 언제나 같은 발이다. 가로 오프셋(좌우 벌림)은 여기서 하지 않고
 * 액터가 **월드 접선** 기준으로 건다 — `MapToWorld` 가 Y 를 뒤집는 거울 변환이라 맵에서 왼쪽으로
 * 민 발이 월드에서는 오른쪽에 놓이기 때문이다(NavLocalizer::SolveTransform).
 */

/** 발자국 하나를 놓을 자리. */
struct FNavFloorPlacement
{
	/** 맵 좌표(cm). 경로 폴리라인 위의 점(가로 오프셋 전). */
	FVector2D MapPos = FVector2D::ZeroVector;

	/**
	 * 진행 방향 yaw(도). 맵 +X 축 기준 CCW = UE 월드 yaw(축 리맵 없음, NavLocalizer §).
	 * 자리 앞뒤 ½Spacing 두 점을 잇는 현(chord) 방향이라 코너에서는 두 세그먼트 사이로
	 * 부드럽게 돈다. 직선 구간에서는 세그먼트 방향과 같다.
	 */
	float YawDeg = 0.f;

	/** 경로 시작 기준 그리드 번호 k (Arc = k·SpacingCm). 창이 밀려도 같은 자리는 같은 k. */
	int32 StepIndex = 0;

	/**
	 * 가로(좌우) 오프셋 배율 0..1. 급회전(앞뒤 ½Spacing 접선 사이 각이 클수록) 자리에서는
	 * 줄여 발이 코너 바깥으로 튀어 벽을 뚫지 않게 한다. 직선이면 1.
	 */
	float LateralScale = 1.f;

	/** 짝수 k = 왼발, 홀수 k = 오른발. */
	bool IsLeft() const { return (StepIndex & 1) == 0; }
};

struct TIMEMACHINEAR_API FNavFloorGuide
{
	/**
	 * 정적 경로 폴리라인 위, 사용자 앞 RangeCm 까지 SpacingCm 간격으로 발자국 자리를 낸다.
	 *
	 *  1. UserXY 를 폴리라인에 투영(수직거리 최소 세그먼트) → 시작 호길이.
	 *  2. 경로 시작 기준 k·SpacingCm 의 고정 그리드 중 [시작 호길이+½Spacing(발밑 여유),
	 *     시작 호길이+RangeCm] 창 안에 드는 자리만 낸다(월드 고정 — 사용자 따라 안 미끄러짐).
	 *  3. 각 자리의 진행 방향 = 자리 앞뒤 ½Spacing 두 점을 잇는 현 방향. LateralScale 은
	 *     그 두 점의 세그먼트 접선 사이 각으로 정한다(TurnReduceStartDeg~TurnReduceEndDeg 에서 1→0).
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

	/**
	 * 월드에서 발을 좌우로 벌린다. 진행 방향 WorldDir(XY, 정규화 불필요) 기준 왼발은 왼쪽(-Right),
	 * 오른발은 오른쪽(+Right)으로 |OffsetCm|·LateralScale 만큼. UE 왼손 좌표: Forward (1,0) 의
	 * Right 는 (0,1) 이므로 Right = (-Dir.Y, Dir.X).
	 */
	static FVector OffsetForStep(const FVector& WorldCenter, const FVector& WorldDir,
		bool bLeft, float OffsetCm, float LateralScale);

	/** 회전각이 이 값(도) 이상이면 가로 오프셋을 줄이기 시작한다. */
	static constexpr float TurnReduceStartDeg = 30.f;
	/** 회전각이 이 값(도) 이상이면 가로 오프셋 0(경로 위에 정확히). */
	static constexpr float TurnReduceEndDeg = 75.f;
};
