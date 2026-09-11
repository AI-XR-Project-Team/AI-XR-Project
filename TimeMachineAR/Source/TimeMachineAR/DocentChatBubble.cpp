#include "DocentChatBubble.h"

#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
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

		// 시각은 말풍선 바깥, 아래 모서리 옆에 작게. 말풍선을 담은 HorizontalBox 를
		// 위로 거슬러 찾아, 말풍선 쪽 자식 바로 옆(도슨트는 오른쪽, 관람객은 왼쪽)에 끼운다.
		if (!bSkinApplied)
		{
			bSkinApplied = true;

			UWidget* Anchor = Bg;
			UHorizontalBox* Row = nullptr;
			while (Anchor != nullptr)
			{
				UPanelWidget* Parent = Anchor->GetParent();
				Row = Cast<UHorizontalBox>(Parent);
				if (Row != nullptr)
				{
					break;
				}
				Anchor = Parent;
			}

			if (Row != nullptr)
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

				// 말풍선 슬롯의 아래 여백만큼 같이 띄워야 시각이 말풍선 아래 모서리 옆에 선다.
				float Lift = 6.f;
				if (const UHorizontalBoxSlot* AnchorSlot = Cast<UHorizontalBoxSlot>(Anchor->Slot))
				{
					Lift += AnchorSlot->GetPadding().Bottom;
				}
				const int32 Index = Row->GetChildIndex(Anchor);
				Row->InsertChildAt(bIsUser ? Index : Index + 1, Time);
				if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Time->Slot))
				{
					S->SetVerticalAlignment(VAlign_Bottom);
					S->SetPadding(bIsUser ? FMargin(0.f, 0.f, 14.f, Lift) : FMargin(14.f, 0.f, 0.f, Lift));
				}
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
