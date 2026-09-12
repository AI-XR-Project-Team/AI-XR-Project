#include "NavGuideLogWidget.h"

#include "TimerManager.h"
#include "Engine/World.h"
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
		Bar->SetBrushColor(FLinearColor(0.06f, 0.06f, 0.08f, 0.94f));   // 짙은 바(지도 위에서도 선명).
		Bar->SetPadding(FMargin(18.f, 10.f));
		Bar->SetHorizontalAlignment(HAlign_Fill);   // 텍스트가 바 폭에 맞춰 줄바꿈되도록 채움.
		Bar->SetVerticalAlignment(VAlign_Center);
		// 바깥으로 글씨가 삐져나가도 잘라 낸다(안전망). 폭·높이는 아래에서 넉넉히 준다.
		Bar->SetClipping(EWidgetClipping::ClipToBounds);
		BarPanel = Bar;

		UTextBlock* Msg = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Msg->SetJustification(ETextJustify::Center);
		Msg->SetAutoWrapText(true);
		Msg->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo Font = Msg->GetFont();
		Font.Size = 18;   // 긴 문구가 네모 안에 들어오도록 살짝 줄인다.
		Msg->SetFont(Font);
		MessageText = Msg;
		Bar->SetContent(Msg);

		if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Bar)))
		{
			// 화면 상단 왼쪽. 오른쪽 미니맵 자리를 비우되, 긴 문구가 삐져나가지 않도록
			// 폭을 화면의 왼쪽 ~75% 까지 넓히고 높이도 넉넉히(줄바꿈 3줄까지) 잡는다.
			// 스트레치 앵커라 Offsets 는 (좌인셋, 상단Y, 우인셋, 높이) 로 읽힌다.
			BarSlot->SetAnchors(FAnchors(0.03f, 0.f, 0.75f, 0.f));
			BarSlot->SetAutoSize(false);
			BarSlot->SetOffsets(FMargin(0.f, 24.f, 0.f, 132.f));
		}
	}
	return Super::RebuildWidget();
}

void UNavGuideLogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 표시 전용 오버레이(터치 안 먹음). 미니맵이 실제로 표출되기 전엔 숨겨 두고(시작 깜빡임 방지),
	// 타이머가 미니맵의 표출을 확인하면 켠다. 타이머는 위젯 visibility 와 무관하게 돌아 Collapsed
	// 로 시작해도 되살릴 수 있다(NativeTick 은 Collapsed 면 멈춰 못 씀).
	SetVisibility(ESlateVisibility::Collapsed);
	Refresh();

	// 표시/숨김 + 문구 갱신은 월드 타이머로 돌린다(0.12초 간격).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			VisSyncTimer, this, &UNavGuideLogWidget::SyncWithMinimap, 0.12f, true);
	}
}

void UNavGuideLogWidget::SyncWithMinimap()
{
	// 안내 로그는 **미니맵이 화면에 실제로 표출될 때만** 보인다(=네비 기능 활성). 미니맵은
	// 앱 내내 살아 있고 네비 버튼이 표시/숨김만 토글하므로, 존재 여부가 아니라 "지금 그려지고
	// 있는가"로 판정해야 한다. 숨겨진(부모 접힘 포함) 위젯은 NativePaint 가 멈춰 시각이 안 는다.
	const bool bNavShown = BoundMinimap.IsValid() && BoundMinimap->WasRecentlyPainted();

	// The full map now owns its Lexi speech bubble.
	const ESlateVisibility Want = bNavShown && Phase != ENavGuidePhase::FullMapOpen
		? ESlateVisibility::HitTestInvisible   // 표시 전용(터치 안 먹음).
		: ESlateVisibility::Collapsed;
	if (GetVisibility() != Want)
	{
		SetVisibility(Want);
	}

	// 보이는 동안은 문구를 미니맵의 실제 상태(목적지 유무 등)에 맞춰 다시 계산한다 —
	// 신호가 안 와도 "목적지 없는데 안내 문구" 같은 꼬임이 남지 않는다.
	if (bNavShown)
	{
		Refresh();
	}
}

void UNavGuideLogWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VisSyncTimer);
	}
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
	bool bDestSet = false;
	if (BoundMinimap.IsValid())
	{
		DestType = BoundMinimap->GetDestinationNodeType();
		DestLabel = BoundMinimap->GetDestinationLabel();
		bDestSet = BoundMinimap->HasDestination();
	}

	// 상태가 꼬여 목적지가 없는데 안내/도착 문구(예: "공룡 발자국을 따라가주세요")로 가 있으면
	// 목적지 설정 안내로 되돌린다. 미니맵의 실제 목적지 유무를 진실의 근원으로 삼는다.
	ENavGuidePhase Effective = Phase;
	if (!bDestSet && (Effective == ENavGuidePhase::Guiding || Effective == ENavGuidePhase::Arrived))
	{
		Effective = ENavGuidePhase::Localized;
	}

	FString Text;
	FLinearColor Color = FLinearColor::White;

	switch (Effective)
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
