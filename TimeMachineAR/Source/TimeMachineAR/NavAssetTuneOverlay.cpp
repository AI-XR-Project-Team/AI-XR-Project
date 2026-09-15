// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavAssetTuneOverlay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	/** 뒤의 AR 화면이 비치게 반투명 — 글씨는 흰색 굵게. */
	const FLinearColor kPanelColor(0.03f, 0.04f, 0.07f, 0.72f);
	const FLinearColor kButtonColor(0.22f, 0.24f, 0.30f, 0.95f);

	UButton* MakeButton(UWidgetTree* Tree, const FString& Label, int32 FontSize, UTextBlock** OutText = nullptr)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(kButtonColor);
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Label));
		T->SetJustification(ETextJustify::Center);
		T->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo F = T->GetFont();
		F.Size = FontSize;
		T->SetFont(F);
		Btn->AddChild(T);
		if (OutText != nullptr)
		{
			*OutText = T;
		}
		return Btn;
	}
}

TSharedRef<SWidget> UNavAssetTuneOverlay::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Root;

		// ── 토글 버튼: 오른쪽 가장자리, 화면 높이 30% 지점 (상단 바·하단 바와 안 겹치게) ──
		UTextBlock* ToggleText = nullptr;
		ToggleBtn = MakeButton(WidgetTree, TEXT("조정"), 20, &ToggleText);
		ToggleLabel = ToggleText;
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(ToggleBtn)))
		{
			S->SetAnchors(FAnchors(1.f, 0.30f));
			S->SetAlignment(FVector2D(1.f, 0.5f));
			S->SetAutoSize(true);
			S->SetPosition(FVector2D(-12.f, 0.f));
		}

		// ── 패드: 화면 아래쪽(하단 바 위), 가로 전체 ──────────────────────────
		UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Pad->SetBrushColor(kPanelColor);
		Pad->SetPadding(FMargin(10.f, 8.f));
		PadPanel = Pad;

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Pad->SetContent(Col);

		UTextBlock* Status = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Status->SetAutoWrapText(true);
		Status->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo SF = Status->GetFont();
		SF.Size = 17;
		Status->SetFont(SF);
		Status->SetShadowOffset(FVector2D(1.f, 1.f));
		Status->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		StatusText = Status;
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Col->AddChildToVerticalBox(Status)))
		{
			VS->SetPadding(FMargin(4.f, 2.f, 4.f, 6.f));
		}

		auto MakeRow = [&]() -> UHorizontalBox*
		{
			UHorizontalBox* R = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Col->AddChildToVerticalBox(R)))
			{
				VS->SetPadding(FMargin(0.f, 3.f));
			}
			return R;
		};
		auto AddBtn = [&](UHorizontalBox* Row, const FString& Label, FName Handler, UTextBlock** OutText = nullptr) -> UButton*
		{
			UButton* B = MakeButton(WidgetTree, Label, 21, OutText);
			FScriptDelegate D;
			D.BindUFunction(this, Handler);
			B->OnClicked.Add(D);
			if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(B)))
			{
				HS->SetPadding(FMargin(3.f, 0.f));
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
			return B;
		};

		// 이동은 **화면(카메라) 기준** — 「앞」 = 지금 보는 방향으로 멀어짐.
		UHorizontalBox* Row1 = MakeRow();
		AddBtn(Row1, TEXT("앞"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleFwd));
		AddBtn(Row1, TEXT("뒤"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleBack));
		AddBtn(Row1, TEXT("◀ 좌"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleLeft));
		AddBtn(Row1, TEXT("우 ▶"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleRight));
		AddBtn(Row1, TEXT("▲ 위"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleUp));
		AddBtn(Row1, TEXT("▼ 아래"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleDown));

		UHorizontalBox* Row2 = MakeRow();
		AddBtn(Row2, TEXT("↺ 회전"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleRotCCW));
		AddBtn(Row2, TEXT("회전 ↻"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleRotCW));
		AddBtn(Row2, TEXT("＋ 크게"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleScaleUp));
		AddBtn(Row2, TEXT("－ 작게"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleScaleDown));
		UTextBlock* StepText = nullptr;
		AddBtn(Row2, TEXT("스텝 보통"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleStep), &StepText);
		StepLabel = StepText;

		UHorizontalBox* Row3 = MakeRow();
		NextTargetBtn = AddBtn(Row3, TEXT("대상 ▶"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleNextTarget));
		AddBtn(Row3, TEXT("저장값으로"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleRevert));
		AddBtn(Row3, TEXT("초기화"), GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleReset));
		NextTargetBtn->SetVisibility(ESlateVisibility::Collapsed);

		if (UCanvasPanelSlot* PS = Cast<UCanvasPanelSlot>(Root->AddChild(Pad)))
		{
			// 스트레치 앵커: Offsets = (좌인셋, 상단Y, 우인셋, 높이). 하단 바(약 120) 위에 얹는다.
			PS->SetAnchors(FAnchors(0.02f, 1.f, 0.98f, 1.f));
			PS->SetAlignment(FVector2D(0.f, 1.f));
			PS->SetAutoSize(false);
			PS->SetOffsets(FMargin(0.f, -150.f, 0.f, 290.f));
		}

		FScriptDelegate ToggleDelegate;
		ToggleDelegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UNavAssetTuneOverlay, HandleToggle));
		ToggleBtn->OnClicked.Add(ToggleDelegate);
	}
	return Super::RebuildWidget();
}

