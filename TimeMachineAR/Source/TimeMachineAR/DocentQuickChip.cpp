#include "DocentQuickChip.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UDocentQuickChip::SetQuestion(const FString& InQuestion)
{
	Question = InQuestion;

	if (ChipText != nullptr)
	{
		ChipText->SetText(FText::FromString(Question));
	}
}

void UDocentQuickChip::NativeConstruct()
{
	Super::NativeConstruct();

	if (ChipButton != nullptr)
	{
		// SetQuestion 이 NativeConstruct 보다 먼저 불릴 수 있어 여기서 한 번 더 맞춘다.
		ChipButton->OnClicked.AddUniqueDynamic(this, &UDocentQuickChip::HandleButtonClicked);
	}

	if (ChipText != nullptr && !Question.IsEmpty())
	{
		ChipText->SetText(FText::FromString(Question));
	}
}

void UDocentQuickChip::NativeDestruct()
{
	if (ChipButton != nullptr)
	{
		ChipButton->OnClicked.RemoveDynamic(this, &UDocentQuickChip::HandleButtonClicked);
	}

	Super::NativeDestruct();
}

void UDocentQuickChip::HandleButtonClicked()
{
	OnClicked.ExecuteIfBound(Question);
}
