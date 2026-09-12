#include "NavMinimapWidget.h"

#include "Components/Widget.h"
#include "Rendering/DrawElements.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "NavRouteProgress.h"
#include "NavFullMapWidget.h"
#include "NavGuideLogWidget.h"   // 안내 로그 오버레이(§D)
#include "Misc/App.h"            // FApp::GetCurrentTime (표출 판정 타임스탬프)
#include "NavDestinations.h"     // 목적지 색·아이콘 규칙(§B)
#include "NavLocalizer.h"   // 측위 품질(reroute 게이트)·현재 pose
#include "NavClient.h"      // LogNav, Reroute
#include "NavFloorGuideActor.h"    // §C-1 바닥 발자국
#include "NavDestMarkerWidget.h"   // §C-2 목적지 마름모 HUD

namespace
{
	/** 링을 근사할 다각형의 변 수. 24 면 반지름 20px 까지 눈으로는 원이다. */
	constexpr int32 RingSegments = 24;

	/** 축척이 0 으로 죽는 것을 막는 하한. */
	constexpr float MinRangeCm = 1.f;

	FString SkinPath(const TCHAR* Name)
	{
		return FString::Printf(TEXT("/Game/UI/Nav/Skin/%s.%s"), Name, Name);
	}
	const TCHAR* PoiSkin(const FNavMapNode& Node)
	{
		switch (FNavDestinations::Classify(Node.NodeType))
		{
		case ENavDestKind::Facility: return TEXT("poi_toilet");
		case ENavDestKind::Entrance: return TEXT("poi_entrance");
		case ENavDestKind::Exhibit:
			switch (FNavDestinations::DinoIndexFromLabel(Node.Label))
			{
			case 1: return TEXT("poi_triceratops");
			case 2: return TEXT("poi_brachiosaurus");
			case 3: return TEXT("poi_trex");
			// The supplied ankylosaurus POI depicts a triceratops. Keep the existing correct icon.
			default: return nullptr;
			}
		default: return nullptr;
		}
	}
}

// ---------------------------------------------------------------------- 데이터

