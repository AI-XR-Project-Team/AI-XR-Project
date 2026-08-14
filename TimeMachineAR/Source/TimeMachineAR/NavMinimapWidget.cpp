#include "NavMinimapWidget.h"

#include "Components/Widget.h"
#include "Rendering/DrawElements.h"

namespace
{
	/** 링을 근사할 다각형의 변 수. 24 면 반지름 20px 까지 눈으로는 원이다. */
	constexpr int32 RingSegments = 24;

	/** 축척이 0 으로 죽는 것을 막는 하한. */
	constexpr float MinRangeCm = 1.f;
}

// ---------------------------------------------------------------------- 데이터

void UNavMinimapWidget::SetRoute(const FNavRoute& InRoute)
{
	SetWaypoints(InRoute.Waypoints);
}

void UNavMinimapWidget::SetWaypoints(const TArray<FNavWaypoint>& InWaypoints)
{
	RouteXY.Reset(InWaypoints.Num());
	for (const FNavWaypoint& Wp : InWaypoints)
	{
		RouteXY.Emplace(Wp.PosXCm, Wp.PosYCm);
	}
	RefreshEmptyHint();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::ClearRoute()
{
	RouteXY.Reset();
	RefreshEmptyHint();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetCurrentPose(float PosXCm, float PosYCm, float HeadingDeg, bool bHasHeading)
{
	CurrentXY = FVector2D(PosXCm, PosYCm);
	CurrentHeadingDeg = HeadingDeg;
	bCurrentHasHeading = bHasHeading;
	bHasCurrent = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::ClearCurrentPose()
{
	bHasCurrent = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshEmptyHint();
}

void UNavMinimapWidget::RefreshEmptyHint()
{
	if (EmptyHint != nullptr)
	{
		EmptyHint->SetVisibility(HasRoute() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void UNavMinimapWidget::BuildPreviewRoute(TArray<FVector2D>& Out)
{
	// 스케치와 같은 ㄱ자. 단위는 맵 cm 이라 총 길이가 12m 쯤 되어
	// 2m 셰브론이 몇 개 찍히는지 디자이너에서 바로 보인다.
	Out.Reset();
	Out.Emplace(0.f, 0.f);
	Out.Emplace(620.f, 240.f);
	Out.Emplace(980.f, -320.f);
}

// ---------------------------------------------------------------------- 그리기

int32 UNavMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	// 그릴 경로를 고른다. 디자이너에서는 데이터가 없으므로 샘플을 쓴다.
	TArray<FVector2D> PreviewPts;
	const TArray<FVector2D>* WorldPtsPtr = &RouteXY;
	if (RouteXY.Num() == 0)
	{
		if (!IsDesignTime() || !bPreviewInDesigner)
		{
			return Layer;   // 실행 중에 경로가 없으면 EmptyHint 가 대신 보인다.
		}
		BuildPreviewRoute(PreviewPts);
		WorldPtsPtr = &PreviewPts;
	}
	const TArray<FVector2D>& WorldPts = *WorldPtsPtr;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float DrawW = Size.X - 2.f * PaddingPx;
	const float DrawH = Size.Y - 2.f * PaddingPx;
	if (DrawW <= 0.f || DrawH <= 0.f)
	{
		return Layer;   // 위젯이 여백보다 작다. 그릴 자리가 없다.
	}

	// 경로(와 현재 위치)를 모두 담는 맵 경계.
	FVector2D Min = WorldPts[0];
	FVector2D Max = WorldPts[0];
	for (const FVector2D& P : WorldPts)
	{
		Min.X = FMath::Min(Min.X, P.X); Max.X = FMath::Max(Max.X, P.X);
		Min.Y = FMath::Min(Min.Y, P.Y); Max.Y = FMath::Max(Max.Y, P.Y);
	}
	const bool bDrawCurrent = bHasCurrent && !IsDesignTime();
	if (bDrawCurrent)
	{
		Min.X = FMath::Min(Min.X, CurrentXY.X); Max.X = FMath::Max(Max.X, CurrentXY.X);
		Min.Y = FMath::Min(Min.Y, CurrentXY.Y); Max.Y = FMath::Max(Max.Y, CurrentXY.Y);
	}
	Min -= FVector2D(WorldPaddingCm, WorldPaddingCm);
	Max += FVector2D(WorldPaddingCm, WorldPaddingCm);

	const float RangeX = FMath::Max(Max.X - Min.X, MinRangeCm);
	const float RangeY = FMath::Max(Max.Y - Min.Y, MinRangeCm);
	const float Scale = FMath::Min(DrawW / RangeX, DrawH / RangeY);

	// 남는 쪽은 가운데로 민다. 세로로 긴 경로가 왼쪽에 붙어 버리는 것 방지.
	const float OffX = PaddingPx + (DrawW - RangeX * Scale) * 0.5f;
	const float OffY = PaddingPx + (DrawH - RangeY * Scale) * 0.5f;

	// 맵(cm) → 위젯 로컬(px). y 를 뒤집어 맵 +Y 가 화면 위로 간다.
	auto ToLocal = [&](const FVector2D& W)
	{
		return FVector2D(OffX + (W.X - Min.X) * Scale,
		                 Size.Y - OffY - (W.Y - Min.Y) * Scale);
	};

	TArray<FVector2D> LocalPts;
	LocalPts.Reserve(WorldPts.Num());
	for (const FVector2D& W : WorldPts)
	{
		LocalPts.Add(ToLocal(W));
	}

	const FPaintGeometry Geom = AllottedGeometry.ToPaintGeometry();

	// 띠 → 셰브론 → 노드 링 순서. 뒤에 그리는 것이 위에 얹힌다. 링을 마지막에
	// 그려야 두 띠가 꺾이며 어긋난 이음매를 링이 덮어 준다.
	++Layer;
	for (int32 i = 0; i < LocalPts.Num() - 1; ++i)
	{
		PaintBand(OutDrawElements, Layer, Geom, LocalPts[i], LocalPts[i + 1]);
	}

	++Layer;
	PaintChevrons(OutDrawElements, Layer, Geom, LocalPts, WorldPts, Scale);

	++Layer;
	const int32 Last = LocalPts.Num() - 1;
	for (int32 i = 0; i <= Last; ++i)
	{
		FLinearColor Color = NodeRingColor;
		if (bDistinguishEndpoints)
		{
			if (i == 0)         { Color = StartNodeColor; }
			else if (i == Last) { Color = DestNodeColor; }
		}
		PaintRing(OutDrawElements, Layer, Geom, LocalPts[i],
			NodeRingRadiusPx, NodeRingThicknessPx, Color);
	}

	// 현재 위치. 측위 계층이 붙기 전에는 여기 오지 않는다.
	if (bDrawCurrent)
	{
		++Layer;
		const FVector2D C = ToLocal(CurrentXY);

		// 채운 원 대신 반지름만큼 두꺼운 링을 그린다. Slate 에는 채운 원
		// 프리미티브가 없고, 삼각형 팬을 직접 만드는 것보다 이쪽이 짧다.
		PaintRing(OutDrawElements, Layer, Geom, C,
			CurrentPoseRadiusPx * 0.5f, CurrentPoseRadiusPx, CurrentPoseColor);

		if (bCurrentHasHeading)
		{
			// 맵 heading 은 +X 기준 CCW. 화면은 y 가 아래로 늘어나므로 sin 을 뒤집는다.
			const float Rad = FMath::DegreesToRadians(CurrentHeadingDeg);
			const FVector2D Dir(FMath::Cos(Rad), -FMath::Sin(Rad));
			TArray<FVector2D> Tick;
			Tick.Add(C + Dir * CurrentPoseRadiusPx);
			Tick.Add(C + Dir * (CurrentPoseRadiusPx * 2.2f));
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geom, Tick,
				ESlateDrawEffect::None, CurrentPoseColor, true, 2.f);
		}
	}

	return Layer;
}

void UNavMinimapWidget::PaintBand(FSlateWindowElementList& Out, int32 Layer,
	const FPaintGeometry& Geom, const FVector2D& A, const FVector2D& B) const
{
	const FVector2D Delta = B - A;
	const float Len = Delta.Size();
	if (Len < KINDA_SMALL_NUMBER)
	{
		return;   // 같은 자리에 놓인 웨이포인트 두 개. 방향을 못 구한다.
	}

	const FVector2D Dir = Delta / Len;
	const FVector2D Normal(-Dir.Y, Dir.X);
	const FVector2D Half = Normal * (PathBandWidthPx * 0.5f);

	TArray<FVector2D> Side;
	Side.Add(A + Half);
	Side.Add(B + Half);
	FSlateDrawElement::MakeLines(Out, Layer, Geom, Side,
		ESlateDrawEffect::None, PathColor, true, PathLineThicknessPx);

	Side[0] = A - Half;
	Side[1] = B - Half;
	FSlateDrawElement::MakeLines(Out, Layer, Geom, Side,
		ESlateDrawEffect::None, PathColor, true, PathLineThicknessPx);
}

void UNavMinimapWidget::PaintChevrons(FSlateWindowElementList& Out, int32 Layer,
	const FPaintGeometry& Geom, const TArray<FVector2D>& LocalPts,
	const TArray<FVector2D>& WorldPts, float Scale) const
{
	if (LocalPts.Num() < 2 || ArrowSpacingCm <= 0.f)
	{
		return;
	}

	// 화면에서 너무 촘촘하면 2m 의 정수배로 벌린다. "한 칸 = 2m" 를 유지하려고
	// 정수배로만 늘린다.
	float SpacingCm = ArrowSpacingCm;
	if (MinArrowSpacingPx > 0.f && SpacingCm * Scale < MinArrowSpacingPx)
	{
		const float Mult = FMath::CeilToFloat(MinArrowSpacingPx / FMath::Max(SpacingCm * Scale, KINDA_SMALL_NUMBER));
		SpacingCm *= Mult;
	}

	const float HalfSpan = PathBandWidthPx * 0.5f;

	// 첫 셰브론은 반 칸 뒤에서 시작한다. 0 에서 시작하면 출발 노드 링과 겹친다.
	float NextAtCm = SpacingCm * 0.5f;
	float TravelledCm = 0.f;

	for (int32 i = 0; i < WorldPts.Num() - 1; ++i)
	{
		const FVector2D WA = WorldPts[i];
		const FVector2D WB = WorldPts[i + 1];
		const float SegCm = (WB - WA).Size();
		if (SegCm < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D LA = LocalPts[i];
		const FVector2D LB = LocalPts[i + 1];
		const FVector2D LDelta = LB - LA;
		const float LLen = LDelta.Size();
		if (LLen < KINDA_SMALL_NUMBER)
		{
			TravelledCm += SegCm;
			continue;
		}
		const FVector2D Dir = LDelta / LLen;
		const FVector2D Normal(-Dir.Y, Dir.X);

		// 이 구간 안에 들어오는 셰브론을 전부 찍는다. 투영이 어파인이라
		// 맵에서의 비율 t 를 로컬 점에 그대로 써도 같은 자리가 나온다.
		while (NextAtCm <= TravelledCm + SegCm)
		{
			const float T = (NextAtCm - TravelledCm) / SegCm;
			const FVector2D P = LA + LDelta * T;

			TArray<FVector2D> Chevron;
			Chevron.Add(P - Dir * (ArrowLengthPx * 0.5f) + Normal * HalfSpan);
			Chevron.Add(P + Dir * (ArrowLengthPx * 0.5f));
			Chevron.Add(P - Dir * (ArrowLengthPx * 0.5f) - Normal * HalfSpan);
			FSlateDrawElement::MakeLines(Out, Layer, Geom, Chevron,
				ESlateDrawEffect::None, PathColor, true, ArrowThicknessPx);

			NextAtCm += SpacingCm;
		}

		TravelledCm += SegCm;
	}
}

void UNavMinimapWidget::PaintRing(FSlateWindowElementList& Out, int32 Layer,
	const FPaintGeometry& Geom, const FVector2D& Center, float Radius,
	float Thickness, const FLinearColor& Color) const
{
	TArray<FVector2D> Poly;
	Poly.Reserve(RingSegments + 1);
	for (int32 i = 0; i <= RingSegments; ++i)
	{
		const float A = (2.f * PI * i) / RingSegments;
		Poly.Emplace(Center.X + FMath::Cos(A) * Radius,
		             Center.Y + FMath::Sin(A) * Radius);
	}
	FSlateDrawElement::MakeLines(Out, Layer, Geom, Poly,
		ESlateDrawEffect::None, Color, true, Thickness);
}
