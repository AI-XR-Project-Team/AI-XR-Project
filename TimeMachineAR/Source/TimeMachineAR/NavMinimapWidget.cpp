#include "NavMinimapWidget.h"

#include "Components/Widget.h"
#include "Rendering/DrawElements.h"
#include "NavRouteProgress.h"
#include "NavFullMapWidget.h"
#include "NavClient.h"   // LogNav

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
	// 동적 경로선·진행률 계산기에도 같은 폴리라인을 실어 준다.
	GetRouteProgress()->SetRoutePoints(RouteXY);
	bWasOffRoute = false;
	RefreshEmptyHint();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetGraph(const FNavGraph& InGraph)
{
	Graph = InGraph;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetDestinationNode(const FString& NodeId)
{
	DestinationNodeId = NodeId;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::ClearRoute()
{
	RouteXY.Reset();
	GetRouteProgress()->Reset();
	bWasOffRoute = false;
	RefreshEmptyHint();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetRouteXY(const TArray<FVector2D>& InRouteXY)
{
	RouteXY = InRouteXY;
	GetRouteProgress()->SetRoutePoints(RouteXY);
	bWasOffRoute = false;
	RefreshEmptyHint();
	Invalidate(EInvalidateWidgetReason::Paint);
}

UNavRouteProgress* UNavMinimapWidget::GetRouteProgress()
{
	if (RouteProgress == nullptr)
	{
		RouteProgress = NewObject<UNavRouteProgress>(this);
	}
	return RouteProgress;
}

void UNavMinimapWidget::OpenFullMap()
{
	if (FullMapWidgetClass == nullptr)
	{
		UE_LOG(LogNav, Warning,
			TEXT("[minimap] OpenFullMap: FullMapWidgetClass 가 비어 있습니다. WBP_NavMinimap 의 ")
			TEXT("FullMapWidgetClass 를 WBP_NavMinimapFull 로 지정하세요."));
		return;
	}
	// 이미 화면에 떠 있으면 중복 오픈 방지. 단 RemoveFromParent 로 닫힌 위젯은
	// UObject 가 바로 파괴되지 않아 IsValid 만으론 "닫힘"을 구분 못 한다(닫아도 계속
	// valid → 재오픈이 막힘). 그래서 IsInViewport 로 실제 표시 여부를 본다.
	if (FullMapInstance.IsValid() && FullMapInstance->IsInViewport())
	{
		return;   // 이미 떠 있다.
	}

	UNavFullMapWidget* W = CreateWidget<UNavFullMapWidget>(GetWorld(), FullMapWidgetClass);
	if (W == nullptr)
	{
		return;
	}

	W->ApplyState(Graph, RouteXY, bHasCurrent && !IsDesignTime(),
		CurrentXY, CurrentHeadingDeg, bCurrentHasHeading, DestinationNodeId);
	W->OnDestinationChosen.AddDynamic(this, &UNavMinimapWidget::HandleDestinationChosen);
	W->AddToViewport(100);
	FullMapInstance = W;
}

void UNavMinimapWidget::HandleDestinationChosen(const FString& NodeId)
{
	DestinationNodeId = NodeId;
	Invalidate(EInvalidateWidgetReason::Paint);
	OnDestinationChosen.Broadcast(NodeId);   // BP → NavClient.RequestRoute
}

void UNavMinimapWidget::SetCurrentPose(float PosXCm, float PosYCm, float HeadingDeg, bool bHasHeading)
{
	CurrentXY = FVector2D(PosXCm, PosYCm);
	CurrentHeadingDeg = HeadingDeg;
	bCurrentHasHeading = bHasHeading;
	bHasCurrent = true;

	// 동적 경로선·이탈 판정. 4단계는 이탈이 서면 로그만 남긴다(자동 reroute 는 5단계).
	UNavRouteProgress* Progress = GetRouteProgress();
	if (Progress->HasRoute())
	{
		const FNavProgress P = Progress->UpdatePose(CurrentXY);
		if (P.bOffRoute && !bWasOffRoute)
		{
			UE_LOG(LogNav, Warning,
				TEXT("[minimap] 경로 이탈 감지 (lateral=%.0fcm > %.0fcm). 4단계는 로그만 남깁니다."),
				P.LateralOffsetCm, Progress->OffRouteThresholdCm);
		}
		bWasOffRoute = P.bOffRoute;
	}

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
	GetRouteProgress();   // 미리 만들어 둔다.
	RefreshEmptyHint();
}

// ---------------------------------------------------------------------- 좌표/조회

FVector2D UNavMinimapWidget::WorldToLocal(const FVector2D& W) const
{
	return FVector2D(
		CachedScreenOrigin.X + (W.X - CachedWorldOrigin.X) * CachedScale,
		CachedScreenOrigin.Y - (W.Y - CachedWorldOrigin.Y) * CachedScale);
}

bool UNavMinimapWidget::FindNodeAtLocal(const FVector2D& LocalPos, float RadiusPx,
	FString& OutNodeId) const
{
	if (!bHasCachedTransform || Graph.Nodes.Num() == 0)
	{
		return false;
	}
	const float R2 = RadiusPx * RadiusPx;
	float BestD2 = R2;
	bool bFound = false;
	for (const FNavMapNode& Node : Graph.Nodes)
	{
		const FVector2D L = WorldToLocal(FVector2D(Node.PosXCm, Node.PosYCm));
		const float D2 = static_cast<float>((L - LocalPos).SizeSquared());
		if (D2 <= BestD2)
		{
			BestD2 = D2;
			OutNodeId = Node.NodeId;
			bFound = true;
		}
	}
	return bFound;
}

const TArray<FVector2D>& UNavMinimapWidget::ResolveRoutePts(
	TArray<FVector2D>& DynamicScratch, TArray<FVector2D>& PreviewScratch) const
{
	// 측위 중이면 동적 경로선([평활된 내 위치] + 남은 노드).
	if (bHasCurrent && !IsDesignTime() && RouteProgress != nullptr && RouteProgress->HasRoute())
	{
		RouteProgress->GetDrawPolyline(DynamicScratch);
		if (DynamicScratch.Num() > 0)
		{
			return DynamicScratch;
		}
	}
	if (RouteXY.Num() > 0)
	{
		return RouteXY;
	}
	if (IsDesignTime() && bPreviewInDesigner)
	{
		BuildPreviewRoute(PreviewScratch);
		return PreviewScratch;
	}
	return RouteXY;   // 비어 있음
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

	bHasCachedTransform = false;   // 이번 프레임 변환을 새로 잡는다.

	const bool bFull = (Mode == ENavMinimapMode::Full);
	const bool bDrawCurrent = bHasCurrent && !IsDesignTime();

	// 그릴 경로 폴리라인(맵 cm): 측위 중이면 동적 선, 아니면 정적/미리보기.
	TArray<FVector2D> DynamicScratch, PreviewScratch;
	const TArray<FVector2D>& WorldPts = ResolveRoutePts(DynamicScratch, PreviewScratch);
	const bool bHaveRoute = WorldPts.Num() > 0;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float DrawW = Size.X - 2.f * PaddingPx;
	const float DrawH = Size.Y - 2.f * PaddingPx;
	if (DrawW <= 0.f || DrawH <= 0.f)
	{
		return Layer;   // 위젯이 여백보다 작다. 그릴 자리가 없다.
	}

	// -------- 모드별 맵→로컬 변환 결정 --------
	if (bFull)
	{
		// 전체 지도: 그래프(벽·구조물·노드)와 경로·현재위치를 모두 담아 맞춘다.
		bool bAny = false;
		FVector2D Min(0, 0), Max(0, 0);
		auto Grow = [&](const FVector2D& P)
		{
			if (!bAny) { Min = Max = P; bAny = true; }
			else { Min.X = FMath::Min(Min.X, P.X); Max.X = FMath::Max(Max.X, P.X);
			       Min.Y = FMath::Min(Min.Y, P.Y); Max.Y = FMath::Max(Max.Y, P.Y); }
		};
		for (const FVector2D& V : Graph.Outline) { Grow(V); }
		for (const FNavMapNode& N : Graph.Nodes) { Grow(FVector2D(N.PosXCm, N.PosYCm)); }
		for (const FNavObstacle& O : Graph.Obstacles)
		{
			Grow(FVector2D(O.X0, O.Y0)); Grow(FVector2D(O.X1, O.Y1));
		}
		for (const FVector2D& P : WorldPts) { Grow(P); }
		if (bDrawCurrent) { Grow(CurrentXY); }
		if (!bAny)
		{
			return Layer;   // 그릴 것이 아무것도 없다.
		}
		Min -= FVector2D(WorldPaddingCm, WorldPaddingCm);
		Max += FVector2D(WorldPaddingCm, WorldPaddingCm);

		const float RangeX = FMath::Max(Max.X - Min.X, MinRangeCm);
		const float RangeY = FMath::Max(Max.Y - Min.Y, MinRangeCm);
		CachedScale = FMath::Min(DrawW / RangeX, DrawH / RangeY);
		const float OffX = PaddingPx + (DrawW - RangeX * CachedScale) * 0.5f;
		const float OffY = PaddingPx + (DrawH - RangeY * CachedScale) * 0.5f;
		CachedWorldOrigin = Min;
		CachedScreenOrigin = FVector2D(OffX, Size.Y - OffY);
	}
	else
	{
		// Follow: 고정 배율, 내 위치(없으면 경로 시작점)를 화면 중앙에 둔다. north-up.
		CachedScale = DrawW / FMath::Max(FollowWindowCm, MinRangeCm);
		FVector2D CenterW = FVector2D::ZeroVector;
		if (bDrawCurrent)          { CenterW = CurrentXY; }
		else if (bHaveRoute)       { CenterW = WorldPts[0]; }
		else                       { return Layer; }   // 중심 잡을 근거가 없다.
		CachedWorldOrigin = CenterW;
		CachedScreenOrigin = FVector2D(Size.X * 0.5f, Size.Y * 0.5f);
	}
	bHasCachedTransform = true;

	const FPaintGeometry Geom = AllottedGeometry.ToPaintGeometry();

	// -------- Full 모드: 벽·구조물·전체 엣지·전체 노드를 먼저 깐다 --------
	if (bFull && Graph.Nodes.Num() > 0)
	{
		PaintFullMapBase(OutDrawElements, Layer, Geom);
	}

	if (!bHaveRoute)
	{
		// 경로가 없어도 Full 모드면 전체 지도는 이미 그렸다. 현재 위치만 마저 찍는다.
		if (bDrawCurrent)
		{
			++Layer;
			const FVector2D COnly = WorldToLocal(CurrentXY);
			PaintRing(OutDrawElements, Layer, Geom, COnly,
				CurrentPoseRadiusPx * 0.5f, CurrentPoseRadiusPx, CurrentPoseColor);
		}
		return Layer;
	}

	TArray<FVector2D> LocalPts;
	LocalPts.Reserve(WorldPts.Num());
	for (const FVector2D& W : WorldPts)
	{
		LocalPts.Add(WorldToLocal(W));
	}

	// 띠 → 셰브론 → 노드 링 순서. 뒤에 그리는 것이 위에 얹힌다. 링을 마지막에
	// 그려야 두 띠가 꺾이며 어긋난 이음매를 링이 덮어 준다.
	++Layer;
	for (int32 i = 0; i < LocalPts.Num() - 1; ++i)
	{
		PaintBand(OutDrawElements, Layer, Geom, LocalPts[i], LocalPts[i + 1]);
	}

	++Layer;
	PaintChevrons(OutDrawElements, Layer, Geom, LocalPts, WorldPts, CachedScale);

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
		const FVector2D C = WorldToLocal(CurrentXY);

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

void UNavMinimapWidget::PaintFilledRect(FSlateWindowElementList& Out, int32 Layer,
	const FPaintGeometry& Geom, const FVector2D& LocalA, const FVector2D& LocalB,
	const FLinearColor& Color) const
{
	const float MinX = FMath::Min(LocalA.X, LocalB.X);
	const float MaxX = FMath::Max(LocalA.X, LocalB.X);
	const float MinY = FMath::Min(LocalA.Y, LocalB.Y);
	const float MaxY = FMath::Max(LocalA.Y, LocalB.Y);
	if (MaxX - MinX < 1.f || MaxY - MinY < 1.f)
	{
		return;
	}
	// Slate 에 채움 프리미티브(브러시 없이)가 없어 수평선으로 메운다. 구조물은
	// 작고 개수가 적어 비용은 무시할 수 있다.
	for (float Y = MinY; Y <= MaxY; Y += 2.f)
	{
		TArray<FVector2D> Line;
		Line.Add(FVector2D(MinX, Y));
		Line.Add(FVector2D(MaxX, Y));
		FSlateDrawElement::MakeLines(Out, Layer, Geom, Line,
			ESlateDrawEffect::None, Color, false, 2.f);
	}
}

void UNavMinimapWidget::PaintFullMapBase(FSlateWindowElementList& Out, int32& Layer,
	const FPaintGeometry& Geom) const
{
	// 1) 내부 구조물(채운 사각형) — 맨 아래.
	++Layer;
	for (const FNavObstacle& O : Graph.Obstacles)
	{
		const FVector2D A = WorldToLocal(FVector2D(O.X0, O.Y0));
		const FVector2D B = WorldToLocal(FVector2D(O.X1, O.Y1));
		PaintFilledRect(Out, Layer, Geom, A, B, ObstacleColor);
	}

	// 2) 벽(외곽선) — 폐곡선으로 잇는다.
	if (Graph.Outline.Num() >= 2)
	{
		++Layer;
		TArray<FVector2D> Poly;
		Poly.Reserve(Graph.Outline.Num() + 1);
		for (const FVector2D& V : Graph.Outline)
		{
			Poly.Add(WorldToLocal(V));
		}
		Poly.Add(WorldToLocal(Graph.Outline[0]));   // 닫는다
		FSlateDrawElement::MakeLines(Out, Layer, Geom, Poly,
			ESlateDrawEffect::None, WallColor, true, WallThicknessPx);
	}

	// 3) 전체 엣지 — 옅은 회색. 노드 id → 좌표를 먼저 인덱싱.
	TMap<FString, FVector2D> NodeXY;
	NodeXY.Reserve(Graph.Nodes.Num());
	for (const FNavMapNode& N : Graph.Nodes)
	{
		NodeXY.Add(N.NodeId, FVector2D(N.PosXCm, N.PosYCm));
	}
	++Layer;
	for (const FNavMapEdge& E : Graph.Edges)
	{
		const FVector2D* A = NodeXY.Find(E.FromNodeId);
		const FVector2D* B = NodeXY.Find(E.ToNodeId);
		if (A != nullptr && B != nullptr)
		{
			TArray<FVector2D> Seg;
			Seg.Add(WorldToLocal(*A));
			Seg.Add(WorldToLocal(*B));
			FSlateDrawElement::MakeLines(Out, Layer, Geom, Seg,
				ESlateDrawEffect::None, GraphEdgeColor, true, GraphEdgeThicknessPx);
		}
	}

	// 4) 전체 노드 — 링. 목적지로 지정된 노드는 채워서 구분.
	++Layer;
	for (const FNavMapNode& N : Graph.Nodes)
	{
		const FVector2D C = WorldToLocal(FVector2D(N.PosXCm, N.PosYCm));
		if (!DestinationNodeId.IsEmpty() && N.NodeId == DestinationNodeId)
		{
			// 채운 링(반지름의 절반 위치에 반지름만 한 두께) = 꽉 찬 점.
			PaintRing(Out, Layer, Geom, C,
				GraphNodeRadiusPx * 0.5f, GraphNodeRadiusPx, DestinationNodeColor);
		}
		else
		{
			PaintRing(Out, Layer, Geom, C, GraphNodeRadiusPx, 2.f, GraphNodeColor);
		}
	}
}
