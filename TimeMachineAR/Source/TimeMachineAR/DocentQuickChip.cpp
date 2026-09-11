#include "DocentQuickChip.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"

void UDocentQuickChip::SetQuestion(const FString& InQuestion)
{
	Question = InQuestion;

	if (ChipText != nullptr)
	{
		ChipText->SetText(FText::FromString(Question));
	}
}

void UDocentQuickChip::SetTopic(const FString& InQuestion, const FString& InTitle, const FString& InSubtitle, FName InIconName, float InRowWidth)
{
	RowWidth = InRowWidth;
	Question = InQuestion;
	Title = InTitle;
	Subtitle = InSubtitle;
	IconName = InIconName;

	if (ChipText != nullptr)
	{
		ChipText->SetText(FText::FromString(Title));
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
		ChipText->SetText(FText::FromString(Title.IsEmpty() ? Question : Title));
	}

	ApplySkin();
}

void UDocentQuickChip::ApplySkin()
{
	if (WidgetTree == nullptr || ChipButton == nullptr || ChipText == nullptr)
	{
		return;
	}
	if (bSkinApplied)
	{
		return;
	}
	bSkinApplied = true;

	const FLinearColor Glass   = FLinearColor::FromSRGBColor(FColor(22, 27, 36, 215));
	const FLinearColor GlassHi = FLinearColor::FromSRGBColor(FColor(38, 45, 58, 235));
	const FLinearColor Outline = FLinearColor(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor TextMain = FLinearColor::FromSRGBColor(FColor(240, 244, 250));

	// 목업의 추천 질문은 칩이 아니라 전체 폭의 행이다. 유리 캡슐 + 왼쪽 아이콘 + 오른쪽 >.
	FButtonStyle Style = ChipButton->GetStyle();
	Style.SetNormal (FSlateRoundedBoxBrush(Glass,   28.f, Outline, 2.f));
	Style.SetHovered(FSlateRoundedBoxBrush(GlassHi, 28.f, Outline, 2.f));
	Style.SetPressed(FSlateRoundedBoxBrush(GlassHi, 28.f, Outline, 2.f));
	Style.SetNormalPadding(FMargin(0.f));
	Style.SetPressedPadding(FMargin(0.f));
	ChipButton->SetStyle(Style);

	// 질문 내용으로 아이콘을 고른다. 시트의 아이콘은 거의 검정이라 어두운 배경에서
	// 안 보여, DinoCard 의 흰 Material 아이콘을 쓴다.
	FString IconAsset = IconName.IsNone() ? FString(TEXT("label")) : IconName.ToString();
	if (IconName.IsNone())
	{
		if (Question.Contains(TEXT("설명")))               { IconAsset = TEXT("menu_book"); }
		else if (Question.Contains(TEXT("식성")))          { IconAsset = TEXT("dentistry"); }
		else if (Question.Contains(TEXT("크기")))          { IconAsset = TEXT("straighten"); }
		else if (Question.Contains(TEXT("사실")) || Question.Contains(TEXT("놀라운"))) { IconAsset = TEXT("travel_explore"); }
	}
	const bool bTwoLine = !Subtitle.IsEmpty();

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

	if (UTexture2D* IconTex = LoadObject<UTexture2D>(nullptr,
		*FString::Printf(TEXT("/Game/UI/DinoCard/Icons/%s.%s"), *IconAsset, *IconAsset)))
	{
		UImage* Icon = WidgetTree->ConstructWidget<UImage>();
		Icon->SetBrushFromTexture(IconTex);
		Icon->SetDesiredSizeOverride(bTwoLine ? FVector2D(72.f, 72.f) : FVector2D(64.f, 64.f));
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(Icon)))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(34.f, 0.f, 26.f, 0.f));
		}
	}

	ChipText->RemoveFromParent();
	{
		FSlateFontInfo Font = ChipText->GetFont();
		Font.Size = 30;
		ChipText->SetFont(Font);
		ChipText->SetColorAndOpacity(FSlateColor(TextMain));
		ChipText->SetJustification(ETextJustify::Left);
	}
	UWidget* Label = ChipText;
	if (bTwoLine)
	{
		// 주제 행: 제목 아래 흐린 부제. 목업의 "기본 정보 / 시대, 크기, 특징" 꼴.
		UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
		Lines->AddChild(ChipText);
		UTextBlock* Sub = WidgetTree->ConstructWidget<UTextBlock>();
		Sub->SetText(FText::FromString(Subtitle));
		FSlateFontInfo SubFont = ChipText->GetFont();
		SubFont.Size = 24;
		Sub->SetFont(SubFont);
		Sub->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(FColor(160, 170, 185))));
		if (UVerticalBoxSlot* SS = Cast<UVerticalBoxSlot>(Lines->AddChild(Sub)))
		{
			SS->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}
		Label = Lines;
	}
	if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	if (UTexture2D* Chev = LoadObject<UTexture2D>(nullptr,
		TEXT("/Game/UI/DinoCard/Icons/chevron_right.chevron_right")))
	{
		UImage* Arrow = WidgetTree->ConstructWidget<UImage>();
		Arrow->SetBrushFromTexture(Chev);
		Arrow->SetDesiredSizeOverride(FVector2D(36.f, 36.f));
		Arrow->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.6f));
		Arrow->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(Arrow)))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 30.f, 0.f));
		}
	}

	// WrapBox 안에서 한 줄에 하나만 오도록 폭을 못 박는다. 1080 UMG 폭에서 좌우 여백을 뺀 값.
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(RowWidth);
	Size->SetHeightOverride(bTwoLine ? 176.f : 126.f);
	Size->SetContent(Row);
	ChipButton->SetContent(Size);
	if (UButtonSlot* BS = Cast<UButtonSlot>(Size->Slot))
	{
		BS->SetPadding(FMargin(0.f));
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
