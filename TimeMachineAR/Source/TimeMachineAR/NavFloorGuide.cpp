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

	// (2) 시작 호길이 + SpacingCm 부터, RangeCm(또는 경로 끝)까지 SpacingCm 간격.
	const float EndArc = FMath::Min(StartArc + RangeCm, TotalLen);

	// 지금 훑는 세그먼트 커서. 호길이가 단조 증가하므로 세그먼트도 앞으로만 간다.
	int32 Seg = 0;
	for (float Arc = StartArc + SpacingCm; Arc <= EndArc + KINDA_SMALL_NUMBER; Arc += SpacingCm)
	{
		const float ClampedArc = FMath::Min(Arc, TotalLen);

		// ClampedArc 를 담는 세그먼트로 커서를 전진.
		while (Seg < SegCount - 1 && ClampedArc > Cum[Seg + 1])
		{
			++Seg;
		}

		const float SegLen = Cum[Seg + 1] - Cum[Seg];
		const float LocalT = (SegLen > KINDA_SMALL_NUMBER)
			? FMath::Clamp((ClampedArc - Cum[Seg]) / SegLen, 0.f, 1.f)
			: 0.f;

		const FVector2D A = RoutePts[Seg];
		const FVector2D B = RoutePts[Seg + 1];

		FNavFloorPlacement P;
		P.MapPos = A + (B - A) * LocalT;

		FVector2D Dir = B - A;
		if (Dir.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			Dir.Normalize();
			P.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		}
		else if (Out.Num() > 0)
		{
			P.YawDeg = Out.Last().YawDeg;   // 0 길이 세그먼트면 직전 방향 유지.
		}

		Out.Add(P);
	}
}
