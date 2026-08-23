#include "NavGuideLogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "NavMinimapWidget.h"
#include "NavLocalizer.h"
#include "NavDestinations.h"
#include "NavClient.h"   // LogNav

TSharedRef<SWidget> UNavGuideLogWidget::RebuildWidget()
{
	// WBP 없이 만들어졌으면(순수 C++ 경로) 위젯 트리를 여기서 세운다. WBP 로 상속했으면
	// RootWidget 이 이미 있어 이 블록을 건너뛰고, BindWidgetOptional 로 자식을 받는다.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Root;

		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Bar->SetBrushColor(FLinearColor(0.08f, 0.08f, 0.10f, 0.82f));   // 반투명 짙은 바.
		Bar->SetPadding(FMargin(18.f, 10.f));
		Bar->SetHorizontalAlignment(HAlign_Center);
		Bar->SetVerticalAlignment(VAlign_Center);
		BarPanel = Bar;

		UTextBlock* Msg = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Msg->SetJustification(ETextJustify::Center);
		Msg->SetAutoWrapText(true);
		Msg->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo Font = Msg->GetFont();
		Font.Size = 20;
		Msg->SetFont(Font);
		MessageText = Msg;
		Bar->SetContent(Msg);

		if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Bar)))
		{
			// 화면 상단, 가로로 좌우 5% 를 뺀 폭으로 스트레치. 세로는 고정 높이 안에서 줄바꿈.
			// 스트레치 앵커라 Offsets 는 (좌인셋, 상단Y, 우인셋, 높이) 로 읽힌다.
			BarSlot->SetAnchors(FAnchors(0.05f, 0.f, 0.95f, 0.f));
			BarSlot->SetAutoSize(false);
			BarSlot->SetOffsets(FMargin(0.f, 24.f, 0.f, 96.f));
		}
	}
	return Super::RebuildWidget();
}

void UNavGuideLogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Refresh();
}

void UNavGuideLogWidget::NativeDestruct()
{
	if (BoundLocalizer.IsValid())
	{
		BoundLocalizer->OnLocalized.RemoveDynamic(this, &UNavGuideLogWidget::HandleLocalized);
		BoundLocalizer->OnLocalizationLost.RemoveDynamic(this, &UNavGuideLogWidget::HandleLocalizationLost);
		BoundLocalizer->OnAnchorChanged.RemoveDynamic(this, &UNavGuideLogWidget::HandleAnchorChanged);
	}
	if (BoundMinimap.IsValid())
	{
		BoundMinimap->OnFullMapOpenChanged.RemoveDynamic(this, &UNavGuideLogWidget::HandleFullMapOpenChanged);
		BoundMinimap->OnDestinationChosen.RemoveDynamic(this, &UNavGuideLogWidget::HandleDestinationChosen);
		BoundMinimap->OnGuidanceUpdated.RemoveDynamic(this, &UNavGuideLogWidget::HandleGuidanceUpdated);
	}
	Super::NativeDestruct();
}

void UNavGuideLogWidget::BindToMinimap(UNavMinimapWidget* Minimap)
{
	if (Minimap == nullptr || Minimap == BoundMinimap.Get())
	{
		return;
	}
	BoundMinimap = Minimap;
	Minimap->OnFullMapOpenChanged.AddDynamic(this, &UNavGuideLogWidget::HandleFullMapOpenChanged);
	Minimap->OnDestinationChosen.AddDynamic(this, &UNavGuideLogWidget::HandleDestinationChosen);
	Minimap->OnGuidanceUpdated.AddDynamic(this, &UNavGuideLogWidget::HandleGuidanceUpdated);

	// 측위 신호는 서브시스템에서 직접 받는다(NavStatusWidget 과 같은 자동 연결).
	BoundLocalizer = UNavLocalizer::GetNavLocalizer(this);
	if (BoundLocalizer.IsValid())
	{
		BoundLocalizer->OnLocalized.AddDynamic(this, &UNavGuideLogWidget::HandleLocalized);
		BoundLocalizer->OnLocalizationLost.AddDynamic(this, &UNavGuideLogWidget::HandleLocalizationLost);
		BoundLocalizer->OnAnchorChanged.AddDynamic(this, &UNavGuideLogWidget::HandleAnchorChanged);

		// 이미 측위된 상태에서 붙었으면 곧장 그 단계로.
		if (BoundLocalizer->IsLocalized() && Phase == ENavGuidePhase::NavOn)
		{
			Phase = ENavGuidePhase::Localized;
		}
	}
	Refresh();
}

