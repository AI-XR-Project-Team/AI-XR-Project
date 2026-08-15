#include "DinoTabButton.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UDinoTabButton::Setup(int32 InIndex, const FText& InLabel)
{
	TabIndex = InIndex;
	Label = InLabel;
	Apply();
}

void UDinoTabButton::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;

	if (SelectedMark != nullptr)
	{
		// Collapsed 가 아니라 Hidden 이다. 밑줄이 접히면 그 탭만 높이가 줄어
		// 글자가 아래로 내려앉고, 탭 글자들이 같은 높이에 안 선다.
		SelectedMark->SetVisibility(bSelected
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}

	OnSelectionChanged(bSelected);
}

void UDinoTabButton::NativeConstruct()
{
	Super::NativeConstruct();

	if (TabButton != nullptr)
	{
		TabButton->OnClicked.AddUniqueDynamic(this, &UDinoTabButton::HandleButtonClicked);
	}

	Apply();
	SetSelected(bSelected);
}

void UDinoTabButton::NativeDestruct()
{
	if (TabButton != nullptr)
	{
		TabButton->OnClicked.RemoveDynamic(this, &UDinoTabButton::HandleButtonClicked);
	}

	Super::NativeDestruct();
}

void UDinoTabButton::Apply()
{
	if (TabLabel != nullptr)
	{
		TabLabel->SetText(Label);
	}
}

void UDinoTabButton::HandleButtonClicked()
{
	OnClicked.ExecuteIfBound(TabIndex);
}
