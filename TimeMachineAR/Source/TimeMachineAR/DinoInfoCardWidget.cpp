#include "DinoInfoCardWidget.h"

#include "DinoInfoData.h"
#include "DinoOceanWidgets.h"
#include "DinoStatTile.h"
#include "DinoTabButton.h"
#include "DocentChatWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SafeZone.h"
#include "Components/SafeZoneSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "Widgets/Layout/SBox.h"

DEFINE_LOG_CATEGORY_STATIC(LogDinoCard, Log, All);

TSharedRef<SWidget> UDinoInfoCardWidget::RebuildWidget()
{
	TSharedRef<SBox> Host = SNew(SBox)[Super::RebuildWidget()];
	SkinHost = Host;
	return Host;
}

void UDinoInfoCardWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (bIsOpen && ActiveSkin == EDinoCardSkin::Ocean && ContentScroll && OceanTextCenter)
	{
		const float Height = ContentScroll->GetCachedGeometry().GetLocalSize().Y;
		if (Height > 0.f && !FMath::IsNearlyEqual(OceanTextCenter->GetMinDesiredHeight(), Height, 0.5f))
		{
			OceanTextCenter->SetMinDesiredHeight(Height);
		}
	}
}

void UDinoInfoCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// WBP 가 잡아 둔 루트·클래스를 처음 한 번 기억한다. 바다 스킨에서 돌아올 때 쓴다.
	if (DefaultRoot == nullptr && WidgetTree != nullptr && WidgetTree->RootWidget != OceanRoot)
	{
		DefaultRoot = WidgetTree->RootWidget;
		DefaultStatTileClass = StatTileClass;
		DefaultTabButtonClass = TabButtonClass;
	}

	if (CloseButton != nullptr)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleCloseClicked);
	}

	if (AskDocentButton != nullptr)
	{
		AskDocentButton->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleAskDocentClicked);
	}

	if (FullscreenButton != nullptr)
	{
		FullscreenButton->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleFullscreenClicked);
	}

	// ShowFor 가 NativeConstruct 보다 먼저 올 수 있다(스폰 직후 탭). 그 경우
	// 이미 CurrentInfo 가 차 있으므로 열린 상태를 유지한 채 다시 칠하기만 한다.
	if (CurrentInfo != nullptr)
	{
		ApplyInfo();
	}
	else if (bStartHidden)
	{
		HideCard();
	}
}

void UDinoInfoCardWidget::NativeDestruct()
{
	if (CloseButton != nullptr)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &UDinoInfoCardWidget::HandleCloseClicked);
	}

	if (AskDocentButton != nullptr)
	{
		AskDocentButton->OnClicked.RemoveDynamic(this, &UDinoInfoCardWidget::HandleAskDocentClicked);
	}

	if (FullscreenButton != nullptr)
	{
		FullscreenButton->OnClicked.RemoveDynamic(this, &UDinoInfoCardWidget::HandleFullscreenClicked);
	}

	Super::NativeDestruct();
}

void UDinoInfoCardWidget::ShowFor(UDinoInfoData* InInfo)
{
	if (InInfo == nullptr)
	{
		UE_LOG(LogDinoCard, Warning,
			TEXT("정보가 비어 카드를 열지 않습니다. BP_DinoOverlay 의 DinoInfo 를 지정하세요."));
		return;
	}

	CurrentInfo = InInfo;
	ApplySkin(CurrentInfo->CardSkin);
	ApplyInfo();

	if (ActiveSkin == EDinoCardSkin::Ocean && !HiddenScanHud.IsValid())
	{
		if (UDocentChatWidget* Hud = FindDocentChat())
		{
			HiddenScanHud = Hud;
			ScanHudVisibility = Hud->GetVisibility();
			Hud->SetVisibility(ESlateVisibility::Hidden);
		}
	}
	bIsOpen = true;
	// 지난번에 밀다 만 위치가 남아 있으면 카드가 비뚤게 열린다.
	bSwipeTracking = bSwipeActive = false;
	SwipeOffsetY = 0.f;
	SetCardOffsetY(0.f);

	if (CardPanel != nullptr)
	{
		CardPanel->SetVisibility(ESlateVisibility::Visible);
	}

	// 루트가 Visible 이어야 이 위젯이 직접 터치를 받는다(스와이프 닫기의 전제).
	// 겸사겸사 전체화면 카드가 뒤쪽 AR 을 가려 오탭도 막는다.
	SetVisibility(ESlateVisibility::Visible);

	// 시안은 카드가 하단 바까지 덮는다. BP 가 ZOrder 0 으로 붙였다면 위로 올린다.
	// RemoveFromParent 가 NativeDestruct 를 태우지만, CurrentInfo 가 차 있으면
	// 다음 NativeConstruct 가 열린 상태 그대로 다시 칠하도록 이미 짜여 있다.
	// 스킨이 바뀌어 루트가 갈렸을 때도 같은 경로로 슬레이트를 다시 만든다.
	RebuildSlateIfNeeded();

	FocusCard();

	UE_LOG(LogDinoCard, Log, TEXT("[card] 열기: %s (exhibit=%s)"),
		*CurrentInfo->NameKo.ToString(),
		CurrentInfo->ExhibitKey.IsEmpty() ? TEXT("(미설정)") : *CurrentInfo->ExhibitKey);
}

