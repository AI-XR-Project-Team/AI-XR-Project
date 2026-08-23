#include "NavFullMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "NavDestButton.h"
#include "NavDestinations.h"
#include "NavClient.h"   // LogNav

void UNavFullMapWidget::ApplyState(const FNavGraph& InGraph, const TArray<FVector2D>& InRouteXY,
	bool bHasPose, const FVector2D& PoseXY, float PoseHeadingDeg, bool bPoseHasHeading,
	const FString& InDestNodeId)
{
	if (MapView == nullptr)
	{
		return;
	}
	MapView->Mode = ENavMinimapMode::Full;   // WBP 설정이 빠져도 강제한다.
	MapView->SetGraph(InGraph);
	MapView->SetRouteXY(InRouteXY);
	MapView->SetDestinationNode(InDestNodeId);
	if (bHasPose)
	{
		MapView->SetCurrentPose(PoseXY.X, PoseXY.Y, PoseHeadingDeg, bPoseHasHeading);
	}
	else
	{
		MapView->ClearCurrentPose();
	}

	BuildDestinationButtons(InGraph);   // 하단 목적지 리스트 버튼(§B-2).
}

void UNavFullMapWidget::Close()
{
	OnClosed.Broadcast();
	RemoveFromParent();
}

FReply UNavFullMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	const FVector2D Abs = InMouseEvent.GetScreenSpacePosition();

	// **목적지 아이콘**을 눌렀으면 그 노드로 경로를 잡고, 그 밖의 아무 곳이나 눌러도 닫는다.
	// 노드 터치 목적지 선택은 폐지됐다(D-5) — 아이콘(목적지 노드)만 판정한다(§B-1).
	// 하단 버튼은 UButton 이 클릭을 먹어 여기까지 오지 않는다.
	if (MapView != nullptr)
	{
		const FVector2D Local = MapView->GetCachedGeometry().AbsoluteToLocal(Abs);
		FString NodeId;
		if (MapView->FindDestinationNodeAtLocal(Local, MapView->NodeHitRadiusPx, NodeId))
		{
			MapView->SetDestinationNode(NodeId);
			OnDestinationChosen.Broadcast(NodeId);
		}
	}

	Close();
	return FReply::Handled();
}

void UNavFullMapWidget::HandleDestButtonClicked(const FString& NodeId)
{
	if (MapView != nullptr)
	{
		MapView->SetDestinationNode(NodeId);
	}
	OnDestinationChosen.Broadcast(NodeId);
	Close();   // 목적지를 골랐으니 전체 지도를 닫는다(아이콘 탭과 동작 일치).
}

void UNavFullMapWidget::BuildDestinationButtons(const FNavGraph& InGraph)
{
	// 이전 버튼 정리.
	for (UNavDestButton* B : DestButtons)
	{
		if (B != nullptr) { B->RemoveFromParent(); }
	}
	DestButtons.Reset();

	if (WidgetTree == nullptr)
	{
		return;
	}

	TArray<int32> Order;
	FNavDestinations::BuildDestinationOrder(InGraph.Nodes, Order);
	if (Order.Num() == 0)
	{
		return;
	}

	// 2열 그리드. 위 4칸 전시물, 아래 2칸 화장실·입구(BuildDestinationOrder 가 순서를 정한다).
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
	if (Grid == nullptr)
	{
		return;
	}
	Grid->SetSlotPadding(FMargin(6.f));

	for (int32 SlotIndex = 0; SlotIndex < Order.Num(); ++SlotIndex)
	{
		const FNavMapNode& N = InGraph.Nodes[Order[SlotIndex]];
		const FLinearColor Accent = FNavDestinations::AccentColor(N.NodeType);

		UNavDestButton* Button = WidgetTree->ConstructWidget<UNavDestButton>(UNavDestButton::StaticClass());
		if (Button == nullptr) { continue; }
		Button->NodeId = N.NodeId;

		// 테두리 두껍게 + node_type 색(final §B-2). 흰 바탕 + 굵은 색 외곽선(둥근 모서리).
		FButtonStyle Style = Button->GetStyle();
		const FSlateRoundedBoxBrush Normal(FLinearColor::White, 8.f, Accent, 4.f);
		const FSlateRoundedBoxBrush Hover(FLinearColor(0.94f, 0.94f, 0.96f), 8.f, Accent, 5.f);
		Style.SetNormal(Normal);
		Style.SetHovered(Hover);
		Style.SetPressed(Hover);
		Button->SetStyle(Style);
		Button->WireClick();
		Button->OnDestClicked.AddDynamic(this, &UNavFullMapWidget::HandleDestButtonClicked);

		// 내용: 아이콘(글씨 왼쪽) + label(번호 없음).
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		const FString IconPath = FNavDestinations::IconObjectPath(N.NodeType, N.Label);
		if (UTexture2D* Tex = IconPath.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr, *IconPath))
		{
			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Icon->SetBrushFromTexture(Tex);
			Icon->SetDesiredSizeOverride(FVector2D(34.f, 34.f));
			if (UHorizontalBoxSlot* IconSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Icon)))
			{
				IconSlot->SetVerticalAlignment(VAlign_Center);
				IconSlot->SetPadding(FMargin(4.f, 4.f, 8.f, 4.f));
			}
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(N.Label));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.10f, 0.10f, 0.11f)));
		if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(0.f, 4.f, 8.f, 4.f));
		}

		Button->SetContent(Row);

		if (UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Button, SlotIndex / 2, SlotIndex % 2))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
		DestButtons.Add(Button);
	}

	// 그리드를 화면에 붙인다. 전용 호스트가 있으면 거기, 없으면 루트 패널 하단 중앙.
	if (DestButtonHost != nullptr)
	{
		DestButtonHost->ClearChildren();
		DestButtonHost->AddChild(Grid);
		return;
	}

	UPanelWidget* Root = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		UE_LOG(LogNav, Warning,
			TEXT("[fullmap] 루트가 패널이 아니라 목적지 버튼을 붙일 곳이 없습니다. "
			     "WBP_NavMinimapFull 에 DestButtonHost 를 두거나 루트를 CanvasPanel 로 두세요."));
		return;
	}
	UPanelSlot* Added = Root->AddChild(Grid);
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Added))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));   // 하단 중앙.
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D(0.f, -28.f));
	}
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Added))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Center);
		OverlaySlot->SetVerticalAlignment(VAlign_Bottom);
		OverlaySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 28.f));
	}
}
