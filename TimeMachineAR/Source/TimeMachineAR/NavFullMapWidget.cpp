#include "NavFullMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/Overlay.h"
#include "Components/CanvasPanel.h"
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
	Grid->SetSlotPadding(FMargin(12.f));

	// 지역변수 이름을 Slot 으로 두면 UWidget::Slot 멤버를 가려 MSVC 에서 C4458(-Werror)로
	// 컴파일이 막힌다(clang 은 -Wno-error=shadow 라 통과 — 윈도우 팀원만 깨진다). SlotIdx 로 둔다.
	for (int32 SlotIdx = 0; SlotIdx < Order.Num(); ++SlotIdx)
	{
		const FNavMapNode& N = InGraph.Nodes[Order[SlotIdx]];
		const FLinearColor Accent = FNavDestinations::AccentColor(N.NodeType);

		UNavDestButton* Button = WidgetTree->ConstructWidget<UNavDestButton>(UNavDestButton::StaticClass());
		if (Button == nullptr) { continue; }
		Button->NodeId = N.NodeId;

		// 테두리 두껍게 + node_type 색(final §B-2). 흰 바탕 + 굵은 색 외곽선(둥근 모서리).
		FButtonStyle Style = Button->GetStyle();
		const FSlateRoundedBoxBrush Normal(FLinearColor::White, 14.f, Accent, 7.f);
		const FSlateRoundedBoxBrush Hover(FLinearColor(0.94f, 0.94f, 0.96f), 14.f, Accent, 9.f);
		Style.SetNormal(Normal);
		Style.SetHovered(Hover);
		Style.SetPressed(Hover);
		// 버튼 자체 여백을 키워 클릭 영역·시각 크기를 2배 수준으로.
		Style.SetNormalPadding(FMargin(14.f, 12.f));
		Style.SetPressedPadding(FMargin(14.f, 12.f));
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
			Icon->SetDesiredSizeOverride(FVector2D(68.f, 68.f));   // 2배.
			if (UHorizontalBoxSlot* IconSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Icon)))
			{
				IconSlot->SetVerticalAlignment(VAlign_Center);
				IconSlot->SetPadding(FMargin(8.f, 8.f, 14.f, 8.f));
			}
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(N.Label));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.10f, 0.10f, 0.11f)));
		Label->SetJustification(ETextJustify::Center);
		Label->SetAutoWrapText(true);   // "티라노사우루스 렉스"처럼 긴 이름이 버튼 밖으로 안 나가게.
		{
			FSlateFontInfo LabelFont = Label->GetFont();
			LabelFont.Size = 26;   // 키우되, 긴 이름이 삐져나가지 않을 만큼만.
			Label->SetFont(LabelFont);
		}
		if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetHorizontalAlignment(HAlign_Fill);
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));   // 남는 폭을 글자가 차지 → 줄바꿈 여유.
			TextSlot->SetPadding(FMargin(0.f, 8.f, 16.f, 8.f));
		}

		Button->SetContent(Row);

		if (UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Button, SlotIdx / 2, SlotIdx % 2))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);   // 셀 폭을 꽉 채워 버튼을 넓게.
			GridSlot->SetVerticalAlignment(VAlign_Center);   // 높이는 내용대로(과하게 커지지 않게).
		}
		DestButtons.Add(Button);
	}

	// 그리드를 화면에 붙인다. 전용 호스트가 있으면 거기, 없으면 루트 패널 하단 중앙.
	if (DestButtonHost != nullptr)
	{
		DestButtonHost->ClearChildren();
		DestButtonHost->AddChild(Grid);
		UE_LOG(LogNav, Log, TEXT("[fullmap] 목적지 버튼 %d개 → DestButtonHost(%s)."),
			DestButtons.Num(), *DestButtonHost->GetClass()->GetName());
		return;
	}

	UWidget* RootW = (WidgetTree != nullptr) ? WidgetTree->RootWidget : nullptr;
	UPanelWidget* Root = Cast<UPanelWidget>(RootW);

	// 다자식 패널(Canvas/Overlay 등)이면 바로 붙는다. 단일자식(Border 등)이면 AddChild 가
	// null 을 돌려주므로 아래에서 기존 자식과 함께 Overlay 로 묶어 재부모한다.
	UPanelSlot* Added = (Root != nullptr) ? Root->AddChild(Grid) : nullptr;

	if (Added == nullptr)
	{
		// 루트가 단일자식 컨테이너(WBP_NavMinimapFull 은 Border 가 루트). 기존 내용(MapView)과
		// 버튼 그리드를 Overlay 로 묶어 Border 의 유일 자식으로 되꽂는다.
		UContentWidget* Content = Cast<UContentWidget>(RootW);
		if (Content == nullptr)
		{
			UE_LOG(LogNav, Warning,
				TEXT("[fullmap] 루트(%s)에 버튼을 붙일 수 없습니다. WBP 에 DestButtonHost(패널)를 두세요."),
				RootW ? *RootW->GetClass()->GetName() : TEXT("null"));
			return;
		}
		UWidget* Existing = Content->GetContent();
		// Overlay 로는 세로 위치를 하단/중앙 같은 정렬로만 줄 수 있어 "지도 아래~바닥 중간"을
		// 못 맞춘다. CanvasPanel 로 묶어 세로 앵커를 분수(0.72)로 지정한다.
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		Content->SetContent(Canvas);
		if (Existing != nullptr)
		{
			if (UCanvasPanelSlot* ES = Cast<UCanvasPanelSlot>(Canvas->AddChild(Existing)))
			{
				ES->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));   // 지도는 화면 가득.
				ES->SetOffsets(FMargin(0.f));
			}
		}
		if (UCanvasPanelSlot* GS = Cast<UCanvasPanelSlot>(Canvas->AddChild(Grid)))
		{
			// 가로: 좌우 4~96% 로 넓게 스트레치(버튼이 화면 폭을 크게 차지 → 긴 이름도 여유).
			// 세로: 0.80 지점 — 예전 하단 배치와 0.72 의 중간쯤(사용자 요청).
			// 스트레치+포인트 혼합 앵커라 Offsets 는 (좌인셋, 상단Y, 우인셋, 높이) 로 읽힌다.
			GS->SetAnchors(FAnchors(0.04f, 0.80f, 0.96f, 0.80f));
			GS->SetAlignment(FVector2D(0.f, 0.5f));   // 0.80 선에 세로 중심을 맞춘다.
			GS->SetAutoSize(false);
			GS->SetOffsets(FMargin(0.f, 0.f, 0.f, 380.f));   // 높이(3줄까지 여유).
		}
		UE_LOG(LogNav, Log,
			TEXT("[fullmap] 목적지 버튼 %d개 → 단일자식 루트(%s)를 CanvasPanel 로 재부모해 하단 중간 부착."),
			DestButtons.Num(), *RootW->GetClass()->GetName());
		return;
	}

	// 다자식 패널에 직접 붙은 경우: 세로 0.80(예전 하단과 0.72 의 중간)에 가로로 넓게 배치.
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Added))
	{
		CanvasSlot->SetAnchors(FAnchors(0.04f, 0.80f, 0.96f, 0.80f));
		CanvasSlot->SetAlignment(FVector2D(0.f, 0.5f));
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 380.f));
	}
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Added))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Center);
		OverlaySlot->SetPadding(FMargin(40.f, 0.f, 40.f, 0.f));
	}
	UE_LOG(LogNav, Log, TEXT("[fullmap] 목적지 버튼 %d개 → %s 에 직접 부착."),
		DestButtons.Num(), *Root->GetClass()->GetName());
}