void UDinoInfoCardWidget::HideCard()
{
	bIsOpen = false;
	if (UDocentChatWidget* Hud = HiddenScanHud.Get())
	{
		Hud->SetVisibility(ScanHudVisibility);
		HiddenScanHud.Reset();
	}

	if (OceanMenuPopup != nullptr)
	{
		OceanMenuPopup->SetVisibility(ESlateVisibility::Collapsed);
	}

	bSwipeTracking = bSwipeActive = false;
	SwipeOffsetY = 0.f;
	SetCardOffsetY(0.f);

	if (CardPanel != nullptr)
	{
		// Collapsed 로 접어야 뒤쪽 AR 화면의 터치를 가로채지 않는다.
		CardPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 루트를 다시 통과 상태로. Hidden 이 아니라 SelfHitTestInvisible 인 이유는
	// 이 위젯 안에 카드 말고 다른 UI 를 둘 수 있기 때문이다 — 그건 계속 살아 있어야 한다.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UDinoInfoCardWidget::ApplyInfo()
{
	if (CurrentInfo == nullptr)
	{
		return;
	}

	// 이름만 필수 바인딩이라 널 검사를 안 해도 되지만, WBP 없이 C++ 단독으로
	// 만들어 쓰는 경우를 대비해 나머지와 같은 경로를 태운다.
	SetTextOrCollapse(NameKoText, CurrentInfo->NameKo);
	SetTextOrCollapse(NumberText, CurrentInfo->DisplayNumber);
	SetTextOrCollapse(NameSciText, CurrentInfo->NameSci);
	SetTextOrCollapse(DietTagText, CurrentInfo->DietTag);
	SetTextOrCollapse(PeriodTagText, CurrentInfo->PeriodTag);
	SetTextOrCollapse(PeriodSubText, CurrentInfo->PeriodSub);

	SetImageOrCollapse(DietIconImage, CurrentInfo->DietIcon);
	SetImageOrCollapse(PeriodIconImage, CurrentInfo->PeriodIcon);
	SetImageOrCollapse(HeroImage, CurrentInfo->HeroImage);

	if (ActiveSkin == EDinoCardSkin::Ocean)
	{
		ApplyOceanInfo();
	}

	// 탭을 먼저 정하고 버튼을 만든다. RebuildTabs 가 첫 탭을 골라 본문·타일까지 채운다
	// (타일은 탭마다 다를 수 있어 ApplyTabContent 가 맡는다).
	ResolveTabs();
	RebuildTabs();

	// 전시물 UUID 가 없으면 도슨트에 물어볼 수가 없다. 버튼만 숨기면 마스코트와
	// "물어보세요" 문구만 남아 더 어색하므로, DocentCta 가 지정돼 있으면 그 줄을
	// 통째로 접는다. 지정 안 된 예전 구성에서는 버튼만 접는다.
	const bool bCanAsk = !CurrentInfo->ExhibitKey.IsEmpty();
	const ESlateVisibility CtaVisibility = bCanAsk
		? ESlateVisibility::Visible
		: ESlateVisibility::Collapsed;

	if (DocentCta != nullptr)
	{
		DocentCta->SetVisibility(CtaVisibility);
	}
	else if (AskDocentButton != nullptr)
	{
		AskDocentButton->SetVisibility(CtaVisibility);
	}
}

void UDinoInfoCardWidget::ResolveTabs()
{
	ResolvedTabs.Reset();

	if (CurrentInfo == nullptr)
	{
		return;
	}

	if (CurrentInfo->Tabs.Num() > 0)
	{
		ResolvedTabs = CurrentInfo->Tabs;
		return;
	}

	// 탭이 없는 DA. 예전 IntroText 하나를 탭처럼 취급한다. 라벨을 "소개" 로
	// 박아 넣지 않는 이유는, 탭이 하나뿐이면 탭 바가 통째로 접히기 때문이다.
	if (!CurrentInfo->IntroText.IsEmpty())
	{
		FDinoTab Fallback;
		Fallback.Body = CurrentInfo->IntroText;
		ResolvedTabs.Add(MoveTemp(Fallback));
	}
}

void UDinoInfoCardWidget::RebuildTabs()
{
	TabButtons.Reset();

	if (TabBar != nullptr)
	{
		TabBar->ClearChildren();

		// 탭이 하나뿐이면 고를 게 없다. 버튼 하나짜리 탭 바는 장식일 뿐이라
		// 본문 위 여백만 차지한다.
		const bool bShowBar = ResolvedTabs.Num() > 1;
		TabBar->SetVisibility(bShowBar
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);

		if (bShowBar)
		{
			if (TabButtonClass == nullptr)
			{
				UE_LOG(LogDinoCard, Warning,
					TEXT("TabButtonClass 가 비어 탭 버튼을 만들 수 없습니다. "
						 "카드 WBP 의 클래스 기본값에서 지정하세요."));
			}
			else
			{
				for (int32 Index = 0; Index < ResolvedTabs.Num(); ++Index)
				{
					UDinoTabButton* Button = CreateWidget<UDinoTabButton>(this, TabButtonClass);
					if (Button == nullptr)
					{
						continue;
					}

					Button->Setup(Index, ResolvedTabs[Index].Label, ResolvedTabs[Index].Icon);
					Button->OnClicked.BindUObject(this, &UDinoInfoCardWidget::HandleTabClicked);

					// 이름을 Slot 으로 두면 UWidget::Slot 멤버를 가려 C4458 이 난다.
					UPanelSlot* AddedSlot = TabBar->AddChild(Button);
					TabButtons.Add(Button);

					// 탭은 바 폭을 균등하게 나눠 가져야 한다. AddChild 로 붙인 슬롯은
					// 기본이 Auto 라 글자 길이대로 폭이 제각각이 된다("소개" 는 좁고
					// "서식지" 는 넓어진다). 탭 개수가 종마다 달라서 고정 폭도 못 쓴다.
					if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(AddedSlot))
					{
						BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					}
				}
			}
		}
	}

	// 다른 공룡으로 갈아 끼웠는데 이전 선택이 남아 있으면 범위를 벗어날 수 있다.
	SelectedTabIndex = 0;
	SelectTab(0);
}

void UDinoInfoCardWidget::SelectTab(int32 TabIndex)
{
	if (!ResolvedTabs.IsValidIndex(TabIndex))
	{
		return;
	}

	SelectedTabIndex = TabIndex;

	for (UDinoTabButton* Button : TabButtons)
	{
		if (Button != nullptr)
		{
			Button->SetSelected(Button->GetTabIndex() == TabIndex);
		}
	}

	ApplyTabContent();
}

void UDinoInfoCardWidget::ApplyTabContent()
{
	if (!ResolvedTabs.IsValidIndex(SelectedTabIndex))
	{
		SetTextOrCollapse(TabHeading, FText::GetEmpty());
		SetTextOrCollapse(IntroLabel, FText::GetEmpty());
		SetImageOrCollapse(TabImage, nullptr);
		// 탭이 하나도 없는 DA 라도 종 공통 타일은 보여 준다.
		RebuildStats();
		return;
	}

	const FDinoTab& Tab = ResolvedTabs[SelectedTabIndex];

	SetTextOrCollapse(TabHeading, Tab.Heading);
	SetTextOrCollapse(IntroLabel, Tab.Body);
	SetImageOrCollapse(TabImage, Tab.Image);

	// 탭 전용 타일이 있으면 그것, 없으면 종 공통. 탭을 바꿀 때마다 새로 만든다.
	RebuildStats();

	if (ActiveSkin == EDinoCardSkin::Ocean)
	{
		// CTA 아랫줄: 탭 문구 → 종 공통 문구 순.
		if (OceanCtaPrompt != nullptr && CurrentInfo != nullptr)
		{
			const FText& Prompt = Tab.DocentPrompt.IsEmpty() ? CurrentInfo->DocentPrompt : Tab.DocentPrompt;
			SetTextOrCollapse(OceanCtaPrompt, Prompt);
		}

		// 삽화는 본문 오른쪽 위에 은은하게 깔린다. 텍스처 비율대로 폭 330 에 맞춘다.
		if (TabImage != nullptr && Tab.Image != nullptr)
		{
			const float W = 330.f;
			const float H = W * FMath::Max(1.f, (float)Tab.Image->GetSizeY()) / FMath::Max(1.f, (float)Tab.Image->GetSizeX());
			FSlateBrush Brush;
			Brush.SetResourceObject(Tab.Image);
			Brush.ImageSize = FVector2D(W, H);
			TabImage->SetBrush(Brush);
			TabImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		// 삽화가 있으면 본문이 그 아래로 들어가지 않게 오른쪽을 비운다. 시안 (3)(4)(5).
		if (IntroLabel != nullptr)
		{
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(IntroLabel->Slot))
			{
				S->SetPadding(FMargin(0.f, 0.f, Tab.Image != nullptr ? 270.f : 0.f, 24.f));
			}
		}
		if (TabHeading != nullptr)
		{
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(TabHeading->Slot))
			{
				S->SetPadding(FMargin(0.f, 0.f, Tab.Image != nullptr ? 240.f : 0.f, 18.f));
			}
		}
	}

	// 탭을 바꾸면 새 본문의 처음부터 읽어야 한다.
	if (ContentScroll != nullptr)
	{
		ContentScroll->ScrollToStart();
	}
}

void UDinoInfoCardWidget::HandleTabClicked(int32 TabIndex)
{
	SelectTab(TabIndex);
	FocusCard();
}

