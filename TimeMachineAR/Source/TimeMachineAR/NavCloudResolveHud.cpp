// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavCloudResolveHud.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

namespace
{
	/** 토스트 한 건이 떠 있는 시간(초). */
	constexpr float kToastSeconds = 3.5f;
}

TSharedRef<SWidget> UNavCloudResolveHud::RebuildWidget()
{
	// WBP 없이 만들어졌으면(순수 C++ 경로) 위젯 트리를 여기서 세운다 — 8단계 안내 로그와 같은 패턴.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Root;

		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Bar->SetBrushColor(FLinearColor(0.05f, 0.20f, 0.10f, 0.94f)); // 인식=초록 계열
		Bar->SetPadding(FMargin(18.f, 10.f));
		Bar->SetHorizontalAlignment(HAlign_Fill);
		Bar->SetVerticalAlignment(VAlign_Center);
		Bar->SetClipping(EWidgetClipping::ClipToBounds);
		BarPanel = Bar;

		UTextBlock* Msg = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Msg->SetJustification(ETextJustify::Center);
		Msg->SetAutoWrapText(true);
		Msg->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo Font = Msg->GetFont();
		Font.Size = 20;
		Msg->SetFont(Font);
		MessageText = Msg;
		Bar->SetContent(Msg);

		if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Bar)))
		{
			// 화면 하단 가운데. 상단은 안내 로그(8단계)가 쓰므로 겹치지 않게 아래로 둔다.
			// 스트레치 앵커라 Offsets 는 (좌인셋, 상단Y, 우인셋, 높이) 로 읽힌다.
			BarSlot->SetAnchors(FAnchors(0.10f, 1.f, 0.90f, 1.f));
			BarSlot->SetAutoSize(false);
			BarSlot->SetOffsets(FMargin(0.f, -180.f, 0.f, 96.f));
		}
	}
	return Super::RebuildWidget();
}

void UNavCloudResolveHud::NativeConstruct()
{
	Super::NativeConstruct();
	// 표시 전용 오버레이(터치 안 먹음). 띄울 게 없으면 접어 둔다.
	SetVisibility(ESlateVisibility::Collapsed);
}

void UNavCloudResolveHud::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	Super::NativeDestruct();
}

void UNavCloudResolveHud::ShowRecognized(int32 PointNo, float Seconds)
{
	ShowMessage(FString::Printf(TEXT("%d번 앵커 인식 (%.1f초)"), PointNo, Seconds));
}

void UNavCloudResolveHud::ShowLocalized(int32 PointNo, float Seconds)
{
	ShowMessage(FString::Printf(TEXT("%d번 앵커로 측위 (%.1f초) — 지도 준비됨"), PointNo, Seconds));
}

void UNavCloudResolveHud::ShowMessage(const FString& Message)
{
	// 같은 문구가 이미 큐에 있으면 중복으로 쌓지 않는다.
	if (Pending.Contains(Message))
	{
		return;
	}
	Pending.Add(Message);

	// 지금 아무것도 안 떠 있으면 바로 첫 건을 띄운다(떠 있으면 타이머가 이어받는다).
	if (GetVisibility() == ESlateVisibility::Collapsed)
	{
		ShowNext();
	}
}

void UNavCloudResolveHud::ShowNext()
{
	UWorld* World = GetWorld();
	if (Pending.Num() == 0)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const FString Next = Pending[0];
	Pending.RemoveAt(0);
	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Next));
	}
	SetVisibility(ESlateVisibility::HitTestInvisible); // 표시 전용 — 기존 UI 입력을 막지 않는다.

	if (World != nullptr)
	{
		World->GetTimerManager().ClearTimer(HideTimer);
		World->GetTimerManager().SetTimer(
			HideTimer, this, &UNavCloudResolveHud::ShowNext, kToastSeconds, false);
	}
}
