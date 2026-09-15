#include "NavGuideLogWidget.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "NavMinimapWidget.h"
#include "NavLocalizer.h"
#include "NavDestinations.h"
#include "NavClient.h"   // LogNav
#include "DocentChatWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

namespace
{
	/** 렉시 스킨 폴더. Content/UI/Nav/Skin → /Game/UI/Nav/Skin(final §0 조사 결과). */
	FString LexiSkinPath(const TCHAR* Name)
	{
		return FString::Printf(TEXT("/Game/UI/Nav/Skin/%s.%s"), Name, Name);
	}

	// 스캔 HUD 렉시 말풍선(DocentChatWidget::ApplyScanSkin)과 같은 유리 패널 색.
	const FLinearColor BubblePanelColor = FLinearColor::FromSRGBColor(FColor(18, 22, 30, 215));
	const FLinearColor NameTextColor = FLinearColor::FromSRGBColor(FColor(0x9C, 0xD8, 0xFF));
	const FLinearColor AccentBlue = FLinearColor::FromSRGBColor(FColor(0x4B, 0xA3, 0xFF));
	const FLinearColor AccentOrange = FLinearColor::FromSRGBColor(FColor(0xEF, 0x7D, 0x1E));
}

TSharedRef<SWidget> UNavGuideLogWidget::RebuildWidget()
{
	// WBP 없이 만들어졌으면(순수 C++ 경로) 위젯 트리를 여기서 세운다. WBP 로 상속했으면
	// RootWidget 이 이미 있어 이 블록을 건너뛰고, BindWidgetOptional 로 자식을 받는다.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Root;

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		// 아바타 칸(고정 96×96). 텍스처 로드에 실패하면 ApplyPhase 가 이 칸을 접는다.
		USizeBox* AvatarSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		AvatarSizeBox->SetWidthOverride(150.f);
		AvatarSizeBox->SetHeightOverride(150.f);
		AvatarBox = AvatarSizeBox;

		UImage* Avatar = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		AvatarImage = Avatar;
		AvatarSizeBox->SetContent(Avatar);

		if (UHorizontalBoxSlot* AvatarSlot = Row->AddChildToHorizontalBox(AvatarSizeBox))
		{
			AvatarSlot->SetVerticalAlignment(VAlign_Bottom);
			AvatarSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
		}

		// 말풍선 본체(유리 패널 + 단계별 강조색 외곽선). 색은 ApplyPhase 가 매번 다시 칠한다.
		UBorder* Bubble = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Bubble->SetPadding(FMargin(24.f, 16.f));
		BubblePanel = Bubble;
		if (UHorizontalBoxSlot* BubbleSlot = Row->AddChildToHorizontalBox(Bubble))
		{
			BubbleSlot->SetVerticalAlignment(VAlign_Bottom);
			// 남은 가로 폭을 전부 채운다 — 그래야 MessageText 의 AutoWrapText 가 의미 있는
			// 폭 기준으로 줄바꿈한다(Row 자체는 CanvasSlot 의 스트레치 앵커로 폭이 정해진다).
			BubbleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Bubble->SetContent(Column);

		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Name->SetText(FText::FromString(TEXT("렉시")));
		Name->SetColorAndOpacity(FSlateColor(NameTextColor));
		{
			FSlateFontInfo Font = Name->GetFont();
			Font.Size = 20;
			Name->SetFont(Font);
		}
		NameText = Name;
		Column->AddChildToVerticalBox(Name);

		UTextBlock* Msg = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Msg->SetJustification(ETextJustify::Left);
		Msg->SetAutoWrapText(true);
		Msg->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		{
			FSlateFontInfo Font = Msg->GetFont();
			Font.Size = 28;
			Msg->SetFont(Font);
		}
		MessageText = Msg;
		if (UVerticalBoxSlot* MsgSlot = Column->AddChildToVerticalBox(Msg))
		{
			MsgSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}

		if (UCanvasPanelSlot* RowSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Row)))
		{
			// 화면 상단 왼쪽. 오른쪽 미니맵 자리를 비우고, 긴 문구가 삐져나가지 않도록
			// 폭을 화면의 왼쪽 ~72% 까지 넓힌다. 세로는 점 앵커(0..0) 라 고정 높이로
			// 아바타(96)+이름표+말풍선 3줄까지 여유 있게 잡는다.
			// 화면 **하단** 가로 꽉 채움(양옆 4%, 아래 여백 48). 내비 화면은 하단 탭바가 없어 이 자리가
			// 비어 있고, 상단은 X·미니맵과 겹친다. 높이는 내용에 맞춰 자동(AutoSize).
			RowSlot->SetAnchors(FAnchors(0.04f, 1.f, 0.96f, 1.f));
			RowSlot->SetAlignment(FVector2D(0.f, 1.f));
			RowSlot->SetAutoSize(true);
			RowSlot->SetOffsets(FMargin(0.f, -48.f, 0.f, 0.f));
		}
	}
	return Super::RebuildWidget();
}

void UNavGuideLogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureAvatarTextures();
	// 표시 전용 오버레이(터치 안 먹음). 미니맵이 실제로 표출되기 전엔 숨겨 두고(시작 깜빡임 방지),
	// 타이머가 미니맵의 표출을 확인하면 켠다. 타이머는 위젯 visibility 와 무관하게 돌아 Collapsed
	// 로 시작해도 되살릴 수 있다(NativeTick 은 Collapsed 면 멈춰 못 씀).
	SetVisibility(ESlateVisibility::Collapsed);
	AdoptScanHintStyle();
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

	// 전체 지도는 자기 렉시 말풍선을 따로 갖는다.
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
		if (!bScanStyleAdopted)
		{
			AdoptScanHintStyle();   // DocentChat 이 늦게 떠도 한 번은 잡는다.
		}
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
		BoundMinimap->OnNavigationEnded.RemoveDynamic(this, &UNavGuideLogWidget::HandleNavigationEnded);
		BoundMinimap->OnNavigationClosed.RemoveDynamic(this, &UNavGuideLogWidget::HandleNavigationClosed);
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
	Minimap->OnNavigationEnded.AddDynamic(this, &UNavGuideLogWidget::HandleNavigationEnded);
	Minimap->OnNavigationClosed.AddDynamic(this, &UNavGuideLogWidget::HandleNavigationClosed);

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
	// 안내 중/이탈/도착 상태에서 시작 마커가 아닌 마커(=전시물 마커)로 앵커가 바뀌면
	// "인식 완료"(초록). 시작 마커로의 전환은 인식 완료가 아니다.
	const bool bStartMarker = MarkerCode.Contains(TEXT("START")) || MarkerCode.Contains(TEXT("NEUTI4"));
	const bool bFromNavState = Phase == ENavGuidePhase::Guiding || Phase == ENavGuidePhase::OffRoute
		|| Phase == ENavGuidePhase::Arrived;
	if (!bStartMarker && bFromNavState)
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
	LastGuidance = Guidance;   // Guiding 문구가 매 틱 바뀔 수 있어 ApplyPhase 가 다시 읽는다.

	// 도착하면 인식 완료(초록)로 넘어가기 전까지 도착 문구. 그 외에는 이탈/안내.
	if (Guidance.bArrived)
	{
		if (Phase != ENavGuidePhase::Recognized && Phase != ENavGuidePhase::Ended)
		{
			Phase = ENavGuidePhase::Arrived;
			Refresh();
		}
	}
	else if (Phase != ENavGuidePhase::FullMapOpen && Phase != ENavGuidePhase::Ended)
	{
		// Ended 는 여기서 제외한다 — 도착 후 자동 종료(§4) 대기 중에 bArrived 가 히스테리시스
		// 경계에서 잠깐 false 로 흔들려도 안내/이탈 문구로 되돌아가면 안 된다(Closed 까지는
		// 마무리 문구를 유지한다). OnNavigationClosed 가 오면 Localized 로 확실히 정리된다.
		const ENavGuidePhase Next = Guidance.bOffRoute ? ENavGuidePhase::OffRoute : ENavGuidePhase::Guiding;
		// OffRoute↔Guiding 전환이든, Guiding 유지 중 문구만 바뀌는 경우든 매번 Refresh 해야
		// LastGuidance 최신값이 반영된다(§3: "Guiding 은 매 틱 문구가 바뀔 수 있다").
		Phase = Next;
		Refresh();
	}
}

void UNavGuideLogWidget::HandleNavigationEnded()
{
	// §4 도착 후 자동 종료 — Dwell 이 다 찼다. 잠깐 마무리 문구로 갈아탄다.
	Phase = ENavGuidePhase::Ended;
	Refresh();
}

void UNavGuideLogWidget::HandleNavigationClosed()
{
	// §4 정리 완료(ClearRoute+SetDestinationNode("")) — 목적지 없음 상태로 되돌아간다.
	bHasDestination = false;
	Phase = ENavGuidePhase::Localized;
	Refresh();
}