void UDinoInfoCardWidget::RebuildStats()
{
	if (StatBox == nullptr)
	{
		return;
	}

	StatBox->ClearChildren();

	if (CurrentInfo == nullptr)
	{
		return;
	}

	// 탭 전용 타일이 있으면 그것, 없으면 종 공통. 예전 DA 는 탭 타일이 비어 있다.
	const TArray<FDinoStat>* Stats = &CurrentInfo->Stats;
	if (ResolvedTabs.IsValidIndex(SelectedTabIndex) && ResolvedTabs[SelectedTabIndex].Stats.Num() > 0)
	{
		Stats = &ResolvedTabs[SelectedTabIndex].Stats;
	}

	if (StatTileClass == nullptr)
	{
		if (Stats->Num() > 0)
		{
			UE_LOG(LogDinoCard, Warning,
				TEXT("StatTileClass 가 비어 요약 타일을 만들 수 없습니다. "
					 "카드 WBP 의 클래스 기본값에서 지정하세요."));
		}
		return;
	}

	for (const FDinoStat& Stat : *Stats)
	{
		UDinoStatTile* Tile = CreateWidget<UDinoStatTile>(this, StatTileClass);
		if (Tile == nullptr)
		{
			continue;
		}

		Tile->SetStat(Stat);
		UPanelSlot* AddedSlot = StatBox->AddChild(Tile);

		// 탭 바와 같은 이유다. AddChild 로 붙인 슬롯은 기본이 Auto 라 글자 길이대로
		// 폭이 제각각이 된다("6~9 ton" 칸은 넓고 "육식" 칸은 좁아진다). 타일 개수가
		// 종마다 달라 고정 폭도 못 쓴다. StatBox 가 WrapBox 면 Cast 가 실패하고
		// 줄바꿈 배치가 그대로 유지된다.
		if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(AddedSlot))
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			// 타일이 서로 붙으면 한 덩어리로 보인다. 시안의 카드 간격.
			BoxSlot->SetPadding(FMargin(6.f, 0.f));
		}
	}
}

void UDinoInfoCardWidget::SetImageOrCollapse(UImage* Image, UTexture2D* Texture)
{
	if (Image == nullptr)
	{
		return;
	}

	if (Texture != nullptr)
	{
		Image->SetBrushFromTexture(Texture);
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		Image->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UDinoInfoCardWidget::SetTextOrCollapse(UTextBlock* Block, const FText& Value)
{
	if (Block == nullptr)
	{
		return;
	}

	Block->SetText(Value);
	Block->SetVisibility(Value.IsEmpty()
		? ESlateVisibility::Collapsed
		: ESlateVisibility::HitTestInvisible);
}

void UDinoInfoCardWidget::HandleCloseClicked()
{
	HideCard();
}

void UDinoInfoCardWidget::HandleAskDocentClicked()
{
	if (CurrentInfo == nullptr || CurrentInfo->ExhibitKey.IsEmpty())
	{
		return;
	}

	if (OceanMenuPopup != nullptr)
	{
		OceanMenuPopup->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 도슨트에서 돌아오면 같은 공룡·탭으로 다시 연다(바다 스킨 기본 정책). 채팅이
	// 닫히는 순간을 챗 위젯이 알려 준다. 레벨 BP 가 HideCard → OpenChat → ShowChat
	// 순으로 부르므로, 열림(true) 알림은 무시하고 닫힘만 본다.
	if (ActiveSkin == EDinoCardSkin::Ocean)
	{
		if (UDocentChatWidget* Chat = FindDocentChat())
		{
			if (BoundDocentChat.Get() != Chat)
			{
				if (UDocentChatWidget* Old = BoundDocentChat.Get())
				{
					Old->OnOpenStateChanged.RemoveDynamic(this, &UDinoInfoCardWidget::HandleDocentOpenStateChanged);
				}
				Chat->OnOpenStateChanged.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleDocentOpenStateChanged);
				BoundDocentChat = Chat;
			}
			ReopenInfo = CurrentInfo;
			ReopenTabIndex = SelectedTabIndex;
			bReopenAfterDocent = true;
		}
	}

	OnAskDocentClicked.Broadcast(CurrentInfo->ExhibitKey);
}

void UDinoInfoCardWidget::HandleFullscreenClicked()
{
	if (CurrentInfo == nullptr)
	{
		return;
	}

	OnFullscreenClicked.Broadcast(CurrentInfo);
}

// ---------------------------------------------------------------- 스와이프로 닫기

bool UDinoInfoCardWidget::BeginSwipe(const FGeometry& InGeometry, const FVector2D& ScreenPos)
{
	if (!bEnableSwipeClose || !bIsOpen)
	{
		return false;
	}

	// 전체 화면 스킨은 본문을 세로로 스크롤한다. 밀어 닫기가 그 제스처를 먹으면 읽다가
	// 카드가 닫힌다. 이 스킨은 뒤로 버튼·안드로이드 뒤로 키로만 닫는다.
	if (ActiveSkin == EDinoCardSkin::Ocean)
	{
		return false;
	}

	// 본문을 읽으려고 위로 스크롤해 둔 상태에서 아래로 미는 것은 "되돌아 올라가기"다.
	// 맨 위일 때만 닫기 제스처로 받는다. iOS 시트·안드로이드 바텀시트와 같은 규칙.
	if (ContentScroll != nullptr && ContentScroll->GetScrollOffset() > 1.f)
	{
		return false;
	}

	SwipeStartY = InGeometry.AbsoluteToLocal(ScreenPos).Y;
	SwipeOffsetY = 0.f;
	bSwipeTracking = true;
	bSwipeActive = false;
	return true;
}

FReply UDinoInfoCardWidget::UpdateSwipe(const FGeometry& InGeometry, const FVector2D& ScreenPos)
{
	if (!bSwipeTracking)
	{
		return FReply::Unhandled();
	}

	const float Delta = InGeometry.AbsoluteToLocal(ScreenPos).Y - SwipeStartY;

	if (!bSwipeActive)
	{
		// 아직 탭인지 드래그인지 모른다. 위로 올리는 중이면 우리 몫이 아니다.
		if (Delta < SwipeStartSlop)
		{
			if (Delta < -SwipeStartSlop)
			{
				bSwipeTracking = false;
				return FReply::Unhandled();
			}
			return FReply::Handled();
		}
		bSwipeActive = true;
	}

	// 위로는 안 따라간다. 카드가 화면 위로 솟으면 상단이 잘려 보인다.
	SwipeOffsetY = FMath::Max(0.f, Delta);
	SetCardOffsetY(SwipeOffsetY);
	return FReply::Handled();
}

FReply UDinoInfoCardWidget::EndSwipe()
{
	const bool bWasActive = bSwipeActive;
	const float Offset = SwipeOffsetY;

	bSwipeTracking = false;
	bSwipeActive = false;
	SwipeOffsetY = 0.f;
	SetCardOffsetY(0.f);

	if (bWasActive && Offset >= SwipeCloseThreshold)
	{
		UE_LOG(LogDinoCard, Log, TEXT("[card] 스와이프로 닫음 (%.0f >= %.0f)"), Offset, SwipeCloseThreshold);
		HideCard();
	}

	return FReply::Handled().ReleaseMouseCapture();
}

void UDinoInfoCardWidget::SetCardOffsetY(float OffsetY)
{
	if (CardPanel != nullptr)
	{
		CardPanel->SetRenderTranslation(FVector2D(0.f, OffsetY));
	}
}

FReply UDinoInfoCardWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
	if (BeginSwipe(InGeometry, InEvent.GetScreenSpacePosition()))
	{
		// 캡처해 두지 않으면 손가락이 움직이는 순간 이벤트가 다른 위젯으로 넘어간다.
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnTouchStarted(InGeometry, InEvent);
}

FReply UDinoInfoCardWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
	const FReply Reply = UpdateSwipe(InGeometry, InEvent.GetScreenSpacePosition());
	return Reply.IsEventHandled() ? Reply : Super::NativeOnTouchMoved(InGeometry, InEvent);
}

FReply UDinoInfoCardWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
	if (bSwipeTracking)
	{
		return EndSwipe();
	}
	return Super::NativeOnTouchEnded(InGeometry, InEvent);
}

