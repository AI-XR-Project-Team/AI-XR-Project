// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavCloudAdminOverlay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/EditableTextBox.h"

namespace
{
	/** 라벨 텍스트를 가진 버튼 하나를 만든다. */
	UButton* MakeLabeledButton(UWidgetTree* Tree, const FString& Label)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Label));
		T->SetJustification(ETextJustify::Center);
		T->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo F = T->GetFont();
		F.Size = 22;
		T->SetFont(F);
		Btn->AddChild(T);
		return Btn;
	}
}

TSharedRef<SWidget> UNavCloudAdminOverlay::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Root;

		// ── 상단 바: 목록 + 상태 ────────────────────────────────────────────
		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Bar->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f));
		Bar->SetPadding(FMargin(16.f, 12.f));
		Bar->SetHorizontalAlignment(HAlign_Fill);
		Bar->SetVerticalAlignment(VAlign_Top);
		Bar->SetClipping(EWidgetClipping::ClipToBounds);
		BarPanel = Bar;

		UTextBlock* Body = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Body->SetJustification(ETextJustify::Left);
		Body->SetAutoWrapText(true);
		Body->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo BF = Body->GetFont();
		BF.Size = 16;
		Body->SetFont(BF);
		BodyText = Body;
		Bar->SetContent(Body);

		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(Bar)))
		{
			S->SetAnchors(FAnchors(0.02f, 0.f, 0.98f, 0.f));
			S->SetAutoSize(false);
			S->SetOffsets(FMargin(0.f, 74.f, 0.f, 210.f)); // 서버주소 줄 아래로
		}

		// ── 최상단: 서버주소 입력 + 적용 ───────────────────────────────────
		UBorder* UrlBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		UrlBar->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f));
		UrlBar->SetPadding(FMargin(8.f, 4.f));
		UHorizontalBox* UrlRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		ServerUrlBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		ServerUrlBox->SetHintText(FText::FromString(TEXT("서버 주소 http://… ")));
		if (UHorizontalBoxSlot* US = Cast<UHorizontalBoxSlot>(UrlRow->AddChildToHorizontalBox(ServerUrlBox)))
		{
			FSlateChildSize Sz(ESlateSizeRule::Fill); Sz.Value = 4.f; US->SetSize(Sz);
			US->SetPadding(FMargin(2.f, 0.f));
		}
		ApplyUrlBtn = MakeLabeledButton(WidgetTree, TEXT("적용"));
		if (UHorizontalBoxSlot* AS = Cast<UHorizontalBoxSlot>(UrlRow->AddChildToHorizontalBox(ApplyUrlBtn)))
		{
			FSlateChildSize Sz(ESlateSizeRule::Fill); Sz.Value = 1.f; AS->SetSize(Sz);
			AS->SetPadding(FMargin(2.f, 0.f));
		}
		UrlBar->SetContent(UrlRow);
		if (UCanvasPanelSlot* USlot = Cast<UCanvasPanelSlot>(Root->AddChild(UrlBar)))
		{
			USlot->SetAnchors(FAnchors(0.02f, 0.f, 0.98f, 0.f));
			USlot->SetAutoSize(false);
			USlot->SetOffsets(FMargin(0.f, 16.f, 0.f, 52.f));
		}

		// ── 화면 중앙: 조준점 ＋ (여기 바닥에 앵커가 놓인다) ─────────────────
		UTextBlock* Cross = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Cross->SetText(FText::FromString(TEXT("＋")));
		Cross->SetJustification(ETextJustify::Center);
		Cross->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.3f, 0.3f))); // 초기 빨강(트래킹 전)
		FSlateFontInfo CF = Cross->GetFont();
		CF.Size = 48;
		Cross->SetFont(CF);
		CrosshairText = Cross;
		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Root->AddChild(Cross)))
		{
			CS->SetAnchors(FAnchors(0.5f, 0.5f));     // 화면 정중앙
			CS->SetAlignment(FVector2D(0.5f, 0.5f));
			CS->SetAutoSize(true);
			CS->SetPosition(FVector2D(0.f, 0.f));
		}

		// ── 하단: 버튼 바 (◀ 이전 / 다음 ▶ / 여기 등록) ───────────────────
		UBorder* BtnBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		BtnBar->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f));
		BtnBar->SetPadding(FMargin(10.f, 8.f));
		BtnBar->SetHorizontalAlignment(HAlign_Fill);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		PrevButton = MakeLabeledButton(WidgetTree, TEXT("◀ 이전"));
		NextButton = MakeLabeledButton(WidgetTree, TEXT("다음 ▶"));
		RegisterButton = MakeLabeledButton(WidgetTree, TEXT("● 여기 등록"));
		PreviewButton = MakeLabeledButton(WidgetTree, TEXT("👁 미리보기"));
		PreviewLabel = Cast<UTextBlock>(PreviewButton->GetChildAt(0));

		auto AddCell = [&](UButton* B, float Fill)
		{
			if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(B)))
			{
				HS->SetPadding(FMargin(6.f, 0.f));
				FSlateChildSize Sz(ESlateSizeRule::Fill);
				Sz.Value = Fill;
				HS->SetSize(Sz);
			}
		};
		AddCell(PrevButton, 1.f);
		AddCell(RegisterButton, 2.f); // 가운데 등록 버튼을 넓게
		AddCell(NextButton, 1.f);
		AddCell(PreviewButton, 1.5f);
		BtnBar->SetContent(Row);

		if (UCanvasPanelSlot* BS = Cast<UCanvasPanelSlot>(Root->AddChild(BtnBar)))
		{
			// 화면 하단 가로 전체, 높이 110.
			BS->SetAnchors(FAnchors(0.02f, 1.f, 0.98f, 1.f));
			BS->SetAlignment(FVector2D(0.f, 1.f));
			BS->SetAutoSize(false);
			BS->SetOffsets(FMargin(0.f, -120.f, 0.f, 110.f));
		}

		// ── 오프셋 보정 패드 (미리보기일 때만 표시) ─────────────────────────
		UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pad->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f));
		Pad->SetPadding(FMargin(8.f, 6.f));
		NudgePanel = Pad;

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		auto MakeRow = [&]() -> UHorizontalBox*
		{
			UHorizontalBox* R = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Col->AddChildToVerticalBox(R)))
			{
				VS->SetPadding(FMargin(0.f, 3.f));
			}
			return R;
		};
		auto AddBtn = [&](UHorizontalBox* Row, const FString& Label) -> UButton*
		{
			UButton* B = MakeLabeledButton(WidgetTree, Label);
			if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(B)))
			{
				HS->SetPadding(FMargin(3.f, 0.f));
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
			return B;
		};

		UHorizontalBox* Row1 = MakeRow();
		NudgeFwd   = AddBtn(Row1, TEXT("앞"));
		NudgeBack  = AddBtn(Row1, TEXT("뒤"));
		NudgeLeft  = AddBtn(Row1, TEXT("좌"));
		NudgeRight = AddBtn(Row1, TEXT("우"));
		NudgeUp    = AddBtn(Row1, TEXT("위"));
		NudgeDown  = AddBtn(Row1, TEXT("아래"));

		UHorizontalBox* Row2 = MakeRow();
		RotCCW    = AddBtn(Row2, TEXT("↺"));
		RotCW     = AddBtn(Row2, TEXT("↻"));
		ScaleUp   = AddBtn(Row2, TEXT("크게"));
		ScaleDown = AddBtn(Row2, TEXT("작게"));
		ResetBtn  = AddBtn(Row2, TEXT("리셋"));
		StepBtn   = AddBtn(Row2, TEXT("스텝×1"));
		StepLabelText = Cast<UTextBlock>(StepBtn->GetChildAt(0));

		Pad->SetContent(Col);
		Pad->SetVisibility(ESlateVisibility::Collapsed); // 기본 숨김

		if (UCanvasPanelSlot* PS = Cast<UCanvasPanelSlot>(Root->AddChild(Pad)))
		{
			// 하단 버튼 바(높이 110, 하단 120 위) 바로 위에 배치.
			PS->SetAnchors(FAnchors(0.02f, 1.f, 0.98f, 1.f));
			PS->SetAlignment(FVector2D(0.f, 1.f));
			PS->SetAutoSize(false);
			PS->SetOffsets(FMargin(0.f, -250.f, 0.f, 120.f));
		}
	}
	return Super::RebuildWidget();
}

