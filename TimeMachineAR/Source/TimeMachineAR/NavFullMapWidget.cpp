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
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "NavDestButton.h"
#include "NavDestinations.h"
#include "NavClient.h"   // LogNav

void UNavFullMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildSkinLayout();
}

void UNavFullMapWidget::BuildSkinLayout()
{
	if (!WidgetTree || !Backdrop || !MapView || SkinButtonHost) { return; }
	// Reparent the bound map instance, preserving its state and delegates.
	MapView->RemoveFromParent();
	Backdrop->SetPadding(FMargin(0));
	Backdrop->SetBrushColor(FLinearColor::FromSRGBColor(FColor(19, 22, 26, 245)));
	UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	Backdrop->SetContent(Scale);
	USizeBox* Design = WidgetTree->ConstructWidget<USizeBox>();
	Design->SetWidthOverride(940); Design->SetHeightOverride(1672);
	Scale->SetContent(Design);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	Design->SetContent(Canvas);
	auto Place = [Canvas](UWidget* W, float X, float Y, float Width, float Height)
	{
		UCanvasPanelSlot* S = Canvas->AddChildToCanvas(W);
		S->SetPosition(FVector2D(X, Y)); S->SetSize(FVector2D(Width, Height));
	};
	auto Picture = [&](const TCHAR* Name, float X, float Y, float Width, float Height)
	{
		const FString Path = FString::Printf(TEXT("/Game/UI/Nav/Skin/%s.%s"), Name, Name);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path))
		{
			UImage* Pic = WidgetTree->ConstructWidget<UImage>();
			Pic->SetBrushFromTexture(Texture);
			Pic->SetVisibility(ESlateVisibility::HitTestInvisible);
			Place(Pic, X, Y, Width, Height);
		}
	};
	Picture(TEXT("lexi_pointing"), 52, 58, 220, 222);
	Picture(TEXT("bubble_speech"), 246, 84, 610, 184);
	UTextBlock* Hint = WidgetTree->ConstructWidget<UTextBlock>();
	Hint->SetText(FText::FromString(TEXT("지도의 아이콘을 눌러\n목적지 안내를 시작해보세요!")));
	FSlateFontInfo Font = Hint->GetFont(); Font.Size = 22; Hint->SetFont(Font);
	Hint->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(FColor(156, 216, 255))));
	Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Hint, 310, 137, 500, 98);
	MapView->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (MapView->WidgetTree)
	{
		TArray<UWidget*> MapChildren;
		MapView->WidgetTree->GetAllWidgets(MapChildren);
		for (UWidget* Child : MapChildren)
			if (UBorder* Background = Cast<UBorder>(Child)) { Background->SetBrushColor(FLinearColor::Transparent); }
	}
	MapView->SetClipping(EWidgetClipping::ClipToBounds);
	MapView->WallColor = FLinearColor::FromSRGBColor(FColor(153, 162, 165));
	MapView->WallThicknessPx = 6.f;
	Place(MapView, 30, 294, 880, 990);
	SkinButtonHost = WidgetTree->ConstructWidget<UOverlay>();
	Place(SkinButtonHost, 36, 1306, 868, 300);
}

