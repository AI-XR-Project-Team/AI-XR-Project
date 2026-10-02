#include "NavFloorGuide.h"

void FNavFloorGuide::ProjectToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B,
	float& OutT, float& OutLateral)
{
	const FVector2D AB = B - A;
	const float LenSq = AB.SizeSquared();
	if (LenSq <= KINDA_SMALL_NUMBER)
	{
		OutT = 0.f;
		OutLateral = FVector2D::Distance(P, A);
		return;
	}
	const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f);
	const FVector2D Proj = A + AB * T;
	OutT = T;
	OutLateral = FVector2D::Distance(P, Proj);
}

FVector FNavFloorGuide::OffsetForStep(const FVector& WorldCenter, const FVector& WorldDir,
	bool bLeft, float OffsetCm, float LateralScale)
{
	FVector2D Dir(WorldDir.X, WorldDir.Y);
	if (Dir.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return WorldCenter;
	}
	Dir.Normalize();
	const FVector2D Right(-Dir.Y, Dir.X);   // UE 왼손: Forward (1,0) → Right (0,1).
	const float Signed = (bLeft ? -1.f : 1.f) * FMath::Abs(OffsetCm) * FMath::Clamp(LateralScale, 0.f, 1.f);
	return WorldCenter + FVector(Right.X * Signed, Right.Y * Signed, 0.f);
}

namespace
{
	/** 호길이 Arc 가 속한 세그먼트(길이 0 세그먼트는 앞/뒤 이웃으로 대체). 없으면 INDEX_NONE. */
	int32 SegmentAtArc(const TArray<float>& Cum, float Arc)
	{
		const int32 SegCount = Cum.Num() - 1;
		int32 Seg = 0;
		while (Seg < SegCount - 1 && Arc > Cum[Seg + 1])
		{
			++Seg;
		}
		// 중복 점(길이 0)이면 방향이 없다 — 앞으로, 그래도 없으면 뒤로 찾는다.
		int32 Fwd = Seg;
		while (Fwd < SegCount && Cum[Fwd + 1] - Cum[Fwd] <= KINDA_SMALL_NUMBER) { ++Fwd; }
		if (Fwd < SegCount) { return Fwd; }
		int32 Back = Seg;
		while (Back >= 0 && Cum[Back + 1] - Cum[Back] <= KINDA_SMALL_NUMBER) { --Back; }
		return Back;
	}

	FVector2D PointAtArc(const TArray<FVector2D>& Pts, const TArray<float>& Cum, float Arc)
	{
		const float Clamped = FMath::Clamp(Arc, 0.f, Cum.Last());
		const int32 Seg = SegmentAtArc(Cum, Clamped);
		if (Seg == INDEX_NONE) { return Pts[0]; }
		const float SegLen = Cum[Seg + 1] - Cum[Seg];
		const float T = FMath::Clamp((Clamped - Cum[Seg]) / SegLen, 0.f, 1.f);
		return Pts[Seg] + (Pts[Seg + 1] - Pts[Seg]) * T;
	}

	FVector2D TangentAtArc(const TArray<FVector2D>& Pts, const TArray<float>& Cum, float Arc)
	{
		const int32 Seg = SegmentAtArc(Cum, FMath::Clamp(Arc, 0.f, Cum.Last()));
		if (Seg == INDEX_NONE) { return FVector2D(1.f, 0.f); }
		return (Pts[Seg + 1] - Pts[Seg]).GetSafeNormal();
	}
}