FReply UDinoInfoCardWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
	if (BeginSwipe(InGeometry, InEvent.GetScreenSpacePosition()))
	{
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InEvent);
}

FReply UDinoInfoCardWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
	const FReply Reply = UpdateSwipe(InGeometry, InEvent.GetScreenSpacePosition());
	return Reply.IsEventHandled() ? Reply : Super::NativeOnMouseMove(InGeometry, InEvent);
}

FReply UDinoInfoCardWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
	if (bSwipeTracking)
	{
		return EndSwipe();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InEvent);
}

// ================================================================ 바다(아르켈론) 스킨
//
// 시안(941×1672)을 UMG 단위(세로 화면 폭 1080, 최소 높이 1920)로 옮긴 값이다.
// 배율 약 1.15. 아래 수치는 시안에서 눈으로 잰 근사값이며 캡처와 비교해 조정한다.

namespace
{
	/** 새로 만든 UImage 는 아직 Slate 가 없어 SetDesiredSizeOverride 가 무시된다. 브러시 크기로 박는다. */
	void SetOceanImage(UImage* Image, UTexture2D* Texture, const FVector2D& Size, const FLinearColor& Tint)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		// DrawAs 는 늘 Image 로 둔다. 나중에 SetBrushFromTexture 로 텍스처만 갈아 끼우는데,
		// NoDrawType 이 남아 있으면 그때도 안 그려진다. 텍스처가 없는 동안은 접어 둔다.
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Image->SetBrush(Brush);
		Image->SetColorAndOpacity(Tint);
		Image->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	/** 남색 유리 + 가는 청록 테두리. 패널·CTA·하단 바·탭 바가 같은 만듦새다. */
	FSlateRoundedBoxBrush GlassBrush(float Radius, float Alpha = 0.78f)
	{
		FLinearColor Fill = DinoOcean::Panel();
		Fill.A = Alpha;
		return FSlateRoundedBoxBrush(Fill, Radius, DinoOcean::Outline(), 1.5f);
	}

	void StyleGlassButton(UButton* Button, float Radius)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(GlassBrush(Radius));
		Style.SetHovered(FSlateRoundedBoxBrush(DinoOcean::PanelHi(), Radius, DinoOcean::Accent(), 1.5f));
		Style.SetPressed(FSlateRoundedBoxBrush(DinoOcean::PanelHi(), Radius, DinoOcean::Accent(), 1.5f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Button->SetStyle(Style);
	}

	/** 시트의 원형 버튼 스프라이트(원+발광+글리프)를 버튼 배경으로. 없으면 유리 원. */
	void StyleSpriteButton(UButton* Button, const TCHAR* SpriteName, float Size)
	{
		FButtonStyle Style = Button->GetStyle();
		if (UTexture2D* Sprite = DinoOcean::Tex(SpriteName))
		{
			FSlateBrush B;
			B.SetResourceObject(Sprite);
			B.ImageSize = FVector2D(Size, Size);
			Style.SetNormal(B);
			FSlateBrush Hi = B;
			Hi.TintColor = FSlateColor(FLinearColor(1.3f, 1.3f, 1.3f, 1.f));
			Style.SetHovered(Hi);
			Style.SetPressed(Hi);
		}
		else
		{
			Style.SetNormal(GlassBrush(Size * 0.5f));
			Style.SetHovered(GlassBrush(Size * 0.5f, 0.95f));
			Style.SetPressed(GlassBrush(Size * 0.5f, 0.95f));
		}
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Button->SetStyle(Style);
	}

	void StyleTransparentButton(UButton* Button)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 24.f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.05f), 24.f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.10f), 24.f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Button->SetStyle(Style);
	}
}

bool UDinoInfoCardWidget::ApplySkin(EDinoCardSkin Skin)
{
	if (Skin == ActiveSkin)
	{
		return false;
	}
	if (WidgetTree == nullptr)
	{
		return false;
	}

	// 처음 스킨을 바꿀 때 아직 NativeConstruct 가 안 돌았을 수 있다(스폰 직후 탭).
	if (DefaultRoot == nullptr && WidgetTree->RootWidget != OceanRoot)
	{
		DefaultRoot = WidgetTree->RootWidget;
		DefaultStatTileClass = StatTileClass;
		DefaultTabButtonClass = TabButtonClass;
	}

	if (Skin == EDinoCardSkin::Ocean)
	{
		BuildOceanSkin();
	}
	else
	{
		RestoreDefaultSkin();
	}

	ActiveSkin = Skin;
	bSlateDirty = true;

	// 스킨이 바뀌면 버튼 포인터도 바뀐다. 이미 구성된 상태면 새 버튼에 바로 잇는다.
	if (CloseButton != nullptr)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleCloseClicked);
	}
	if (AskDocentButton != nullptr)
	{
		AskDocentButton->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleAskDocentClicked);
	}
	return true;
}

void UDinoInfoCardWidget::RestoreDefaultSkin()
{
	if (DefaultRoot == nullptr)
	{
		return;
	}
	WidgetTree->RootWidget = DefaultRoot;
	StatTileClass = DefaultStatTileClass;
	TabButtonClass = DefaultTabButtonClass;

	// WBP 의 BindWidget 은 이름으로 맺어졌다. 같은 이름으로 다시 찾는다.
	NameKoText      = WidgetTree->FindWidget<UTextBlock>(TEXT("NameKoText"));
	NumberText      = WidgetTree->FindWidget<UTextBlock>(TEXT("NumberText"));
	NameSciText     = WidgetTree->FindWidget<UTextBlock>(TEXT("NameSciText"));
	DietTagText     = WidgetTree->FindWidget<UTextBlock>(TEXT("DietTagText"));
	PeriodTagText   = WidgetTree->FindWidget<UTextBlock>(TEXT("PeriodTagText"));
	PeriodSubText   = WidgetTree->FindWidget<UTextBlock>(TEXT("PeriodSubText"));
	DietIconImage   = WidgetTree->FindWidget<UImage>(TEXT("DietIconImage"));
	PeriodIconImage = WidgetTree->FindWidget<UImage>(TEXT("PeriodIconImage"));
	HeroImage       = WidgetTree->FindWidget<UImage>(TEXT("HeroImage"));
	FullscreenButton= WidgetTree->FindWidget<UButton>(TEXT("FullscreenButton"));
	TabBar          = WidgetTree->FindWidget<UPanelWidget>(TEXT("TabBar"));
	TabImage        = WidgetTree->FindWidget<UImage>(TEXT("TabImage"));
	TabHeading      = WidgetTree->FindWidget<UTextBlock>(TEXT("TabHeading"));
	IntroLabel      = WidgetTree->FindWidget<UTextBlock>(TEXT("IntroLabel"));
	StatBox         = WidgetTree->FindWidget<UPanelWidget>(TEXT("StatBox"));
	CloseButton     = WidgetTree->FindWidget<UButton>(TEXT("CloseButton"));
	AskDocentButton = WidgetTree->FindWidget<UButton>(TEXT("AskDocentButton"));
	DocentCta       = WidgetTree->FindWidget(TEXT("DocentCta"));
	CardPanel       = WidgetTree->FindWidget(TEXT("CardPanel"));
	ContentScroll   = WidgetTree->FindWidget<UScrollBox>(TEXT("ContentScroll"));
}