void UNavCloudAdminOverlay::SetBody(const FString& InText, const FColor& InColor)
{
	if (BodyText != nullptr)
	{
		BodyText->SetText(FText::FromString(InText));
		BodyText->SetColorAndOpacity(FSlateColor(FLinearColor(InColor)));
	}
}

void UNavCloudAdminOverlay::SetCrosshairColor(const FColor& InColor)
{
	if (CrosshairText != nullptr)
	{
		CrosshairText->SetColorAndOpacity(FSlateColor(FLinearColor(InColor)));
	}
}

void UNavCloudAdminOverlay::SetNudgeVisible(bool bVisible)
{
	if (NudgePanel != nullptr)
	{
		NudgePanel->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UNavCloudAdminOverlay::SetStepLabel(const FString& InText)
{
	if (StepLabelText != nullptr)
	{
		StepLabelText->SetText(FText::FromString(InText));
	}
}

FString UNavCloudAdminOverlay::GetServerUrlText() const
{
	return ServerUrlBox != nullptr ? ServerUrlBox->GetText().ToString() : FString();
}

void UNavCloudAdminOverlay::SetServerUrlText(const FString& InUrl)
{
	if (ServerUrlBox != nullptr)
	{
		ServerUrlBox->SetText(FText::FromString(InUrl));
	}
}

void UNavCloudAdminOverlay::SetPreviewActive(bool bActive)
{
	if (PreviewLabel != nullptr)
	{
		PreviewLabel->SetText(FText::FromString(bActive ? TEXT("👁 미리보기 ON") : TEXT("👁 미리보기")));
	}
	if (PreviewButton != nullptr)
	{
		// 켜지면 초록, 꺼지면 기본 회색.
		PreviewButton->SetBackgroundColor(bActive ? FLinearColor(0.15f, 0.6f, 0.2f) : FLinearColor(0.35f, 0.35f, 0.38f));
	}
}
