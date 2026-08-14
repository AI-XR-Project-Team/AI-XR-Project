#include "DinoInfoCardWidget.h"

#include "DinoInfoData.h"
#include "DinoStatTile.h"
#include "Components/Button.h"
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
	SetTextOrCollapse(IntroLabel, CurrentInfo->IntroText);

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
