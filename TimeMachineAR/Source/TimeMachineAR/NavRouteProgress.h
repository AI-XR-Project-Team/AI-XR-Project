#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NavTypes.h"
#include "NavRouteProgress.generated.h"

/**
 * 경로 폴리라인 위에서 "내가 어디쯤인지"를 계산하는 **순수 계산 클래스**.
 *
 * AR 도 HTTP 도 위젯도 모른다 — 입력은 맵 좌표(cm)뿐이라 PIE 없이 단위 테스트로
 * 검증할 수 있고, 미니맵·(5단계)턴바이턴 배너·이탈 판정이 전부 이걸 공유한다.
 *
 * ## 핵심 규칙 — 상태를 들고 있지 않는다 (spec §3.3)
 *
 * 매 틱 다시 계산한다. 경로 폴리라인의 각 세그먼트에 내 위치를 투영해 수직거리가
 * 가장 작은 세그먼트 s 와 그 안에서의 진행률 t 를 구한다. 앞으로 걸으면 s 가 커지고
 * **뒤로 걸으면 작아진다** — "나-X 사이에 Y가 끼면 나-Y-X 로 실시간 수정"이 분기
 * 코드 없이 자동으로 나온다.
 *
 * ## 붙인 세 가지 안전장치
 *  - 탐색 윈도우: 직전 s ± SearchWindowSegments 만 본다(경로가 자기 근처를 되지날 때
 *    엉뚱한 세그먼트로 점프 방지). LastSegment 가 없으면 전체를 본다.
 *  - 히스테리시스: 세그먼트 전환에 데드밴드(SegmentHysteresisCm) — 경계에서 s 가 떠는 것 방지.
 *  - 이탈 임계: LateralOffsetCm > OffRouteThresholdCm 면 bOffRoute. 4단계는 로그만.
 *
 * 판정용 위치는 원본을 쓰고, **그리기용 위치는 지수 평활**(DrawSmoothingAlpha)한 값을
 * 쓴다(측위 노이즈가 선 끝을 떨게 하므로).
 *
 * ini 노출: DefaultGame.ini [/Script/TimeMachineAR.NavRouteProgress] 에서 임계값을 조정한다.
 */
UCLASS(BlueprintType, Config = Game)
class TIMEMACHINEAR_API UNavRouteProgress : public UObject
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ 입력

	/** 경로 응답으로 폴리라인을 세운다. waypoints 의 XY 만 쓴다(Z 는 버린다). */
	UFUNCTION(BlueprintCallable, Category = "Nav|Progress")
	void SetRoute(const FNavRoute& InRoute);

	/** 폴리라인을 직접 넣는다(테스트·비-서버 입력용). */
	void SetRoutePoints(const TArray<FVector2D>& InPoints);

	/** 경로·상태를 비운다. */
	UFUNCTION(BlueprintCallable, Category = "Nav|Progress")
	void Reset();

	UFUNCTION(BlueprintPure, Category = "Nav|Progress")
	bool HasRoute() const { return RoutePts.Num() >= 2; }

	/**
	 * 현재 위치로 진행 상태를 다시 계산한다. 매 틱 부른다.
	 * @param CurrentXY 맵 좌표(cm). 판정에는 이 원본을 쓴다.
	 * @return 이번 프레임의 진행 스냅샷.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Progress")
	FNavProgress UpdatePose(const FVector2D& CurrentXY);

	/** 마지막 UpdatePose 결과. */
	UFUNCTION(BlueprintPure, Category = "Nav|Progress")
	FNavProgress GetProgress() const { return LastProgress; }

	// ------------------------------------------------------------------ 출력(그리기)

	/**
	 * 그리기용 폴리라인 = [평활된 내 위치] + [waypoints[s+1], …, 마지막].
	 * 아직 UpdatePose 를 안 불렀으면 원본 경로 전체를 준다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav|Progress")
	void GetDrawPolyline(TArray<FVector2D>& OutPoints) const;

	// ------------------------------------------------------------------ 튜닝(ini)

	/** 이탈 판정 임계(cm). 넘으면 bOffRoute. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Progress",
		meta = (ClampMin = "10.0"))
	float OffRouteThresholdCm = 200.f;

	/** 탐색 윈도우: 직전 세그먼트 ± 이 값만 본다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Progress",
		meta = (ClampMin = "1"))
	int32 SearchWindowSegments = 2;

	/** 세그먼트 전환 데드밴드(cm). 직전 세그먼트에 이만큼 가산점을 줘 떨림을 막는다. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Progress",
		meta = (ClampMin = "0.0"))
	float SegmentHysteresisCm = 15.f;

	/** 그리기 위치 지수 평활 계수(0=안 움직임, 1=즉시). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Progress",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float DrawSmoothingAlpha = 0.35f;

	/** 남은 거리가 이 값 이하이면 도착으로 본다(cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nav|Progress",
		meta = (ClampMin = "1.0"))
	float ArriveThresholdCm = 80.f;

private:
	/** 경로 폴리라인(맵 cm). */
	TArray<FVector2D> RoutePts;
	/** 시작점부터 정점 i 까지의 누적 거리(cm). CumCm[0]=0, CumCm.Last()=총거리. */
	TArray<float> CumCm;

	/** 직전 프레임 세그먼트(탐색 윈도우·히스테리시스 기준). */
	int32 LastSegment = INDEX_NONE;

	/** 그리기용 평활 위치. */
	FVector2D SmoothedDrawXY = FVector2D::ZeroVector;
	bool bHasSmoothed = false;

	FNavProgress LastProgress;

	/** 점 P 를 선분 A-B 에 투영. 반환 (t 클램프, lateral). */
	static void ProjectToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B,
		float& OutT, float& OutLateral);
};
