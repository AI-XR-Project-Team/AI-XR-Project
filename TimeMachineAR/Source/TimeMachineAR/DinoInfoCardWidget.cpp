#include "DinoInfoCardWidget.h"

#include "DinoInfoData.h"
#include "DinoStatTile.h"
#include "DinoTabButton.h"
#include "Components/Button.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
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
	if (CardPanel != nullptr)
	{
		CardPanel->SetVisibility(ESlateVisibility::Visible);
	}

	UE_LOG(LogDinoCard, Log, TEXT("[card] 열기: %s (exhibit=%s)"),
		*CurrentInfo->NameKo.ToString(),
		CurrentInfo->ExhibitId.IsEmpty() ? TEXT("(미설정)") : *CurrentInfo->ExhibitId);
}

void UDinoInfoCardWidget::HideCard()
{
	bIsOpen = false;

	if (CardPanel != nullptr)
	{
		// Collapsed 로 접어야 뒤쪽 AR 화면의 터치를 가로채지 않는다.
		CardPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
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

	// 탭을 먼저 정하고 버튼을 만든다. RebuildTabs 가 첫 탭을 골라 본문까지 채운다.
	ResolveTabs();
	RebuildTabs();

	RebuildStats();

	if (AskDocentButton != nullptr)
	{
		// 전시물 UUID 가 없으면 눌러도 대화를 열 수 없다. 버튼을 남겨 두면
		// 눌리기만 하고 아무 일도 안 일어나 고장으로 보인다.
		const bool bCanAsk = !CurrentInfo->ExhibitId.IsEmpty();
		AskDocentButton->SetVisibility(bCanAsk
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
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

					Button->Setup(Index, ResolvedTabs[Index].Label);
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
		if (TabImage != nullptr)
		{
			TabImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	const FDinoTab& Tab = ResolvedTabs[SelectedTabIndex];

	SetTextOrCollapse(TabHeading, Tab.Heading);
	SetTextOrCollapse(IntroLabel, Tab.Body);

	if (TabImage != nullptr)
	{
		if (Tab.Image != nullptr)
		{
			TabImage->SetBrushFromTexture(Tab.Image);
			TabImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			// 사진 없는 탭에서 이미지 칸을 남기면 빈 사각형이 그대로 보인다.
			TabImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
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
		StatBox->AddChild(Tile);
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