// ---------------------------------------------------------------------- 신호 핸들러

void UNavGuideLogWidget::HandleLocalized(const FString& /*MarkerCode*/)
{
	// 아직 목적지가 없으면 "목적지를 설정" 단계로. 안내 중이면 유지.
	if (!bHasDestination)
	{
		Phase = ENavGuidePhase::Localized;
		Refresh();
	}
}

void UNavGuideLogWidget::HandleLocalizationLost()
{
	Phase = ENavGuidePhase::NavOn;
	Refresh();
}

void UNavGuideLogWidget::HandleAnchorChanged(const FString& MarkerCode)
{
	// 안내 중/도착 상태에서 시작 마커가 아닌 마커(=전시물 마커)로 앵커가 바뀌면 "인식 완료"(초록).
	// 시작 마커로의 전환은 인식 완료가 아니다.
	const bool bStartMarker = MarkerCode.Contains(TEXT("START")) || MarkerCode.Contains(TEXT("NEUTI4"));
	if (!bStartMarker && (Phase == ENavGuidePhase::Guiding || Phase == ENavGuidePhase::Arrived))
	{
		Phase = ENavGuidePhase::Recognized;
		Refresh();
	}
}

void UNavGuideLogWidget::HandleFullMapOpenChanged(bool bOpen)
{
	if (bOpen)
	{
		Phase = ENavGuidePhase::FullMapOpen;
	}
	else
	{
		// 닫힘 → 목적지가 있으면 안내, 없으면 측위 단계로 돌아간다.
		Phase = bHasDestination ? ENavGuidePhase::Guiding : ENavGuidePhase::Localized;
	}
	Refresh();
}

void UNavGuideLogWidget::HandleDestinationChosen(const FString& NodeId)
{
	bHasDestination = !NodeId.IsEmpty();
	Phase = bHasDestination ? ENavGuidePhase::Guiding : ENavGuidePhase::Localized;
	Refresh();
}

void UNavGuideLogWidget::HandleGuidanceUpdated(const FNavGuidance& Guidance)
{
	if (!Guidance.bValid)
	{
		return;
	}
	bHasDestination = true;
	// 도착하면 인식 완료(초록)로 넘어가기 전까지 도착 문구. 그 외에는 안내 중.
	if (Guidance.bArrived)
	{
		if (Phase != ENavGuidePhase::Recognized)
		{
			Phase = ENavGuidePhase::Arrived;
			Refresh();
		}
	}
	else if (Phase != ENavGuidePhase::Guiding && Phase != ENavGuidePhase::FullMapOpen)
	{
		Phase = ENavGuidePhase::Guiding;
		Refresh();
	}
}

// ---------------------------------------------------------------------- 표시

void UNavGuideLogWidget::Refresh()
{
	ApplyPhase();
}

void UNavGuideLogWidget::ApplyPhase()
{
	if (MessageText == nullptr)
	{
		return;   // 트리가 아직 안 섰다(RebuildWidget 전).
	}

	// 목적지 종류/이름은 미니맵이 들고 있는 걸 그때그때 본다(새 데이터 없음).
	FString DestType, DestLabel;
	if (BoundMinimap.IsValid())
	{
		DestType = BoundMinimap->GetDestinationNodeType();
		DestLabel = BoundMinimap->GetDestinationLabel();
	}

	FString Text;
	FLinearColor Color = FLinearColor::White;

	switch (Phase)
	{
	case ENavGuidePhase::NavOn:
		Text = TEXT("바닥의 마커를 인식시켜주세요");
		break;
	case ENavGuidePhase::Localized:
		Text = TEXT("우측의 미니맵을 터치하여 목적지를 설정해주세요");
		break;
	case ENavGuidePhase::FullMapOpen:
		Text = TEXT("지도의 아이콘을 누르거나 아래의 전시물 및 편의시설 버튼을 눌러서 목적지를 설정해주세요");
		break;
	case ENavGuidePhase::Guiding:
		Text = FNavDestinations::GuidingText(DestType);
		break;
	case ENavGuidePhase::Arrived:
		Text = FNavDestinations::ArrivalText(DestType, DestLabel);
		break;
	case ENavGuidePhase::Recognized:
		Text = FNavDestinations::RecognizedText();
		Color = FNavDestinations::AccentColor(ENavDestKind::Entrance);   // 초록.
		break;
	}

	MessageText->SetText(FText::FromString(Text));
	MessageText->SetColorAndOpacity(FSlateColor(Color));
}