void UNavFullMapWidget::ApplyState(const FNavGraph& InGraph, const TArray<FVector2D>& InRouteXY,
	bool bHasPose, const FVector2D& PoseXY, float PoseHeadingDeg, bool bPoseHasHeading,
	const FString& InDestNodeId)
{
	if (MapView == nullptr)
	{
		return;
	}
	MapView->Mode = ENavMinimapMode::Full;   // WBP 설정이 빠져도 강제한다.
	BuildSkinLayout();
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
	Grid->SetSlotPadding(FMargin(10.f, 8.f));

	// 지역변수 이름을 Slot 으로 두면 UWidget::Slot 멤버를 가려 MSVC 에서 C4458(-Werror)로
	// 컴파일이 막힌다(clang 은 -Wno-error=shadow 라 통과 — 윈도우 팀원만 깨진다). SlotIdx 로 둔다.
	for (int32 SlotIdx = 0; SlotIdx < Order.Num(); ++SlotIdx)
	{
		const FNavMapNode& N = InGraph.Nodes[Order[SlotIdx]];
		const FLinearColor Accent = FNavDestinations::AccentColor(N.NodeType);

		UNavDestButton* Button = WidgetTree->ConstructWidget<UNavDestButton>(UNavDestButton::StaticClass());
		if (Button == nullptr) { continue; }
		Button->NodeId = N.NodeId;

		// 어두운 바탕 + node_type 색 외곽선. 지도가 어두운 화면 위에 뜨는데 흰 바탕은
		// 너무 튄다. 목업도 어두운 캡슐에 색 테두리만 얇게 두른 모양이다.
		// 반투명이라 아래 도면이 살짝 비친다.
		FButtonStyle Style = Button->GetStyle();
		// FLinearColor 리터럴은 선형 값이라 화면에서 감마 보정되며 밝아진다.
		// 0.055 를 넣으면 중간 회색으로 뜬다. 앱 배경(#12182B)과 맞추려면
		// sRGB 값을 변환해서 줘야 한다.
		const FSlateRoundedBoxBrush Normal(
			FLinearColor::FromSRGBColor(FColor(18, 24, 43, 235)), 16.f, Accent, 4.f);
		const FSlateRoundedBoxBrush Hover(
			FLinearColor::FromSRGBColor(FColor(34, 44, 72, 245)), 16.f, Accent, 6.f);
		Style.SetNormal(Normal);
		Style.SetHovered(Hover);
		Style.SetPressed(Hover);
		// 버튼 자체 여백을 키워 클릭 영역·시각 크기를 2배 수준으로.
		if (FNavDestinations::Classify(N.NodeType) == ENavDestKind::Exhibit)
		{
			auto SkinBrush = [](const TCHAR* Name, const FSlateBrush& Fallback)
			{
				FSlateBrush Brush = Fallback;
				const FString Path = FString::Printf(TEXT("/Game/UI/Nav/Skin/%s.%s"), Name, Name);
				if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path))
				{
					Brush.SetResourceObject(Texture); Brush.DrawAs = ESlateBrushDrawType::Box;
					Brush.SetUVRegion(FBox2f(FVector2f(0.05f, 0.2f), FVector2f(0.95f, 0.82f)));
					Brush.Margin = FMargin(0.18f, 0.25f); Brush.TintColor = FSlateColor(FLinearColor::White);
				}
				return Brush;
			};
			Style.SetNormal(SkinBrush(TEXT("btn_state_normal"), Normal));
			Style.SetHovered(SkinBrush(TEXT("btn_state_selected"), Hover));
			Style.SetPressed(Style.Hovered);
			Style.SetDisabled(SkinBrush(TEXT("btn_state_disabled"), Normal));
		}
		Style.SetNormalPadding(FMargin(12.f, 6.f));
		Style.SetPressedPadding(FMargin(12.f, 7.f, 12.f, 5.f));
		Button->SetStyle(Style);
		Button->WireClick();
		Button->OnDestClicked.AddDynamic(this, &UNavFullMapWidget::HandleDestButtonClicked);

		// 내용: 아이콘(글씨 왼쪽) + label(번호 없음).
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		FString IconPath = FNavDestinations::IconObjectPath(N.NodeType, N.Label);
		const ENavDestKind Kind = FNavDestinations::Classify(N.NodeType);
		// The older facility icons are dark artwork; use the supplied white-on-dark POIs.
		if (Kind == ENavDestKind::Facility)
			IconPath = TEXT("/Game/UI/Nav/Skin/poi_toilet.poi_toilet");
		else if (Kind == ENavDestKind::Entrance)
			IconPath = TEXT("/Game/UI/Nav/Skin/poi_entrance.poi_entrance");
		if (UTexture2D* Tex = IconPath.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr, *IconPath))
		{
			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Icon->SetBrushFromTexture(Tex);
			Icon->SetDesiredSizeOverride(FVector2D(54.f, 54.f));
			if (UHorizontalBoxSlot* IconSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Icon)))
			{
				IconSlot->SetVerticalAlignment(VAlign_Center);
				IconSlot->SetPadding(FMargin(8.f, 8.f, 14.f, 8.f));
			}
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(N.Label));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.97f, 1.f)));
		Label->SetJustification(ETextJustify::Left);
		Label->SetAutoWrapText(true);   // "티라노사우루스 렉스"처럼 긴 이름이 버튼 밖으로 안 나가게.
		{
			FSlateFontInfo LabelFont = Label->GetFont();
			LabelFont.Size = 18;
			Label->SetFont(LabelFont);
		}
		if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetHorizontalAlignment(HAlign_Fill);
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));   // 남는 폭을 글자가 차지 → 줄바꿈 여유.
			TextSlot->SetPadding(FMargin(0.f, 8.f, 16.f, 8.f));
		}

		// 오른쪽 끝 > 표시. "누르면 안내가 시작된다"는 것을 알리는 목업의 장치다.
		// chevron_right 는 DinoCard 에서 쓰던 것을 그대로 재사용한다.
		if (UTexture2D* ChevronTex = LoadObject<UTexture2D>(
				nullptr, TEXT("/Game/UI/DinoCard/Icons/chevron_right.chevron_right")))
		{
			UImage* Chevron = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Chevron->SetBrushFromTexture(ChevronTex);
			Chevron->SetDesiredSizeOverride(FVector2D(34.f, 34.f));
			Chevron->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.55f));
			if (UHorizontalBoxSlot* ChevSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Chevron)))
			{
				ChevSlot->SetVerticalAlignment(VAlign_Center);
				ChevSlot->SetPadding(FMargin(0.f, 8.f, 12.f, 8.f));
			}
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
	if (SkinButtonHost != nullptr)
	{
		SkinButtonHost->ClearChildren();
		if (UOverlaySlot* HostSlot = Cast<UOverlaySlot>(SkinButtonHost->AddChild(Grid)))
		{
			HostSlot->SetHorizontalAlignment(HAlign_Fill);
			HostSlot->SetVerticalAlignment(VAlign_Fill);
		}
		return;
	}
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