void UDinoInfoCardWidget::RebuildSlateIfNeeded()
{
	if (bSlateDirty)
	{
		// RootWidget only changes the UObject tree. TakeWidget reuses cached Slate,
		// including the old hidden root retained by the viewport/current touch path.
		// Replace the content in place so both rendering and hit testing switch roots.
		if (TSharedPtr<SBox> Host = SkinHost.Pin())
		{
			Host->SetContent(WidgetTree->RootWidget->TakeWidget());
		}
		bSlateDirty = false;
	}
	if (!IsInViewport())
	{
		return;
	}
	if (!bElevatedZ)
	{
		bElevatedZ = true;
		bSlateDirty = false;
		RemoveFromParent();
		AddToViewport(100);
	}
}

void UDinoInfoCardWidget::BuildOceanSkin()
{
	// 한 번 만든 트리는 재사용한다. 텍스트 위젯을 다시 만들면 바인딩도 전부 다시 잇게 된다.
	if (OceanRoot != nullptr)
	{
		WidgetTree->RootWidget = OceanRoot;
		StatTileClass = UDinoOceanStatTile::StaticClass();
		TabButtonClass = UDinoOceanTabButton::StaticClass();

		// 기본 스킨을 거쳐 왔으면 바인딩 포인터가 WBP 쪽을 가리킨다. 이름으로 되찾는다.
		NameKoText      = WidgetTree->FindWidget<UTextBlock>(TEXT("OceanNameKo"));
		NameSciText     = WidgetTree->FindWidget<UTextBlock>(TEXT("OceanNameSci"));
		DietTagText     = WidgetTree->FindWidget<UTextBlock>(TEXT("OceanDietTag"));
		PeriodTagText   = WidgetTree->FindWidget<UTextBlock>(TEXT("OceanPeriodTag"));
		DietIconImage   = WidgetTree->FindWidget<UImage>(TEXT("OceanDietIcon"));
		PeriodIconImage = WidgetTree->FindWidget<UImage>(TEXT("OceanPeriodIcon"));
		HeroImage       = WidgetTree->FindWidget<UImage>(TEXT("OceanHeroImage"));
		TabBar          = WidgetTree->FindWidget<UPanelWidget>(TEXT("OceanTabBar"));
		TabImage        = WidgetTree->FindWidget<UImage>(TEXT("OceanTabImage"));
		TabHeading      = WidgetTree->FindWidget<UTextBlock>(TEXT("OceanTabHeading"));
		IntroLabel      = WidgetTree->FindWidget<UTextBlock>(TEXT("OceanIntroLabel"));
		StatBox         = WidgetTree->FindWidget<UPanelWidget>(TEXT("OceanStatBox"));
		CloseButton     = WidgetTree->FindWidget<UButton>(TEXT("OceanBackButton"));
		AskDocentButton = WidgetTree->FindWidget<UButton>(TEXT("OceanAskDocentButton"));
		ContentScroll   = WidgetTree->FindWidget<UScrollBox>(TEXT("OceanContentScroll"));
		DocentCta       = AskDocentButton;
		CardPanel       = OceanRoot;
		NumberText = nullptr;
		PeriodSubText = nullptr;
		FullscreenButton = nullptr;
		return;
	}

	using namespace DinoOcean;

	constexpr float SideInset = 30.f;     // 시안 좌우 여백 26px
	constexpr float RoundButton = 96.f;   // 시안 원형 버튼 84px
	constexpr float BarRadius = 34.f;
	constexpr float CtaHeight = 168.f;    // 시안 148px

	UWidgetTree* Tree = WidgetTree;
	auto MakeText = [Tree](FName Name, int32 Size, bool bSemi, const FLinearColor& Color) -> UTextBlock*
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		T->SetFont(Font(Size, bSemi));
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetVisibility(ESlateVisibility::HitTestInvisible);
		return T;
	};

	// ---- 루트: 배경(터치 차단) + 안전영역 열 + 메뉴 팝업
	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("OceanCardPanel"));
	OceanRoot = Root;
	CardPanel = Root;
	Tree->RootWidget = Root;

	UBackgroundBlur* CameraBlur = Tree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(), TEXT("OceanCameraBlur"));
	CameraBlur->SetBlurStrength(3.f);
	CameraBlur->SetApplyAlphaToBlur(false);
	CameraBlur->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* BlurSlot = Root->AddChildToOverlay(CameraBlur))
	{
		BlurSlot->SetHorizontalAlignment(HAlign_Fill);
		BlurSlot->SetVerticalAlignment(VAlign_Fill);
	}

	OceanBackground = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OceanBackground"));
	OceanBackground->SetBrush(FSlateRoundedBoxBrush(Background(), 0.f));
	OceanBackground->SetVisibility(ESlateVisibility::Visible);   // 뒤쪽 AR 액터 탭을 막는다
	if (UOverlaySlot* S = Root->AddChildToOverlay(OceanBackground))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	USafeZone* Safe = Tree->ConstructWidget<USafeZone>();
	if (UOverlaySlot* S = Root->AddChildToOverlay(Safe))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	Safe->SetContent(Column);
	if (USafeZoneSlot* S = Cast<USafeZoneSlot>(Column->Slot))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	// ---- 상단: 뒤로 / 전시존 배지 / 메뉴
	{
		UOverlay* Header = Tree->ConstructWidget<UOverlay>();
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Header))
		{
			S->SetPadding(FMargin(SideInset, 22.f, SideInset, 0.f));
			S->SetHorizontalAlignment(HAlign_Fill);
		}

		CloseButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OceanBackButton"));
		StyleSpriteButton(CloseButton, TEXT("T_BtnBack"), RoundButton);
		{
			USizeBox* Box = Tree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(RoundButton);
			Box->SetHeightOverride(RoundButton);
			CloseButton->SetContent(Box);
		}
		if (UOverlaySlot* S = Header->AddChildToOverlay(CloseButton))
		{
			S->SetHorizontalAlignment(HAlign_Left);
			S->SetVerticalAlignment(VAlign_Center);
		}

		OceanMenuButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OceanMenuButton"));
		StyleSpriteButton(OceanMenuButton, TEXT("T_BtnMenu"), RoundButton);
		{
			USizeBox* Box = Tree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(RoundButton);
			Box->SetHeightOverride(RoundButton);
			OceanMenuButton->SetContent(Box);
		}
		if (UOverlaySlot* S = Header->AddChildToOverlay(OceanMenuButton))
		{
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetVerticalAlignment(VAlign_Center);
		}
		OceanMenuButton->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleMenuClicked);

		UBorder* Badge = Tree->ConstructWidget<UBorder>();
		Badge->SetBrush(GlassBrush(30.f));
		Badge->SetBrushColor(FLinearColor::White);
		Badge->SetPadding(FMargin(26.f, 12.f, 32.f, 12.f));
		Badge->SetHorizontalAlignment(HAlign_Center);
		Badge->SetVerticalAlignment(VAlign_Center);
		if (UOverlaySlot* S = Header->AddChildToOverlay(Badge))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetVerticalAlignment(VAlign_Center);
		}
		UHorizontalBox* BadgeRow = Tree->ConstructWidget<UHorizontalBox>();
		Badge->SetContent(BadgeRow);
		UImage* Pin = Tree->ConstructWidget<UImage>();
		SetOceanImage(Pin, Tex(TEXT("T_IcoZonePin")), FVector2D(40.f, 40.f), Accent());
		if (UHorizontalBoxSlot* S = BadgeRow->AddChildToHorizontalBox(Pin))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 18.f, 0.f));
		}
		UVerticalBox* BadgeText = Tree->ConstructWidget<UVerticalBox>();
		if (UHorizontalBoxSlot* S = BadgeRow->AddChildToHorizontalBox(BadgeText))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		OceanZoneName = MakeText(TEXT("OceanZoneName"), 27, true, DinoOcean::Text());
		OceanZoneName->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* S = BadgeText->AddChildToVerticalBox(OceanZoneName))
		{
			S->SetHorizontalAlignment(HAlign_Center);
		}
		OceanZoneSub = MakeText(TEXT("OceanZoneSub"), 19, false, TextDim());
		OceanZoneSub->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* S = BadgeText->AddChildToVerticalBox(OceanZoneSub))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}
	}

	// ---- 히어로: 왼쪽 큰 거북, 오른쪽 이름·학명·배지. 남는 세로 공간을 여기와 패널이 나눠 갖는다.
	{
		UOverlay* Hero = Tree->ConstructWidget<UOverlay>();
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Hero))
		{
			FSlateChildSize HeroSize(ESlateSizeRule::Fill);
			HeroSize.Value = 0.45f;   // 패널(0.55)과 남는 공간을 나눈다
			S->SetSize(HeroSize);
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
		Hero->SetClipping(EWidgetClipping::ClipToBounds);

		HeroImage = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OceanHeroImage"));
		// 텍스처 1433×972. 시안에서 거북은 폭의 약 72%, 왼쪽 가장자리에 붙는다.
		SetOceanImage(HeroImage, nullptr, FVector2D(780.f, 529.f), FLinearColor::White);
		if (UOverlaySlot* S = Hero->AddChildToOverlay(HeroImage))
		{
			S->SetHorizontalAlignment(HAlign_Left);
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(-16.f, 0.f, 0.f, 0.f));
		}

		UVerticalBox* NameCol = Tree->ConstructWidget<UVerticalBox>();
		if (UOverlaySlot* S = Hero->AddChildToOverlay(NameCol))
		{
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 90.f, SideInset, 0.f));
		}

		NameKoText = MakeText(TEXT("OceanNameKo"), 72, true, Accent());
		NameKoText->SetJustification(ETextJustify::Right);
		NameKoText->SetShadowOffset(FVector2D(0.f, 3.f));
		NameKoText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		if (UVerticalBoxSlot* S = NameCol->AddChildToVerticalBox(NameKoText))
		{
			S->SetHorizontalAlignment(HAlign_Right);
		}

		NameSciText = MakeText(TEXT("OceanNameSci"), 32, false, SRGB(140, 210, 255));
		{
			FSlateFontInfo Sci = NameSciText->GetFont();
			Sci.SkewAmount = 0.22f;   // 시안의 이탤릭 학명. Pretendard 에 이탤릭이 없어 기울인다.
			NameSciText->SetFont(Sci);
		}
		NameSciText->SetJustification(ETextJustify::Right);
		if (UVerticalBoxSlot* S = NameCol->AddChildToVerticalBox(NameSciText))
		{
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetPadding(FMargin(0.f, 0.f, 6.f, 22.f));
		}

		auto MakePill = [&](const TCHAR* IconName, const FLinearColor& Outline, const FLinearColor& IconTint,
			TObjectPtr<UImage>& OutIcon, TObjectPtr<UTextBlock>& OutText, FName TextName)
		{
			UBorder* Pill = Tree->ConstructWidget<UBorder>();
			FLinearColor Fill = Panel();
			Fill.A = 0.8f;
			Pill->SetBrush(FSlateRoundedBoxBrush(Fill, 26.f, Outline, 1.5f));
			Pill->SetBrushColor(FLinearColor::White);
			Pill->SetPadding(FMargin(22.f, 9.f, 26.f, 9.f));
			UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
			Pill->SetContent(Row);
			OutIcon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), IconName);
			SetOceanImage(OutIcon, nullptr, FVector2D(32.f, 32.f), IconTint);
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(OutIcon))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
			}
			OutText = MakeText(TextName, 25, true, DinoOcean::Text());
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(OutText))
			{
				S->SetVerticalAlignment(VAlign_Center);
			}
			if (UVerticalBoxSlot* S = NameCol->AddChildToVerticalBox(Pill))
			{
				S->SetHorizontalAlignment(HAlign_Right);
				S->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			}
		};
		MakePill(TEXT("OceanDietIcon"), Outline(), Accent(), DietIconImage, DietTagText, TEXT("OceanDietTag"));
		MakePill(TEXT("OceanPeriodIcon"), SRGB(255, 180, 77, 170), Orange(), PeriodIconImage, PeriodTagText, TEXT("OceanPeriodTag"));
		// 시대 배지 아랫줄(약 6,800만 년 전)은 이 시안에 없다. 접어 둔다.
		PeriodSubText = nullptr;
		NumberText = nullptr;
		FullscreenButton = nullptr;
	}

	// ---- 탭 바
	{
		UBorder* Frame = Tree->ConstructWidget<UBorder>();
		Frame->SetBrush(GlassBrush(BarRadius));
		Frame->SetBrushColor(FLinearColor::White);
		Frame->SetPadding(FMargin(6.f, 0.f, 6.f, 0.f));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Frame))
		{
			S->SetPadding(FMargin(SideInset, 14.f, SideInset, 0.f));
			S->SetHorizontalAlignment(HAlign_Fill);
		}
		UHorizontalBox* Bar = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OceanTabBar"));
		Frame->SetContent(Bar);
		TabBar = Bar;
	}

	// ---- 본문 패널: 남는 세로 공간을 히어로와 나눠 갖는다. 글(제목·본문)만 스크롤하고
	// 요약 타일은 패널 아래에 고정한다 — 짧은 화면에서도 타일이 보이고, 긴 화면에서는
	// 시안처럼 타일이 패널 바닥 쪽에 앉는다.
	{
		UBorder* Panel = Tree->ConstructWidget<UBorder>();
		Panel->SetBrush(GlassBrush(BarRadius, 0.72f));
		Panel->SetBrushColor(FLinearColor::White);
		Panel->SetPadding(FMargin(34.f, 30.f, 34.f, 26.f));
		Panel->SetClipping(EWidgetClipping::ClipToBounds);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Panel))
		{
			FSlateChildSize PanelSize(ESlateSizeRule::Fill);
			PanelSize.Value = 0.55f;
			S->SetSize(PanelSize);
			S->SetPadding(FMargin(SideInset, 18.f, SideInset, 0.f));
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}

		UVerticalBox* PanelCol = Tree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(PanelCol);
		if (UBorderSlot* S = Cast<UBorderSlot>(PanelCol->Slot))
		{
			S->SetPadding(FMargin(0.f));
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}

		UOverlay* TextArea = Tree->ConstructWidget<UOverlay>();
		if (UVerticalBoxSlot* S = PanelCol->AddChildToVerticalBox(TextArea))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}

		// 삽화: 오른쪽 위, 은은하게. 텍스트가 그 위를 지나지 않게 ApplyTabContent 가 여백을 준다.
		TabImage = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OceanTabImage"));
		SetOceanImage(TabImage, nullptr, FVector2D(330.f, 300.f), FLinearColor(1.f, 1.f, 1.f, 0.55f));
		if (UOverlaySlot* S = TextArea->AddChildToOverlay(TabImage))
		{
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetVerticalAlignment(VAlign_Top);
			S->SetPadding(FMargin(0.f, -20.f, -20.f, 0.f));
		}

		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("OceanContentScroll"));
		Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
		if (UOverlaySlot* S = TextArea->AddChildToOverlay(Scroll))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
		ContentScroll = Scroll;
		OceanTextCenter = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OceanTextCenter"));
		Scroll->AddChild(OceanTextCenter);
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>();
		OceanTextCenter->SetContent(Content);
		if (USizeBoxSlot* S = Cast<USizeBoxSlot>(Content->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Center);
		}

		UImage* Dash = Tree->ConstructWidget<UImage>();
		Dash->SetBrush(FSlateRoundedBoxBrush(Accent(), 3.f));
		Dash->SetDesiredSizeOverride(FVector2D(44.f, 6.f));
		Dash->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Dash))
		{
			S->SetHorizontalAlignment(HAlign_Left);
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		}

		TabHeading = MakeText(TEXT("OceanTabHeading"), 36, true, DinoOcean::Text());
		TabHeading->SetAutoWrapText(true);
		if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(TabHeading))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		}

		IntroLabel = MakeText(TEXT("OceanIntroLabel"), 24, false, DinoOcean::Text());
		IntroLabel->SetAutoWrapText(true);
		IntroLabel->SetLineHeightPercentage(1.4f);
		if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(IntroLabel))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 26.f));
		}

		UHorizontalBox* Stats = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OceanStatBox"));
		if (UVerticalBoxSlot* S = PanelCol->AddChildToVerticalBox(Stats))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Bottom);
			S->SetPadding(FMargin(-6.f, 8.f, -6.f, 0.f));   // 타일 슬롯 좌우 6 을 상쇄해 패널 여백에 맞춘다
		}
		StatBox = Stats;
	}

	// ---- CTA: 렉시 + 두 줄 문구 + 화살표. 버튼 전체가 눌린다.
	{
		AskDocentButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OceanAskDocentButton"));
		StyleGlassButton(AskDocentButton, BarRadius);
		DocentCta = AskDocentButton;
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(AskDocentButton))
		{
			S->SetPadding(FMargin(SideInset, 22.f, SideInset, 22.f));
			S->SetHorizontalAlignment(HAlign_Fill);
		}

		USizeBox* CtaSize = Tree->ConstructWidget<USizeBox>();
		CtaSize->SetHeightOverride(CtaHeight);
		AskDocentButton->SetContent(CtaSize);
		if (UButtonSlot* S = Cast<UButtonSlot>(CtaSize->Slot))
		{
			S->SetPadding(FMargin(0.f));
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}

		UOverlay* Cta = Tree->ConstructWidget<UOverlay>();
		CtaSize->SetContent(Cta);

		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		if (UOverlaySlot* S = Cta->AddChildToOverlay(Row))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
			S->SetPadding(FMargin(250.f, 0.f, 30.f, 0.f));
		}
		UVerticalBox* Lines = Tree->ConstructWidget<UVerticalBox>();
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Lines))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* CtaTitle = MakeText(TEXT("OceanCtaTitle"), 33, true, DinoOcean::Text());
		CtaTitle->SetText(FText::FromString(TEXT("렉시에게 물어보세요!")));
		if (UVerticalBoxSlot* S = Lines->AddChildToVerticalBox(CtaTitle))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		}
		OceanCtaPrompt = MakeText(TEXT("OceanCtaPrompt"), 23, false, TextDim());
		OceanCtaPrompt->SetAutoWrapText(true);
		Lines->AddChildToVerticalBox(OceanCtaPrompt);

		UImage* Chevron = Tree->ConstructWidget<UImage>();
		SetOceanImage(Chevron, LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/DinoCard/Icons/chevron_right.chevron_right")),
			FVector2D(48.f, 48.f), DinoOcean::Text());
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Chevron))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		}

		// 렉시는 CTA 위로 살짝 솟는다(음수 여백). 버튼은 자식을 자르지 않는다.
		UImage* Lexi = Tree->ConstructWidget<UImage>();
		SetOceanImage(Lexi, Tex(TEXT("T_RexyCTA")), FVector2D(214.f, 207.f), FLinearColor::White);
		if (UOverlaySlot* S = Cta->AddChildToOverlay(Lexi))
		{
			S->SetHorizontalAlignment(HAlign_Left);
			S->SetVerticalAlignment(VAlign_Bottom);
			S->SetPadding(FMargin(18.f, 0.f, 0.f, -4.f));
		}
	}

	// ---- 메뉴 팝업: 어두운 막 + 오른쪽 위 목록. 무반응 버튼을 남기지 않기 위한 최소 메뉴.
	{
		UOverlay* Popup = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("OceanMenuPopup"));
		Popup->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* S = Root->AddChildToOverlay(Popup))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
		OceanMenuPopup = Popup;

		OceanMenuBackdrop = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OceanMenuBackdrop"));
		{
			FButtonStyle Style = OceanMenuBackdrop->GetStyle();
			const FSlateRoundedBoxBrush Dim(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f);
			Style.SetNormal(Dim); Style.SetHovered(Dim); Style.SetPressed(Dim);
			Style.SetNormalPadding(FMargin(0.f));
			Style.SetPressedPadding(FMargin(0.f));
			OceanMenuBackdrop->SetStyle(Style);
		}
		if (UOverlaySlot* S = Popup->AddChildToOverlay(OceanMenuBackdrop))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
		OceanMenuBackdrop->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleMenuBackdropClicked);

		USafeZone* MenuSafe = Tree->ConstructWidget<USafeZone>();
		if (UOverlaySlot* S = Popup->AddChildToOverlay(MenuSafe))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
		UBorder* Sheet = Tree->ConstructWidget<UBorder>();
		Sheet->SetBrush(GlassBrush(30.f, 0.96f));
		Sheet->SetBrushColor(FLinearColor::White);
		Sheet->SetPadding(FMargin(12.f));
		MenuSafe->SetContent(Sheet);
		if (USafeZoneSlot* S = Cast<USafeZoneSlot>(Sheet->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetVerticalAlignment(VAlign_Top);
			S->SetPadding(FMargin(0.f, 22.f + RoundButton + 14.f, SideInset, 0.f));
		}
		UVerticalBox* Items = Tree->ConstructWidget<UVerticalBox>();
		Sheet->SetContent(Items);

		auto MakeItem = [&](const TCHAR* Label) -> UButton*
		{
			UButton* B = Tree->ConstructWidget<UButton>();
			StyleTransparentButton(B);
			UTextBlock* T = MakeText(NAME_None, 26, false, DinoOcean::Text());
			T->SetText(FText::FromString(Label));
			B->SetContent(T);
			if (UButtonSlot* S = Cast<UButtonSlot>(T->Slot))
			{
				S->SetPadding(FMargin(28.f, 20.f, 40.f, 20.f));
				S->SetHorizontalAlignment(HAlign_Left);
			}
			Items->AddChildToVerticalBox(B);
			OceanMenuItems.Add(B);
			return B;
		};
		MakeItem(TEXT("AR 화면으로 돌아가기"))->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleCloseClicked);
		MakeItem(TEXT("렉시에게 물어보기"))->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleAskDocentClicked);
		MakeItem(TEXT("내비게이션 열기"))->OnClicked.AddUniqueDynamic(this, &UDinoInfoCardWidget::HandleNavClicked);
	}

	StatTileClass = UDinoOceanStatTile::StaticClass();
	TabButtonClass = UDinoOceanTabButton::StaticClass();
	SetIsFocusable(true);
}

