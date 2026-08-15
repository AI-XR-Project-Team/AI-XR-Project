#include "NavRouteProgress.h"

#include "NavClient.h"   // LogNav

void UNavRouteProgress::SetRoute(const FNavRoute& InRoute)
{
	TArray<FVector2D> Pts;
	Pts.Reserve(InRoute.Waypoints.Num());
	for (const FNavWaypoint& Wp : InRoute.Waypoints)
	{
		Pts.Emplace(Wp.PosXCm, Wp.PosYCm);
	}
	SetRoutePoints(Pts);
}

void UNavRouteProgress::SetRoutePoints(const TArray<FVector2D>& InPoints)
{
	RoutePts = InPoints;

	// 누적 거리표. 연속 중복점은 세그먼트 길이 0 으로 남되 투영에서 자연히 걸러진다.
	CumCm.Reset(RoutePts.Num());
	float Acc = 0.f;
	for (int32 i = 0; i < RoutePts.Num(); ++i)
	{
		if (i > 0)
		{
			Acc += (RoutePts[i] - RoutePts[i - 1]).Size();
		}
		CumCm.Add(Acc);
	}

	LastSegment = INDEX_NONE;
	bHasSmoothed = false;
	LastProgress = FNavProgress();
}

void UNavRouteProgress::Reset()
{
	RoutePts.Reset();
	CumCm.Reset();
	LastSegment = INDEX_NONE;
	bHasSmoothed = false;
	LastProgress = FNavProgress();
}

void UNavRouteProgress::ProjectToSegment(const FVector2D& P, const FVector2D& A,
	const FVector2D& B, float& OutT, float& OutLateral)
{
	const FVector2D AB = B - A;
	const float Ab2 = static_cast<float>(AB | AB);
	float T = 0.f;
	if (Ab2 > KINDA_SMALL_NUMBER)
	{
		T = static_cast<float>((P - A) | AB) / Ab2;
		T = FMath::Clamp(T, 0.f, 1.f);
	}
	const FVector2D Foot = A + AB * T;
	OutT = T;
	OutLateral = static_cast<float>((P - Foot).Size());
}

FNavProgress UNavRouteProgress::UpdatePose(const FVector2D& CurrentXY)
{
	FNavProgress Out;

	// 그리기용 평활 위치는 경로 유무와 무관하게 갱신한다.
	if (!bHasSmoothed)
	{
		SmoothedDrawXY = CurrentXY;
		bHasSmoothed = true;
	}
	else
	{
		SmoothedDrawXY = FMath::Lerp(SmoothedDrawXY, CurrentXY, DrawSmoothingAlpha);
	}

	const int32 NumSeg = RoutePts.Num() - 1;
	if (NumSeg < 1)
	{
		LastProgress = Out;   // 경로 없음 → 무효
		return Out;
	}

	// 탐색 범위: 직전 세그먼트 ± 윈도우. 없으면 전체.
	int32 Lo = 0;
	int32 Hi = NumSeg - 1;
	if (LastSegment != INDEX_NONE)
	{
		Lo = FMath::Max(0, LastSegment - SearchWindowSegments);
		Hi = FMath::Min(NumSeg - 1, LastSegment + SearchWindowSegments);
	}

	// 각 세그먼트에 투영해 수직거리가 가장 작은 곳을 고른다. 히스테리시스: 직전
	// 세그먼트에 SegmentHysteresisCm 만큼 가산점(점수를 낮춰) 경계 떨림을 막는다.
	int32 BestSeg = Lo;
	float BestT = 0.f;
	float BestLateral = TNumericLimits<float>::Max();
	float BestScore = TNumericLimits<float>::Max();
	for (int32 s = Lo; s <= Hi; ++s)
	{
		float T, Lateral;
		ProjectToSegment(CurrentXY, RoutePts[s], RoutePts[s + 1], T, Lateral);
		const float Score = Lateral - (s == LastSegment ? SegmentHysteresisCm : 0.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestSeg = s;
			BestT = T;
			BestLateral = Lateral;
		}
	}

	LastSegment = BestSeg;

	const float Total = CumCm.Last();
	const float SegLen = CumCm[BestSeg + 1] - CumCm[BestSeg];
	const float Travelled = CumCm[BestSeg] + BestT * SegLen;

	Out.bValid = true;
	Out.SegmentIndex = BestSeg;
	Out.SegmentT = BestT;
	Out.RemainingCm = FMath::Max(0.f, Total - Travelled);
	Out.LateralOffsetCm = BestLateral;
	Out.bOffRoute = BestLateral > OffRouteThresholdCm;
	Out.bArrived = Out.RemainingCm <= ArriveThresholdCm;

	LastProgress = Out;
	return Out;
}

void UNavRouteProgress::GetDrawPolyline(TArray<FVector2D>& OutPoints) const
{
	OutPoints.Reset();
	if (RoutePts.Num() == 0)
	{
		return;
	}

	// 아직 측위 전이면 원본 경로 전체.
	if (!bHasSmoothed || LastProgress.SegmentIndex == INDEX_NONE)
	{
		OutPoints = RoutePts;
		return;
	}

	// [평활된 내 위치] + [다음 노드 … 마지막]. 앞으로 가면 앞 노드가 빠지고,
	// 뒤로 가면 지나쳤던 노드가 다시 낀다(세그먼트 인덱스가 줄어드므로).
	OutPoints.Add(SmoothedDrawXY);
	for (int32 i = LastProgress.SegmentIndex + 1; i < RoutePts.Num(); ++i)
	{
		OutPoints.Add(RoutePts[i]);
	}
}