void UNavMinimapWidget::SetRoute(const FNavRoute& InRoute)
{
	SetWaypoints(InRoute.Waypoints);
	// 턴바이턴 안내(5-D)를 위해 서버 steps 도 진행률 계산기에 실어 준다.
	// SetWaypoints 가 이미 SetRoutePoints 로 누적표를 세운 뒤라 구간표가 바르게 잡힌다.
	GetRouteProgress()->SetSteps(InRoute.Steps);
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
	OffRouteSinceSeconds = -1.f;   // 새 경로 → 이탈 타이머 리셋.
	RefreshEmptyHint();
	if (UNavMinimapWidget* Full = GetOpenFullMapView()) { Full->SetWaypoints(InWaypoints); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetGraph(const FNavGraph& InGraph)
{
	Graph = InGraph;
	RebuildIconBrushes();   // 목적지 아이콘 텍스처를 미리 로드(paint 중 로드 금지).
	if (UNavMinimapWidget* Full = GetOpenFullMapView()) { Full->SetGraph(InGraph); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetDestinationNode(const FString& NodeId)
{
	DestinationNodeId = NodeId;
	PushDestinationToMarker();   // §C-2 목적지 마름모 갱신.
	if (UNavMinimapWidget* Full = GetOpenFullMapView()) { Full->SetDestinationNode(NodeId); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::ClearRoute()
{
	RouteXY.Reset();
	GetRouteProgress()->Reset();
	bWasOffRoute = false;
	OffRouteSinceSeconds = -1.f;
	if (FloorGuide != nullptr) { FloorGuide->HideGuide(); }   // §C-1 안내 종료 → 발자국 숨김.
	if (DestMarker != nullptr) { DestMarker->ClearDestination(); }   // §C-2 마름모 내림.
	RefreshEmptyHint();
	if (UNavMinimapWidget* Full = GetOpenFullMapView()) { Full->ClearRoute(); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::SetRouteXY(const TArray<FVector2D>& InRouteXY)
{
	RouteXY = InRouteXY;
	GetRouteProgress()->SetRoutePoints(RouteXY);
	bWasOffRoute = false;
	OffRouteSinceSeconds = -1.f;
	RefreshEmptyHint();
	if (UNavMinimapWidget* Full = GetOpenFullMapView()) { Full->SetRouteXY(InRouteXY); }
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
	W->OnClosed.AddDynamic(this, &UNavMinimapWidget::HandleFullMapClosed);
	W->AddToViewport(100);
	FullMapInstance = W;

	OnFullMapOpenChanged.Broadcast(true);   // §D 안내 로그: "확대 지도" 상태.
}

void UNavMinimapWidget::HandleDestinationChosen(const FString& NodeId)
{
	DestinationNodeId = NodeId;
	PushDestinationToMarker();   // §C-2 목적지 마름모 갱신.
	Invalidate(EInvalidateWidgetReason::Paint);
	OnDestinationChosen.Broadcast(NodeId);   // BP → NavClient.RequestRoute
}

void UNavMinimapWidget::HandleFullMapClosed()
{
	OnFullMapOpenChanged.Broadcast(false);
}

void UNavMinimapWidget::EnsureGuideLog()
{
	// Follow(HUD) 인스턴스만, 디자이너가 아닐 때만 만든다. Full(MapView)·미리보기는 제외.
	if (Mode != ENavMinimapMode::Follow || IsDesignTime() || GuideLog != nullptr)
	{
		return;
	}
	// 클래스가 지정돼 있으면 그 WBP 로, 없으면 순수 C++ 위젯을 그대로 만든다(에디터 작업 0).
	const TSubclassOf<UNavGuideLogWidget> Cls =
		(GuideLogWidgetClass != nullptr) ? GuideLogWidgetClass
		                                 : TSubclassOf<UNavGuideLogWidget>(UNavGuideLogWidget::StaticClass());
	GuideLog = CreateWidget<UNavGuideLogWidget>(GetWorld(), Cls);
	if (GuideLog != nullptr)
	{
		GuideLog->BindToMinimap(this);
		GuideLog->AddToViewport(200);   // 전체 지도(100)보다 **위** — 지도를 켜도 로그가 선명하게 보인다.
	}
}

// ---------------------------------------------------------------------- AR 화면(§C, 9단계)

void UNavMinimapWidget::EnsureFloorGuide()
{
	if (Mode != ENavMinimapMode::Follow || IsDesignTime() || FloorGuide != nullptr)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const TSubclassOf<ANavFloorGuideActor> Cls =
		(FloorGuideActorClass != nullptr) ? FloorGuideActorClass
		                                  : TSubclassOf<ANavFloorGuideActor>(ANavFloorGuideActor::StaticClass());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FloorGuide = World->SpawnActor<ANavFloorGuideActor>(Cls, FTransform::Identity, Params);
}

void UNavMinimapWidget::EnsureDestMarker()
{
	if (Mode != ENavMinimapMode::Follow || IsDesignTime() || DestMarker != nullptr)
	{
		return;
	}
	const TSubclassOf<UNavDestMarkerWidget> Cls =
		(DestMarkerWidgetClass != nullptr) ? DestMarkerWidgetClass
		                                   : TSubclassOf<UNavDestMarkerWidget>(UNavDestMarkerWidget::StaticClass());
	DestMarker = CreateWidget<UNavDestMarkerWidget>(GetWorld(), Cls);
	if (DestMarker != nullptr)
	{
		DestMarker->AddToViewport(40);   // AR 위, 안내 로그(50)·전체 지도(100) 아래.
	}
}

bool UNavMinimapWidget::GetNodePos(const FString& NodeId, FVector2D& OutXY) const
{
	if (NodeId.IsEmpty()) { return false; }
	for (const FNavMapNode& N : Graph.Nodes)
	{
		if (N.NodeId == NodeId)
		{
			OutXY = FVector2D(N.PosXCm, N.PosYCm);
			return true;
		}
	}
	return false;
}

void UNavMinimapWidget::PushDestinationToMarker()
{
	if (DestMarker == nullptr)
	{
		return;
	}
	FVector2D XY;
	if (!DestinationNodeId.IsEmpty() && GetNodePos(DestinationNodeId, XY))
	{
		DestMarker->SetDestination(XY, GetNodeType(DestinationNodeId), GetNodeLabel(DestinationNodeId));
	}
	else
	{
		DestMarker->ClearDestination();
	}
}

void UNavMinimapWidget::RefreshArGuides(const FNavProgress& P)
{
	// 바닥 발자국: 정적 경로 위, 사용자 앞 구간에 목적지 종류별 발자국/화살표.
	if (FloorGuide != nullptr)
	{
		if (RouteXY.Num() >= 2 && bHasCurrent)
		{
			FloorGuide->UpdateGuide(RouteXY, CurrentXY,
				GetNodeType(DestinationNodeId), GetNodeLabel(DestinationNodeId));
		}
		else
		{
			FloorGuide->HideGuide();
		}
	}
	// 목적지 마름모: 남은 거리(경로거리)를 매 프레임 갱신(월드 투영은 위젯이 스스로).
	if (DestMarker != nullptr && P.bValid)
	{
		DestMarker->SetRemaining(P.RemainingCm);
	}
}

void UNavMinimapWidget::SetCurrentPose(float PosXCm, float PosYCm, float HeadingDeg, bool bHasHeading)
{
	CurrentXY = FVector2D(PosXCm, PosYCm);
	CurrentHeadingDeg = HeadingDeg;
	bCurrentHasHeading = bHasHeading;
	bHasCurrent = true;

	// 동적 경로선·이탈 판정·자동 reroute·안내 배너.
	UNavRouteProgress* Progress = GetRouteProgress();
	if (Progress->HasRoute())
	{
		const FNavProgress P = Progress->UpdatePose(CurrentXY);

		// 판정 로직은 작은 미니맵(Follow)만 돌린다. 아래에서 열린 전체 지도(Full)로
		// 같은 pose 를 흘려보내므로, Full 인스턴스가 이탈/reroute/배너를 두 번 돌리면
		// 안 된다 — 모드로 가른다.
		if (Mode == ENavMinimapMode::Follow)
		{
			if (P.bOffRoute && !bWasOffRoute)
			{
				UE_LOG(LogNav, Warning,
					TEXT("[minimap] 경로 이탈 감지 (lateral=%.0fcm > %.0fcm)."),
					P.LateralOffsetCm, Progress->OffRouteThresholdCm);
			}
			bWasOffRoute = P.bOffRoute;

			EvaluateAutoReroute(P);                            // 5-B3
			OnGuidanceUpdated.Broadcast(Progress->GetGuidance());  // 5-D
			RefreshArGuides(P);                               // 9단계 §C — 바닥 발자국·목적지 마름모
		}
	}

	// 열린 전체 지도에도 같은 pose 를 흘려보낸다 — 여는 순간의 스냅샷이 아니라
	// 실시간으로 파란 점이 따라 움직인다(5-C1).
	if (UNavMinimapWidget* Full = GetOpenFullMapView())
	{
		Full->SetCurrentPose(PosXCm, PosYCm, HeadingDeg, bHasHeading);
	}

	Invalidate(EInvalidateWidgetReason::Paint);
}

void UNavMinimapWidget::ClearCurrentPose()
{
	bHasCurrent = false;
	OffRouteSinceSeconds = -1.f;
	if (FloorGuide != nullptr) { FloorGuide->HideGuide(); }   // 측위 상실 → 바닥 발자국 숨김(안전장치 ②).
	if (UNavMinimapWidget* Full = GetOpenFullMapView()) { Full->ClearCurrentPose(); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

UNavMinimapWidget* UNavMinimapWidget::GetOpenFullMapView() const
{
	if (FullMapInstance.IsValid() && FullMapInstance->IsInViewport())
	{
		return FullMapInstance->GetMapView();
	}
	return nullptr;
}

void UNavMinimapWidget::EvaluateAutoReroute(const FNavProgress& P)
{
	// 3중 게이트(spec §3.3). 하나라도 막히면 요청하지 않는다.
	UWorld* World = GetWorld();
	if (World == nullptr || DestinationNodeId.IsEmpty())
	{
		return;
	}

	// ① 이탈이 아니면 타이머를 접고 끝. 이탈이면 시작 시각을 기록.
	if (!P.bOffRoute)
	{
		OffRouteSinceSeconds = -1.f;
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (OffRouteSinceSeconds < 0.f)
	{
		OffRouteSinceSeconds = Now;
	}

	UNavRouteProgress* Progress = GetRouteProgress();

	// ① 지속 시간: 한 프레임 튐으로 서버를 때리지 않는다.
	if (Now - OffRouteSinceSeconds < Progress->RerouteOffRouteHoldSeconds)
	{
		return;
	}
	// ② 쿨다운: 이탈이 계속돼도 무한 재요청이 되지 않게.
	if (Now - LastRerouteSeconds < Progress->RerouteCooldownSeconds)
	{
		return;
	}

	// ③ 측위 품질: 흔들려서 생긴 가짜 이탈에 경로를 갈아엎으면 더 나빠진다.
	UNavLocalizer* Localizer = UNavLocalizer::GetNavLocalizer(this);
	if (Localizer != nullptr && Localizer->IsTrackingDegraded())
	{
		return;   // 품질이 회복될 때까지 미룬다.
	}

	// 게이트 통과 — 현재 위치에서 목적지로 다시 길을 찾는다. 새 경로는 OnRouteReceived
	// → (BP) → SetRoute 로 돌아와 폴리라인을 교체한다.
	UNavClient* Client = nullptr;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		Client = GI->GetSubsystem<UNavClient>();
	}
	if (Client == nullptr)
	{
		return;
	}

	// from 은 노드가 아니라 임의 맵 좌표를 그대로 보낸다(navigation.py 가 엣지 투영으로 스냅).
	FNavMapPose From = (Localizer != nullptr) ? Localizer->GetCurrentMapPose() : FNavMapPose();
	if (Localizer == nullptr)
	{
		From.PosXCm = CurrentXY.X;
		From.PosYCm = CurrentXY.Y;
		From.HeadingDeg = CurrentHeadingDeg;
		From.bHasHeading = bCurrentHasHeading;
	}

	LastRerouteSeconds = Now;
	UE_LOG(LogNav, Log,
		TEXT("[minimap] 자동 reroute (이탈 %.1f초 지속, lateral=%.0fcm). to=%s"),
		Now - OffRouteSinceSeconds, P.LateralOffsetCm, *DestinationNodeId);
	Client->Reroute(FString(), From, DestinationNodeId);
}

void UNavMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	GetRouteProgress();   // 미리 만들어 둔다.
	// 경로 띠·셰브론이 위젯 밖으로 삐져나가지 않게 클립한다(5-C3). Follow 8m 창에서
	// 특히 필요하고, Full 은 fit-to-bounds 라 사실상 영향이 없다.
	SetClipping(EWidgetClipping::ClipToBounds);
	RefreshEmptyHint();

	EnsureGuideLog();   // §D 안내 로그를 화면 상단에 띄운다(Follow HUD 만).
	EnsureFloorGuide(); // §C-1 바닥 발자국 액터(Follow 만).
	EnsureDestMarker(); // §C-2 목적지 마름모 HUD(Follow 만).
}

void UNavMinimapWidget::NativeDestruct()
{
	// 우리가 만든 안내 로그도 함께 화면에서 내린다.
	if (GuideLog != nullptr)
	{
		GuideLog->RemoveFromParent();
		GuideLog = nullptr;
	}
	// §C-2 목적지 마름모 HUD 도 내린다.
	if (DestMarker != nullptr)
	{
		DestMarker->RemoveFromParent();
		DestMarker = nullptr;
	}
	// §C-1 바닥 발자국 액터를 파괴한다.
	if (FloorGuide != nullptr)
	{
		FloorGuide->Destroy();
		FloorGuide = nullptr;
	}
	Super::NativeDestruct();
}

// ---------------------------------------------------------------------- 좌표/조회

FString UNavMinimapWidget::GetNodeType(const FString& NodeId) const
{
	if (NodeId.IsEmpty()) { return FString(); }
	for (const FNavMapNode& N : Graph.Nodes)
	{
		if (N.NodeId == NodeId) { return N.NodeType; }
	}
	return FString();
}

FString UNavMinimapWidget::GetNodeLabel(const FString& NodeId) const
{
	if (NodeId.IsEmpty()) { return FString(); }
	for (const FNavMapNode& N : Graph.Nodes)
	{
		if (N.NodeId == NodeId) { return N.Label; }
	}
	return FString();
}

bool UNavMinimapWidget::WasRecentlyPainted(double WithinSeconds) const
{
	if (LastPaintSeconds <= 0.0)
	{
		return false;   // 아직 한 번도 안 그려짐(표출 전).
	}
	return (FApp::GetCurrentTime() - LastPaintSeconds) <= WithinSeconds;
}

bool UNavMinimapWidget::FindDestinationNodeAtLocal(const FVector2D& LocalPos, float RadiusPx,
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
		if (!IconNodeIds.Contains(Node.NodeId))
		{
			continue;   // 지도에 아이콘으로 그린 목적지만 고를 수 있다(중복 entrance·junction 제외, D-5).
		}
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

FVector2D UNavMinimapWidget::WorldToLocal(const FVector2D& W) const
{
	return FVector2D(
		CachedScreenOrigin.X + (W.X - CachedWorldOrigin.X) * CachedScale,
		CachedScreenOrigin.Y - (W.Y - CachedWorldOrigin.Y) * CachedScale);
}

bool UNavMinimapWidget::IsLocalInView(const FVector2D& P, float Margin) const
{
	return P.X >= -Margin && P.X <= CachedLocalSize.X + Margin
		&& P.Y >= -Margin && P.Y <= CachedLocalSize.Y + Margin;
}

bool UNavMinimapWidget::IsSegmentInView(const FVector2D& A, const FVector2D& B, float Margin) const
{
	// 값싼 보수적 판정: 선분의 로컬 AABB 가 위젯 사각형과 겹치나. 겹치면 그린다
	// (일부 대각선 오검출이 있어도 클리핑이 최종적으로 잘라 준다).
	const float MinX = FMath::Min(A.X, B.X) - Margin;
	const float MaxX = FMath::Max(A.X, B.X) + Margin;
	const float MinY = FMath::Min(A.Y, B.Y) - Margin;
	const float MaxY = FMath::Max(A.Y, B.Y) + Margin;
	return MaxX >= 0.f && MinX <= CachedLocalSize.X
		&& MaxY >= 0.f && MinY <= CachedLocalSize.Y;
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

	// "표출 중" 표식 — 숨겨지면(부모 접힘 포함) 이 호출 자체가 멈춘다. 안내 로그가 이 신선도로
	// 미니맵이 화면에 떠 있는지(네비 활성)를 판정한다. 디자인 미리보기는 제외.
	if (!IsDesignTime())
	{
		LastPaintSeconds = FApp::GetCurrentTime();
	}

	bHasCachedTransform = false;   // 이번 프레임 변환을 새로 잡는다.

	const bool bFull = (Mode == ENavMinimapMode::Full);
	const bool bDrawCurrent = bHasCurrent && !IsDesignTime();

	// 그릴 경로 폴리라인(맵 cm): 측위 중이면 동적 선, 아니면 정적/미리보기.
	TArray<FVector2D> DynamicScratch, PreviewScratch;
	const TArray<FVector2D>& WorldPts = ResolveRoutePts(DynamicScratch, PreviewScratch);
	const bool bHaveRoute = WorldPts.Num() > 0;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	CachedLocalSize = Size;   // Follow 뷰 컬링·셰브론 창 판정에 쓴다.
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
	if (bFull && Graph.Outline.Num() >= 3)
	{
		// Fill the real graph polygon in narrow horizontal strips: no invented map coordinates.
		TArray<FVector2D> Poly;
		float Top = Size.Y, Bottom = 0;
		for (const FVector2D& P : Graph.Outline)
		{
			const FVector2D V = WorldToLocal(P); Poly.Add(V);
			Top = FMath::Min(Top, float(V.Y)); Bottom = FMath::Max(Bottom, float(V.Y));
		}
		++Layer;
		for (float Y = Top + 1; Y < Bottom; Y += 2)
		{
			TArray<float> Crossings;
			for (int32 I = 0; I < Poly.Num(); ++I)
			{
				const FVector2D A = Poly[I], B = Poly[(I + 1) % Poly.Num()];
				if ((A.Y <= Y && B.Y > Y) || (B.Y <= Y && A.Y > Y))
					Crossings.Add(A.X + (Y - A.Y) * (B.X - A.X) / (B.Y - A.Y));
			}
			Crossings.Sort();
			const bool bGrout = FMath::Fmod(Y - Top, 48.f) < 2.f;
			const FLinearColor Floor = FLinearColor::FromSRGBColor(bGrout ? FColor(97, 91, 79) : FColor(64, 60, 52));
			for (int32 I = 0; I + 1 < Crossings.Num(); I += 2)
			{
				TArray<FVector2D> Span = { FVector2D(Crossings[I], Y), FVector2D(Crossings[I + 1], Y) };
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geom, Span, ESlateDrawEffect::None, Floor, false, 2.f);
			}
		}
	}

	// -------- 벽·구조물·전체 엣지·전체 노드를 먼저 깐다 --------
	// 5-C2: Follow 에도 도면을 깐다("여기가 어디인지" 알 수 있게). 8m 창 밖 요소는
	// PaintFullMapBase 안에서 뷰 컬링으로 솎아내고, 남은 것은 클리핑이 잘라 준다.
	if (Graph.Nodes.Num() > 0)
	{
		PaintFullMapBase(OutDrawElements, Layer, Geom);
		if (bFull)
		{
			if (const FSlateBrush* Stairs = ResolveIconBrush(SkinPath(TEXT("prop_stairs"))))
			{
				for (const FNavObstacle& O : Graph.Obstacles)
				{
					const FVector2D A = WorldToLocal(FVector2D(O.X0, O.Y1));
					const FVector2D B = WorldToLocal(FVector2D(O.X1, O.Y0));
					FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
						AllottedGeometry.ToPaintGeometry(B - A, FSlateLayoutTransform(A)), Stairs);
				}
			}
		}
		// 목적지 아이콘(마름모+그림)은 도면 위에 얹는다. Full·Follow 공통(§B-1·D-8).
		PaintDestinationIcons(OutDrawElements, Layer, AllottedGeometry);
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

			// 5-C3: 클리핑만으론 경계에 반쯤 걸린 화살표가 잘려 보인다. 중심이 창 안에
			// 완전히(길이 여유만큼) 들어오는 것만 그린다. Follow 8m 창에서만 의미가 있다.
			if (Mode == ENavMinimapMode::Follow && !IsLocalInView(P, -ArrowLengthPx))
			{
				NextAtCm += SpacingCm;
				continue;
			}

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
	// Follow(8m 창)에서는 대부분이 화면 밖이다. 벽·엣지·노드를 좌표만 투영해 두고
	// 위젯 사각형과 겹치는 것만 그린다(뷰 컬링). Full 은 fit-to-bounds 라 전부 겹치므로
	// 컬링을 꺼도 결과가 같다 — 불필요한 판정을 아끼려 Follow 에서만 켠다.
	const bool bCull = (Mode == ENavMinimapMode::Follow);
	const float CullMargin = FMath::Max(WallThicknessPx, GraphNodeRadiusPx) + ArrowLengthPx;

	// 1) 내부 구조물(채운 사각형) — 맨 아래.
	++Layer;
	for (const FNavObstacle& O : Graph.Obstacles)
	{
		const FVector2D A = WorldToLocal(FVector2D(O.X0, O.Y0));
		const FVector2D B = WorldToLocal(FVector2D(O.X1, O.Y1));
		if (bCull && !IsSegmentInView(A, B, CullMargin)) { continue; }
		PaintFilledRect(Out, Layer, Geom, A, B, ObstacleColor);
	}

	// 2) 벽(외곽선). Full 은 폐곡선 한 번에 긋고, Follow 는 컬링을 위해 변마다 나눠 긋는다.
	if (Graph.Outline.Num() >= 2)
	{
		++Layer;
		if (bCull)
		{
			const int32 N = Graph.Outline.Num();
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D A = WorldToLocal(Graph.Outline[i]);
				const FVector2D B = WorldToLocal(Graph.Outline[(i + 1) % N]);   // 마지막→처음(닫음)
				if (!IsSegmentInView(A, B, CullMargin)) { continue; }
				TArray<FVector2D> Seg = { A, B };
				FSlateDrawElement::MakeLines(Out, Layer, Geom, Seg,
					ESlateDrawEffect::None, WallColor, true, WallThicknessPx);
			}
		}
		else
		{
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
	}

	// 3)·4) 엣지·노드는 **디버그일 때만** 그린다(final §D-7). 사용자 화면에는 도면·구조물·
	// 목적지 아이콘·경로선만 남는다. 아이콘은 PaintDestinationIcons 가 따로 얹는다.
	if (!bDrawGraphDebug)
	{
		return;
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
			const FVector2D LA = WorldToLocal(*A);
			const FVector2D LB = WorldToLocal(*B);
			if (bCull && !IsSegmentInView(LA, LB, CullMargin)) { continue; }
			TArray<FVector2D> Seg = { LA, LB };
			FSlateDrawElement::MakeLines(Out, Layer, Geom, Seg,
				ESlateDrawEffect::None, GraphEdgeColor, true, GraphEdgeThicknessPx);
		}
	}

	// 4) 전체 노드 — 링. 목적지로 지정된 노드는 채워서 구분.
	++Layer;
	for (const FNavMapNode& N : Graph.Nodes)
	{
		const FVector2D C = WorldToLocal(FVector2D(N.PosXCm, N.PosYCm));
		if (bCull && !IsLocalInView(C, CullMargin)) { continue; }
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

// ---------------------------------------------------------------------- 목적지 아이콘

void UNavMinimapWidget::RebuildIconBrushes()
{
	IconBrushCache.Reset();
	LoadedIconTextures.Reset();
	if (Mode == ENavMinimapMode::Full)
	{
		const TCHAR* Names[] = { TEXT("prop_stairs"), TEXT("dot_orange"), TEXT("dot_blue"), TEXT("dot_green"),
			TEXT("poi_triceratops"), TEXT("poi_brachiosaurus"), TEXT("poi_trex"), TEXT("poi_toilet"), TEXT("poi_entrance") };
		for (const TCHAR* Name : Names)
		{
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *SkinPath(Name)))
			{
				LoadedIconTextures.Add(Texture);
				TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
				Brush->SetResourceObject(Texture); Brush->DrawAs = ESlateBrushDrawType::Image;
				IconBrushCache.Add(SkinPath(Name), Brush);
			}
		}
	}

	// 하단 버튼과 동일한 목적지 집합(중복 entrance 제거 등)만 지도에 그린다.
	IconNodeIds.Reset();
	TArray<int32> Order;
	FNavDestinations::BuildDestinationOrder(Graph.Nodes, Order);
	for (int32 Idx : Order)
	{
		IconNodeIds.Add(Graph.Nodes[Idx].NodeId);
	}

	for (const FNavMapNode& N : Graph.Nodes)
	{
		if (!IconNodeIds.Contains(N.NodeId))
		{
			continue;
		}
		const FString Path = FNavDestinations::IconObjectPath(N.NodeType, N.Label);
		if (Path.IsEmpty() || IconBrushCache.Contains(Path))
		{
			continue;
		}
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
		if (Tex == nullptr)
		{
			UE_LOG(LogNav, Warning,
				TEXT("[minimap] 목적지 아이콘 로드 실패: %s (텍스처 임포트/쿡 누락?). 마름모만 그린다."), *Path);
			continue;
		}
		LoadedIconTextures.Add(Tex);   // GC 방지.
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Tex);
		Brush->DrawAs = ESlateBrushDrawType::Image;
		Brush->ImageSize = FVector2D(DestIconSizePx, DestIconSizePx);
		IconBrushCache.Add(Path, Brush);
	}
}

const FSlateBrush* UNavMinimapWidget::ResolveIconBrush(const FString& ObjectPath) const
{
	if (ObjectPath.IsEmpty())
	{
		return nullptr;
	}
	const TSharedPtr<FSlateBrush>* Found = IconBrushCache.Find(ObjectPath);
	return (Found != nullptr) ? Found->Get() : nullptr;
}

void UNavMinimapWidget::PaintDestinationIcons(FSlateWindowElementList& Out, int32& Layer,
	const FGeometry& AllottedGeometry) const
{
	if (!bHasCachedTransform || Graph.Nodes.Num() == 0)
	{
		return;
	}
	const bool bCull = (Mode == ENavMinimapMode::Follow);
	const float CullMargin = DestDiamondHalfPx + DestIconSizePx;
	const FPaintGeometry LineGeom = AllottedGeometry.ToPaintGeometry();

	++Layer;
	for (const FNavMapNode& N : Graph.Nodes)
	{
		if (!IconNodeIds.Contains(N.NodeId))
		{
			continue;   // 하단 버튼과 같은 6개만(중복 entrance 등 제외).
		}
		const FVector2D C = WorldToLocal(FVector2D(N.PosXCm, N.PosYCm));
		if (bCull && !IsLocalInView(C, CullMargin))
		{
			continue;
		}

		const bool bActive = !DestinationNodeId.IsEmpty() && N.NodeId == DestinationNodeId;
		const FLinearColor Accent = FNavDestinations::AccentColor(N.NodeType);
		if (Mode == ENavMinimapMode::Full)
		{
			const ENavDestKind Kind = FNavDestinations::Classify(N.NodeType);
			const TCHAR* Dot = Kind == ENavDestKind::Facility ? TEXT("dot_blue") :
				Kind == ENavDestKind::Entrance ? TEXT("dot_green") : TEXT("dot_orange");
			bool bDotClear = true;
			for (const FNavMapNode& Other : Graph.Nodes)
			{
				if (Other.NodeId != N.NodeId && IconNodeIds.Contains(Other.NodeId) &&
					FVector2D::Distance(C + FVector2D(0, 95), WorldToLocal(FVector2D(Other.PosXCm, Other.PosYCm))) < 68)
				{ bDotClear = false; break; }
			}
			if (const FSlateBrush* Glow = bDotClear ? ResolveIconBrush(SkinPath(Dot)) : nullptr)
			{
				FSlateDrawElement::MakeBox(Out, ++Layer, AllottedGeometry.ToPaintGeometry(
					FVector2D(44, 44), FSlateLayoutTransform(C + FVector2D(-22, 73))), Glow);
			}
			FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);
			const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(N.Label, LabelFont);
			const FVector2D LabelSize = TextSize + FVector2D(16, 8);
			const FVector2D LabelPos(FMath::Clamp(C.X - LabelSize.X * 0.5, 4.0,
				FMath::Max(4.0, CachedLocalSize.X - LabelSize.X - 4)), C.Y + 46);
			FSlateDrawElement::MakeBox(Out, ++Layer, AllottedGeometry.ToPaintGeometry(
				LabelSize, FSlateLayoutTransform(LabelPos)), FCoreStyle::Get().GetBrush("WhiteBrush"),
				ESlateDrawEffect::None, FLinearColor(0.008f, 0.009f, 0.01f, 0.9f));
			FSlateDrawElement::MakeText(Out, ++Layer, AllottedGeometry.ToPaintGeometry(
				LabelSize, FSlateLayoutTransform(LabelPos + FVector2D(8, 3))), N.Label, LabelFont,
				ESlateDrawEffect::None, FLinearColor::White);
			const TCHAR* Poi = PoiSkin(N);
			if (const FSlateBrush* Sprite = Poi ? ResolveIconBrush(SkinPath(Poi)) : nullptr)
			{
				const float Extent = bActive ? 108.f : 98.f;
				FSlateDrawElement::MakeBox(Out, ++Layer, AllottedGeometry.ToPaintGeometry(
					FVector2D(Extent), FSlateLayoutTransform(C - FVector2D(Extent * 0.5f))), Sprite);
				continue;
			}
		}

		// 마름모(중심→꼭짓점 = Half). 화면 px 고정 크기라 Follow 배율에도 안 커진다.
		const float Half = DestDiamondHalfPx;
		TArray<FVector2D> Diamond;
		Diamond.Add(C + FVector2D(0.f, -Half));
		Diamond.Add(C + FVector2D(Half, 0.f));
		Diamond.Add(C + FVector2D(0.f, Half));
		Diamond.Add(C + FVector2D(-Half, 0.f));
		Diamond.Add(C + FVector2D(0.f, -Half));   // 닫는다(첫 점 = Diamond[0]. 자기참조 Add(Diamond[0])는 재할당 시 크래시).
		FSlateDrawElement::MakeLines(Out, Layer, LineGeom, Diamond,
			ESlateDrawEffect::None, Accent, true,
			bActive ? DestActiveDiamondThicknessPx : DestDiamondThicknessPx);

		// 아이콘 그림. 없으면(임포트 누락) 마름모만 남는다 — 자리 표시는 유지된다.
		const FString IconPath = FNavDestinations::IconObjectPath(N.NodeType, N.Label);
		if (const FSlateBrush* Brush = ResolveIconBrush(IconPath))
		{
			const FVector2D IconSz(DestIconSizePx, DestIconSizePx);
			const FPaintGeometry IconGeom = AllottedGeometry.ToPaintGeometry(
				IconSz, FSlateLayoutTransform(C - IconSz * 0.5f));
			// 비활성 목적지는 살짝 흐리게, 현재 목적지는 또렷하게.
			const FLinearColor Tint = bActive ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.85f);
			FSlateDrawElement::MakeBox(Out, Layer, IconGeom, Brush, ESlateDrawEffect::None, Tint);
		}
	}
}
