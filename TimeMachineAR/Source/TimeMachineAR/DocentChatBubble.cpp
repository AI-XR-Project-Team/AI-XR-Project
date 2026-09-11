#include "DocentChatBubble.h"

#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Spacer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Misc/DateTime.h"

void UDocentChatBubble::Setup(bool bInIsUser, const FString& InText)
{
	bIsUser = bInIsUser;
	Body = InText;

	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Body));
	}

	OnRoleApplied(bIsUser);
	ApplySkin();
}

void UDocentChatBubble::AppendText(const FString& InText)
{
	if (InText.IsEmpty())
	{
		return;
	}

	// FText 를 매번 문자열로 되돌리지 않고 Body 를 기준으로 삼는다. 델타가
	// 수십 번 오는 경로라 왕복 변환 비용이 쌓인다.
	Body += InText;

	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Body));
	}
}

void UDocentChatBubble::SetText(const FString& InText)
{
	Body = InText;

	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Body));
	}
}

void UDocentChatBubble::ApplySkin()
{
	if (WidgetTree == nullptr || MessageText == nullptr)
	{
		return;
	}

	// 목업 색. 도슨트는 어두운 유리, 관람객은 채도 있는 파랑.
	const FLinearColor AiFill   = FLinearColor::FromSRGBColor(FColor(38, 47, 64, 235));
	const FLinearColor UserFill = FLinearColor::FromSRGBColor(FColor(43, 95, 168, 235));
	const FLinearColor Outline  = FLinearColor(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor TextMain = FLinearColor::FromSRGBColor(FColor(240, 244, 250));
	const FLinearColor TextDim  = FLinearColor::FromSRGBColor(FColor(150, 160, 178));

	// 본문
	{
		FSlateFontInfo Font = MessageText->GetFont();
		Font.Size = 30;
		MessageText->SetFont(Font);
		MessageText->SetColorAndOpacity(FSlateColor(TextMain));
		MessageText->SetLineHeightPercentage(1.3f);
	}

	// 말풍선 배경. 꼬리는 생략한다 - 둥근 상자만으로도 목업의 인상은 나온다.
	if (UBorder* Bg = Cast<UBorder>(GetWidgetFromName(TEXT("BubbleBg"))))
	{
		FSlateBrush Brush = FSlateRoundedBoxBrush(bIsUser ? UserFill : AiFill, 26.f, Outline, 2.f);
		Bg->SetBrush(Brush);
		Bg->SetPadding(FMargin(30.f, 22.f, 30.f, 16.f));

		// 시각 텍스트가 아직 없으면 본문 아래에 넣는다. 스트리밍으로 Setup 이 여러 번
		// 불려도 한 번만 감싸도록 VerticalBox 존재 여부로 가른다.
		if (Cast<UVerticalBox>(Bg->GetContent()) == nullptr)
		{
			UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
			MessageText->RemoveFromParent();
			Column->AddChild(MessageText);

			const FDateTime Now = FDateTime::Now();
			const int32 Hour12 = (Now.GetHour() % 12 == 0) ? 12 : Now.GetHour() % 12;
			const FString Stamp = FString::Printf(TEXT("%s %d:%02d"),
				Now.GetHour() < 12 ? TEXT("오전") : TEXT("오후"), Hour12, Now.GetMinute());

			UTextBlock* Time = WidgetTree->ConstructWidget<UTextBlock>();
			Time->SetText(FText::FromString(Stamp));
			FSlateFontInfo TimeFont = Time->GetFont();
			TimeFont.Size = 22;
			Time->SetFont(TimeFont);
			Time->SetColorAndOpacity(FSlateColor(bIsUser ? FLinearColor(1.f, 1.f, 1.f, 0.7f) : TextDim));
			Time->SetJustification(bIsUser ? ETextJustify::Right : ETextJustify::Left);
			if (UVerticalBoxSlot* TimeSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Time)))
			{
				TimeSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
				TimeSlot->SetHorizontalAlignment(bIsUser ? HAlign_Right : HAlign_Left);
			}
			Bg->SetContent(Column);
		}
	}

	// 아바타: 도슨트만 왼쪽에 작은 렉시.
	if (UImage* Avatar = Cast<UImage>(GetWidgetFromName(TEXT("Avatar"))))
	{
		if (bIsUser)
		{
			Avatar->SetVisibility(ESlateVisibility::Collapsed);
		}
		else if (UTexture2D* Face = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/UI/Docent/Skin/lexi_avatar.lexi_avatar")))
		{
			Avatar->SetBrushFromTexture(Face);
			Avatar->SetDesiredSizeOverride(FVector2D(104.f, 118.f));
			Avatar->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}
