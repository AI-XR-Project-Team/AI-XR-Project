#include "DinoInfoCardWidget.h"

#include "DinoInfoData.h"
#include "DinoStatTile.h"
#include "DinoTabButton.h"
#include "Components/Button.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogDinoCard, Log, All);

void UDinoInfoCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

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
	ApplyInfo();

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
	if (!bElevatedZ && IsInViewport())
	{
		bElevatedZ = true;
		RemoveFromParent();
		AddToViewport(100);
	}

	UE_LOG(LogDinoCard, Log, TEXT("[card] 열기: %s (exhibit=%s)"),
		*CurrentInfo->NameKo.ToString(),
		CurrentInfo->ExhibitId.IsEmpty() ? TEXT("(미설정)") : *CurrentInfo->ExhibitId);
}

void UDinoInfoCardWidget::HideCard()
{
	bIsOpen = false;

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

	// 탭을 먼저 정하고 버튼을 만든다. RebuildTabs 가 첫 탭을 골라 본문까지 채운다.
	ResolveTabs();
	RebuildTabs();

	RebuildStats();

	// 전시물 UUID 가 없으면 도슨트에 물어볼 수가 없다. 버튼만 숨기면 마스코트와
	// "물어보세요" 문구만 남아 더 어색하므로, DocentCta 가 지정돼 있으면 그 줄을
	// 통째로 접는다. 지정 안 된 예전 구성에서는 버튼만 접는다.
	const bool bCanAsk = !CurrentInfo->ExhibitId.IsEmpty();
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
		return;
	}

	const FDinoTab& Tab = ResolvedTabs[SelectedTabIndex];

	SetTextOrCollapse(TabHeading, Tab.Heading);
	SetTextOrCollapse(IntroLabel, Tab.Body);
	SetImageOrCollapse(TabImage, Tab.Image);
}

void UDinoInfoCardWidget::HandleTabClicked(int32 TabIndex)
{
	SelectTab(TabIndex);
}

void UDinoInfoCardWidget::RebuildStats()
{
	if (StatBox == nullptr)
	{
		return;
	}

	StatBox->ClearChildren();

	if (StatTileClass == nullptr)
	{
		if (CurrentInfo != nullptr && CurrentInfo->Stats.Num() > 0)
		{
			UE_LOG(LogDinoCard, Warning,
				TEXT("StatTileClass 가 비어 요약 타일을 만들 수 없습니다. "
					 "카드 WBP 의 클래스 기본값에서 지정하세요."));
		}
		return;
	}

	for (const FDinoStat& Stat : CurrentInfo->Stats)
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
	if (CurrentInfo == nullptr || CurrentInfo->ExhibitId.IsEmpty())
	{
		return;
	}

	OnAskDocentClicked.Broadcast(CurrentInfo->ExhibitId);
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