void UNavAssetTuneOverlay::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetAvailable(bAvailable);
	SetPadOpen(bPadOpen);
}

void UNavAssetTuneOverlay::SetAvailable(bool bInAvailable)
{
	bAvailable = bInAvailable;
	if (ToggleBtn != nullptr)
	{
		ToggleBtn->SetVisibility(bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!bAvailable)
	{
		SetPadOpen(false);
	}
}

void UNavAssetTuneOverlay::SetPadOpen(bool bOpen)
{
	bPadOpen = bOpen && bAvailable;
	if (PadPanel != nullptr)
	{
		PadPanel->SetVisibility(bPadOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (ToggleLabel != nullptr)
	{
		ToggleLabel->SetText(FText::FromString(bPadOpen ? TEXT("닫기") : TEXT("조정")));
	}
	if (ToggleBtn != nullptr)
	{
		ToggleBtn->SetBackgroundColor(bPadOpen ? FLinearColor(0.15f, 0.55f, 0.25f, 0.95f) : kButtonColor);
	}
}

void UNavAssetTuneOverlay::SetStatus(const FString& InText, const FColor& InColor)
{
	if (StatusText != nullptr)
	{
		StatusText->SetText(FText::FromString(InText));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(InColor)));
	}
}

void UNavAssetTuneOverlay::SetStepLabel(const FString& InText)
{
	if (StepLabel != nullptr)
	{
		StepLabel->SetText(FText::FromString(InText));
	}
}

void UNavAssetTuneOverlay::SetMultiTarget(bool bMulti)
{
	if (NextTargetBtn != nullptr)
	{
		NextTargetBtn->SetVisibility(bMulti ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UNavAssetTuneOverlay::HandleToggle()     { SetPadOpen(!bPadOpen); }
void UNavAssetTuneOverlay::HandleFwd()        { OnTune.Broadcast(ENavAssetTune::Forward); }
void UNavAssetTuneOverlay::HandleBack()       { OnTune.Broadcast(ENavAssetTune::Back); }
void UNavAssetTuneOverlay::HandleLeft()       { OnTune.Broadcast(ENavAssetTune::Left); }
void UNavAssetTuneOverlay::HandleRight()      { OnTune.Broadcast(ENavAssetTune::Right); }
void UNavAssetTuneOverlay::HandleUp()         { OnTune.Broadcast(ENavAssetTune::Up); }
void UNavAssetTuneOverlay::HandleDown()       { OnTune.Broadcast(ENavAssetTune::Down); }
void UNavAssetTuneOverlay::HandleRotCCW()     { OnTune.Broadcast(ENavAssetTune::RotCCW); }
void UNavAssetTuneOverlay::HandleRotCW()      { OnTune.Broadcast(ENavAssetTune::RotCW); }
void UNavAssetTuneOverlay::HandleScaleUp()    { OnTune.Broadcast(ENavAssetTune::ScaleUp); }
void UNavAssetTuneOverlay::HandleScaleDown()  { OnTune.Broadcast(ENavAssetTune::ScaleDown); }
void UNavAssetTuneOverlay::HandleStep()       { OnTune.Broadcast(ENavAssetTune::Step); }
void UNavAssetTuneOverlay::HandleNextTarget() { OnTune.Broadcast(ENavAssetTune::NextTarget); }
void UNavAssetTuneOverlay::HandleReset()      { OnTune.Broadcast(ENavAssetTune::Reset); }
void UNavAssetTuneOverlay::HandleRevert()     { OnTune.Broadcast(ENavAssetTune::Revert); }