void FNavFloorGuide::BuildPlacements(const TArray<FVector2D>& RoutePts, const FVector2D& UserXY,
	float SpacingCm, float RangeCm, TArray<FNavFloorPlacement>& Out)
{
	Out.Reset();
	if (RoutePts.Num() < 2 || SpacingCm <= 0.f || RangeCm <= 0.f)
	{
		return;
	}

	const int32 SegCount = RoutePts.Num() - 1;

	// 세그먼트별 길이와 시작점부터의 누적 호길이(Cum[i] = 정점 i 까지). Cum.Last() = 총길이.
	TArray<float> Cum;
	Cum.SetNumUninitialized(RoutePts.Num());
	Cum[0] = 0.f;
	for (int32 i = 0; i < SegCount; ++i)
	{
		Cum[i + 1] = Cum[i] + FVector2D::Distance(RoutePts[i], RoutePts[i + 1]);
	}
	const float TotalLen = Cum.Last();
	if (TotalLen <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// (1) 사용자를 폴리라인에 투영 → 가장 가까운 세그먼트의 시작 호길이.
	float BestLateral = TNumericLimits<float>::Max();
	float StartArc = 0.f;
	for (int32 i = 0; i < SegCount; ++i)
	{
		float T, Lat;
		ProjectToSegment(UserXY, RoutePts[i], RoutePts[i + 1], T, Lat);
		if (Lat < BestLateral)
		{
			BestLateral = Lat;
			const float SegLen = Cum[i + 1] - Cum[i];
			StartArc = Cum[i] + T * SegLen;
		}
	}

	// (2) 발자국을 **경로 시작 기준 k·SpacingCm 의 고정 그리드**에만 놓는다(월드 고정).
	// 사용자가 걸으면 [StartArc, StartArc+RangeCm] 창만 앞으로 밀려 뒤 발자국이 빠지고
	// 앞에서 새 발자국이 들어오되, 각 발자국의 월드 위치는 그리드에 고정 → 미끄러지지 않는다.
	const float NearCm = SpacingCm * 0.5f;                 // 이보다 가까운 그리드 점은 숨김(발밑 겹침 방지).
	const float WindowStart = StartArc + NearCm;
	const float EndArc = FMath::Min(StartArc + RangeCm, TotalLen);
	const int32 FirstK = FMath::CeilToInt(WindowStart / SpacingCm);   // WindowStart 이상인 첫 그리드 점.
	const float HalfSpan = SpacingCm * 0.5f;

	for (int32 K = FirstK; K * SpacingCm <= EndArc + KINDA_SMALL_NUMBER; ++K)
	{
		const float Arc = FMath::Min(K * SpacingCm, TotalLen);

		FNavFloorPlacement P;
		P.StepIndex = K;
		P.MapPos = PointAtArc(RoutePts, Cum, Arc);

		// (3) 진행 방향 = 앞뒤 ½Spacing 현. 코너 정점 위 발은 두 세그먼트 사이 각으로 눕는다.
		const FVector2D Behind = PointAtArc(RoutePts, Cum, Arc - HalfSpan);
		const FVector2D Ahead = PointAtArc(RoutePts, Cum, Arc + HalfSpan);
		FVector2D Chord = Ahead - Behind;
		if (Chord.SizeSquared() <= KINDA_SMALL_NUMBER)
		{
			Chord = TangentAtArc(RoutePts, Cum, Arc);   // 현이 0(경로가 되돌아옴)이면 세그먼트 접선.
		}
		if (Chord.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			Chord.Normalize();
			P.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Chord.Y, Chord.X));
		}
		else if (Out.Num() > 0)
		{
			P.YawDeg = Out.Last().YawDeg;
		}

		// 급회전이면 가로 오프셋을 줄인다(코너 바깥으로 벽을 뚫고 나가지 않게).
		const FVector2D TanBehind = TangentAtArc(RoutePts, Cum, FMath::Max(Arc - HalfSpan, 0.f));
		const FVector2D TanAhead = TangentAtArc(RoutePts, Cum, FMath::Min(Arc + HalfSpan, TotalLen));
		const float CosTurn = FMath::Clamp(FVector2D::DotProduct(TanBehind, TanAhead), -1.f, 1.f);
		const float TurnDeg = FMath::RadiansToDegrees(FMath::Acos(CosTurn));
		P.LateralScale = 1.f - FMath::Clamp(
			(TurnDeg - TurnReduceStartDeg) / (TurnReduceEndDeg - TurnReduceStartDeg), 0.f, 1.f);

		Out.Add(P);
	}
}
