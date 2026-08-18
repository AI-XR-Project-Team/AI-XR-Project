#include "DinoTabButton.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UDinoTabButton::Setup(int32 InIndex, const FText& InLabel, UTexture2D* InIcon)
{
	TabIndex = InIndex;
	Label = InLabel;
	Icon = InIcon;
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

	// 아이콘은 선택 상태를 따라간다. Apply 가 그 판단을 들고 있으므로 다시 부른다.
	Apply();

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
		// 시안: 선택된 탭만 흰색, 나머지는 한 톤 죽인 회색. WBP 색을 덮어쓰지만
		// 탭 버튼은 이 두 상태뿐이라 디자인 자유도를 잃을 게 없다.
		TabLabel->SetColorAndOpacity(FSlateColor(bSelected
			? FLinearColor::White
			: FLinearColor(0.22f, 0.24f, 0.30f, 1.f)));
	}

	if (TabIcon != nullptr)
	{
		// 레퍼런스는 선택된 탭에만 아이콘을 둔다. 네 탭에 전부 아이콘을 달면
		// 글자가 밀려 좁아지고, 어느 탭이 켜졌는지도 밑줄로만 구분해야 한다.
		// Collapsed 로 접어야 남은 폭을 글자가 도로 가져가 가운데 정렬이 산다.
		const bool bShowIcon = bSelected && Icon != nullptr;
		if (bShowIcon)
		{
			TabIcon->SetBrushFromTexture(Icon);
			TabIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			TabIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UDinoTabButton::HandleButtonClicked()
{
	OnClicked.ExecuteIfBound(TabIndex);
}
