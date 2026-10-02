#include "DocentChatBubble.h"

#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
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
		Bg->SetPadding(FMargin(30.f, 22.f, 30.f, 20.f));

		// 시각은 말풍선 바깥, 아래 모서리 옆에 작게. 런타임에는 패널의 자식 순서를
		// 바꿀 수 없어(InsertChildAt 은 에디터 전용) BubbleBg 를 감싼 단일 자식 위젯
		// (SizeBox 등)의 내용을 [말풍선, 시각] 가로 상자로 갈아 끼운다.
		if (!bSkinApplied)
		{
			bSkinApplied = true;

			if (UContentWidget* Holder = Cast<UContentWidget>(Bg->GetParent()))
			{
				const FDateTime Now = FDateTime::Now();
				const int32 Hour12 = (Now.GetHour() % 12 == 0) ? 12 : Now.GetHour() % 12;
				const FString Stamp = FString::Printf(TEXT("%s %d:%02d"),
					Now.GetHour() < 12 ? TEXT("오전") : TEXT("오후"), Hour12, Now.GetMinute());

				UTextBlock* Time = WidgetTree->ConstructWidget<UTextBlock>();
				Time->SetText(FText::FromString(Stamp));
				FSlateFontInfo TimeFont = Time->GetFont();
				TimeFont.Size = 20;
				Time->SetFont(TimeFont);
				Time->SetColorAndOpacity(FSlateColor(TextDim));
				Time->SetVisibility(ESlateVisibility::HitTestInvisible);

				UHorizontalBox* Pair = WidgetTree->ConstructWidget<UHorizontalBox>();
				Bg->RemoveFromParent();
				if (bIsUser)
				{
					Pair->AddChild(Time);
					Pair->AddChild(Bg);
				}
				else
				{
					Pair->AddChild(Bg);
					Pair->AddChild(Time);
				}
				if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Time->Slot))
				{
					S->SetVerticalAlignment(VAlign_Bottom);
					S->SetPadding(bIsUser ? FMargin(0.f, 0.f, 14.f, 6.f) : FMargin(14.f, 0.f, 0.f, 6.f));
				}
				if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Bg->Slot))
				{
					S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				}
				Holder->SetContent(Pair);
			}
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