// ---------------------------------------------------------------------- 표시

void UNavGuideLogWidget::Refresh()
{
	ApplyPhase();
}

void UNavGuideLogWidget::PreviewSetState(ENavGuidePhase InPhase, const FString& NodeType, const FString& Label,
	const FNavGuidance& Guidance)
{
	Phase = InPhase;
	LastGuidance = Guidance;
	bUsePreviewDestination = true;
	PreviewNodeType = NodeType;
	PreviewLabel = Label;
	// NativeConstruct 는 미니맵이 실제로 그려질 때까지 Collapsed 로 둔다(SyncWithMinimap).
	// 미니맵 없이 단독 렌더하는 미리보기/테스트는 그 신호가 없으므로 여기서 직접 켠다.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Refresh();
}

bool UNavGuideLogWidget::AdoptScanHintStyle()
{
	if (bScanStyleAdopted || AvatarImage == nullptr || BubblePanel == nullptr || MessageText == nullptr)
	{
		return bScanStyleAdopted;
	}
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UDocentChatWidget::StaticClass(), true);
	if (Widgets.Num() == 0)
	{
		return false;
	}
	UUserWidget* Hud = Widgets[0];
	UBorder* ScanBG = Cast<UBorder>(Hud->GetWidgetFromName(TEXT("ScanHintBG")));
	UImage* Robot = Cast<UImage>(Hud->GetWidgetFromName(TEXT("RefHintRobot")));
	UTextBlock* ScanText = Cast<UTextBlock>(Hud->GetWidgetFromName(TEXT("ScanHintText")));
	USizeBox* RobotSize = Cast<USizeBox>(Hud->GetWidgetFromName(TEXT("RefRobotSize")));
	if (ScanBG == nullptr || Robot == nullptr || ScanText == nullptr)
	{
		return false;
	}
	ScanBubbleBrush = ScanBG->Background;
	BubblePanel->SetPadding(ScanBG->GetPadding());
	AvatarImage->SetBrush(Robot->GetBrush());
	if (RobotSize != nullptr && AvatarBox != nullptr)
	{
		AvatarBox->SetWidthOverride(RobotSize->GetWidthOverride() > 0.f ? RobotSize->GetWidthOverride() : 110.f);
		AvatarBox->SetHeightOverride(RobotSize->GetHeightOverride() > 0.f ? RobotSize->GetHeightOverride() : 110.f);
	}
	MessageText->SetFont(ScanText->GetFont());
	MessageText->SetColorAndOpacity(ScanText->GetColorAndOpacity());
	if (NameText != nullptr)
	{
		NameText->SetVisibility(ESlateVisibility::Collapsed);   // 스캔 말풍선엔 이름표가 없다.
	}
	bScanStyleAdopted = true;
	ApplyPhase();
	return true;
}

void UNavGuideLogWidget::EnsureAvatarTextures()
{
	if (bAvatarTexturesLoaded)
	{
		return;
	}
	bAvatarTexturesLoaded = true;

	auto Load = [](const TCHAR* Name) -> UTexture2D*
	{
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *LexiSkinPath(Name));
		if (Tex == nullptr)
		{
			UE_LOG(LogNav, Warning, TEXT("[guidelog] 렉시 아바타 로드 실패: %s (텍스처 임포트/쿡 누락?). 아바타 칸을 접는다."), Name);
		}
		return Tex;
	};
	AvatarQuestionTex = Load(TEXT("lexi_question"));
	AvatarPointingTex = Load(TEXT("lexi_pointing"));
	AvatarHappyTex = Load(TEXT("lexi_happy"));
	AvatarIdleTex = Load(TEXT("lexi_idle"));
}