void UDinoInfoCardWidget::ApplyOceanInfo()
{
	if (CurrentInfo == nullptr || OceanRoot == nullptr)
	{
		return;
	}

	if (OceanBackground != nullptr)
	{
		OceanBackground->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.015f, 0.035f, 0.06f, 0.38f), 0.f));
		OceanBackground->SetColorAndOpacity(FLinearColor::White);
		OceanBackground->SetVisibility(ESlateVisibility::Visible);
	}

	// 히어로: 텍스처 비율을 유지한 채 폭 780 에 맞춘다. 얼굴·지느러미가 잘리지 않게 왼쪽 정렬.
	if (HeroImage != nullptr && CurrentInfo->HeroImage != nullptr)
	{
		const float W = 780.f;
		const float H = W * FMath::Max(1.f, (float)CurrentInfo->HeroImage->GetSizeY()) / FMath::Max(1.f, (float)CurrentInfo->HeroImage->GetSizeX());
		FSlateBrush Brush;
		Brush.SetResourceObject(CurrentInfo->HeroImage);
		Brush.ImageSize = FVector2D(W, H);
		HeroImage->SetBrush(Brush);
		HeroImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// 태그 아이콘: DA 값이 없으면 스킨 기본(물결·산).
	auto FallbackIcon = [](UImage* Image, UTexture2D* FromData, const TCHAR* Default)
	{
		if (Image == nullptr || FromData != nullptr)
		{
			return;
		}
		if (UTexture2D* Tex = DinoOcean::Tex(Default))
		{
			FSlateBrush B = Image->GetBrush();
			B.SetResourceObject(Tex);
			B.DrawAs = ESlateBrushDrawType::Image;
			Image->SetBrush(B);
			Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	};
	FallbackIcon(DietIconImage, CurrentInfo->DietIcon, TEXT("T_IcoMarine"));
	FallbackIcon(PeriodIconImage, CurrentInfo->PeriodIcon, TEXT("T_IcoEra"));

	if (OceanZoneName != nullptr)
	{
		const FText Zone = CurrentInfo->ZoneName.IsEmpty()
			? FText::FromString(CurrentInfo->NameKo.ToString() + TEXT(" 전시존"))
			: CurrentInfo->ZoneName;
		SetTextOrCollapse(OceanZoneName, Zone);
	}
	SetTextOrCollapse(OceanZoneSub, CurrentInfo->ZoneSub);
}

void UDinoInfoCardWidget::HandleMenuClicked()
{
	if (OceanMenuPopup != nullptr)
	{
		const bool bShown = OceanMenuPopup->GetVisibility() != ESlateVisibility::Collapsed;
		OceanMenuPopup->SetVisibility(bShown ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	FocusCard();
}

void UDinoInfoCardWidget::HandleMenuBackdropClicked()
{
	if (OceanMenuPopup != nullptr)
	{
		OceanMenuPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
	FocusCard();
}

UDocentChatWidget* UDinoInfoCardWidget::FindDocentChat() const
{
	UWorld* World = GetWorld();
	for (TObjectIterator<UDocentChatWidget> It; It; ++It)
	{
		UDocentChatWidget* Chat = *It;
		if (Chat != nullptr && !Chat->HasAnyFlags(RF_ClassDefaultObject) && Chat->GetWorld() == World
			&& (Chat->IsInViewport() || Chat->GetParent() != nullptr))
		{
			return Chat;
		}
	}
	return nullptr;
}

void UDinoInfoCardWidget::HandleNavClicked()
{
	// 새 길찾기 화면을 만들지 않는다. 카드를 닫고 AR 화면 하단 바의 내비게이션 버튼을
	// 대신 눌러 기존 지도 화면을 연다(그 버튼의 BP/C++ 바인딩이 그대로 돈다).
	HideCard();
	if (UDocentChatWidget* Chat = FindDocentChat())
	{
		if (UButton* Nav = Cast<UButton>(Chat->GetWidgetFromName(TEXT("NabButton"))))
		{
			Nav->OnClicked.Broadcast();
			return;
		}
	}
	UE_LOG(LogDinoCard, Warning, TEXT("[card] 내비게이션 버튼(NabButton)을 찾지 못해 카드만 닫습니다."));
}

void UDinoInfoCardWidget::HandleScanClicked()
{
	// 이 화면 아래가 곧 스캔 화면(인식된 모델 포함)이다. 세션·모델을 새로 만들지 않는다.
	HideCard();
}

void UDinoInfoCardWidget::HandleDocentOpenStateChanged(bool bIsChatOpen)
{
	if (bIsChatOpen || !bReopenAfterDocent)
	{
		return;
	}
	bReopenAfterDocent = false;

	UDinoInfoData* Info = ReopenInfo;
	const int32 Tab = ReopenTabIndex;
	ReopenInfo = nullptr;
	if (Info == nullptr)
	{
		return;
	}
	UE_LOG(LogDinoCard, Log, TEXT("[card] 도슨트에서 복귀: %s 탭 %d"), *Info->NameKo.ToString(), Tab);
	ShowFor(Info);
	SelectTab(Tab);
}

void UDinoInfoCardWidget::FocusCard()
{
	if (ActiveSkin != EDinoCardSkin::Ocean || !bIsOpen)
	{
		return;
	}
	// 방금 뷰포트에 다시 붙었으면 아직 창 경로가 없다. 다음 틱에 잡는다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (bIsOpen && ActiveSkin == EDinoCardSkin::Ocean)
			{
				SetFocus();
			}
		}));
	}
}

FReply UDinoInfoCardWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 안드로이드 뒤로 키: 메뉴가 열려 있으면 메뉴만, 아니면 카드를 닫는다. 카드가 닫힌 뒤에는
	// 포커스도 놓아 다음 뒤로 키가 평소대로 흐르게 한다.
	if (bIsOpen && ActiveSkin == EDinoCardSkin::Ocean && InKeyEvent.GetKey() == EKeys::Android_Back)
	{
		if (OceanMenuPopup != nullptr && OceanMenuPopup->GetVisibility() != ESlateVisibility::Collapsed)
		{
			OceanMenuPopup->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			HideCard();
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
			}
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