void UNavGuideLogWidget::ApplyPhase()
{
	if (MessageText == nullptr)
	{
		return;   // 트리가 아직 안 섰다(RebuildWidget 전).
	}

	// 목적지 종류/이름은 미니맵이 들고 있는 걸 그때그때 본다(새 데이터 없음). 미니맵이 안
	// 붙었으면(렌더 미리보기·테스트) PreviewSetState 로 받은 override 를 대신 쓴다.
	FString DestType, DestLabel;
	bool bDestSet = false;
	if (BoundMinimap.IsValid())
	{
		DestType = BoundMinimap->GetDestinationNodeType();
		DestLabel = BoundMinimap->GetDestinationLabel();
		bDestSet = BoundMinimap->HasDestination();
	}
	else if (bUsePreviewDestination)
	{
		DestType = PreviewNodeType;
		DestLabel = PreviewLabel;
		bDestSet = true;
	}

	// 상태가 꼬여 목적지가 없는데 안내/이탈/도착 문구로 가 있으면 목적지 설정 안내로
	// 되돌린다. 미니맵의 실제 목적지 유무를 진실의 근원으로 삼는다.
	ENavGuidePhase Effective = Phase;
	if (!bDestSet && (Effective == ENavGuidePhase::Guiding || Effective == ENavGuidePhase::OffRoute
		|| Effective == ENavGuidePhase::Arrived))
	{
		Effective = ENavGuidePhase::Localized;
	}

	// ---- 문구.
	FString Text;
	switch (Effective)
	{
	case ENavGuidePhase::NavOn:
		Text = FNavDestinations::LexiNavOnText();
		break;
	case ENavGuidePhase::Localized:
		Text = FNavDestinations::LexiLocalizedText();
		break;
	case ENavGuidePhase::FullMapOpen:
		// 전체 지도가 자기 말풍선을 갖는 동안 이 위젯은 Collapsed 다(SyncWithMinimap) —
		// 문구를 새로 만들 필요 없이 마지막 문구를 그대로 둔다.
		Text = MessageText->GetText().ToString();
		break;
	case ENavGuidePhase::Guiding:
		Text = FNavDestinations::LexiGuidingText(DestType, DestLabel, LastGuidance);
		break;
	case ENavGuidePhase::OffRoute:
		Text = FNavDestinations::LexiOffRouteText();
		break;
	case ENavGuidePhase::Arrived:
		Text = FNavDestinations::LexiArrivedText(DestType, DestLabel);
		break;
	case ENavGuidePhase::Recognized:
		Text = FNavDestinations::LexiRecognizedText();
		break;
	case ENavGuidePhase::Ended:
		Text = FNavDestinations::LexiEndedText(DestType);
		break;
	}

	// 텍스트가 같으면 SetText 하지 않는다(§3) — Guiding 은 매 틱 재계산되므로 불필요한
	// 무효화(레이아웃 재계산)를 막는다.
	if (!MessageText->GetText().ToString().Equals(Text, ESearchCase::CaseSensitive))
	{
		MessageText->SetText(FText::FromString(Text));
	}

	// ---- 강조색 + 아바타(§3 표).
	FLinearColor Accent = AccentBlue;
	UTexture2D* Avatar = AvatarHappyTex;
	switch (Effective)
	{
	case ENavGuidePhase::NavOn:
		Avatar = AvatarQuestionTex;
		break;
	case ENavGuidePhase::OffRoute:
		Avatar = AvatarQuestionTex;
		Accent = AccentOrange;
		break;
	case ENavGuidePhase::Guiding:
		Avatar = AvatarPointingTex;
		break;
	case ENavGuidePhase::Localized:
		Avatar = AvatarHappyTex;
		break;
	case ENavGuidePhase::Arrived:
	case ENavGuidePhase::Recognized:
		Avatar = AvatarHappyTex;
		Accent = FNavDestinations::AccentColor(ENavDestKind::Entrance);   // 초록.
		break;
	case ENavGuidePhase::Ended:
		Avatar = AvatarIdleTex;
		break;
	case ENavGuidePhase::FullMapOpen:
	default:
		break;
	}

	if (BubblePanel != nullptr)
	{
		if (bScanStyleAdopted)
		{
			// 스캔 화면 말풍선과 같은 브러시. 스캔 HUD(RefreshReferenceUI)가 하듯 외곽선 색만 단계별로.
			FSlateBrush Brush = ScanBubbleBrush;
			Brush.OutlineSettings.Color = FSlateColor(Accent.CopyWithNewOpacity(0.6f));
			BubblePanel->SetBrush(Brush);
		}
		else
		{
			BubblePanel->SetBrush(FSlateRoundedBoxBrush(BubblePanelColor, 22.f, Accent, 1.5f));
		}
	}

	if (AvatarImage != nullptr)
	{
		if (bScanStyleAdopted)
		{
			// 스캔 화면의 렉시 그림 그대로(단계별 표정 교체 없음 — 사용자 요청).
			AvatarImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (AvatarBox != nullptr) { AvatarBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		}
		else if (Avatar != nullptr)
		{
			AvatarImage->SetBrushFromTexture(Avatar, false);
			AvatarImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (AvatarBox != nullptr) { AvatarBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		}
		else
		{
			// 로드 실패 — 아바타 칸을 접는다. 문구는 계속 보인다.
			if (AvatarBox != nullptr) { AvatarBox->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}
}
