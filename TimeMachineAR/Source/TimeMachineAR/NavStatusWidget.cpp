#include "NavStatusWidget.h"

#include "Components/TextBlock.h"
#include "NavMinimapWidget.h"
#include "NavLocalizer.h"
#include "NavClient.h"   // LogNav

void UNavStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 5-B: 측위 경고는 NavLocalizer 델리게이트에 자동 연결한다 → BP 배선이 필요 없다.
	BoundLocalizer = ResolveLocalizer();
	if (BoundLocalizer.IsValid())
	{
		BoundLocalizer->OnTrackingDegraded.AddDynamic(this, &UNavStatusWidget::HandleTrackingDegraded);
		BoundLocalizer->OnTrackingRecovered.AddDynamic(this, &UNavStatusWidget::HandleTrackingRecovered);
	}
	else
	{
		UE_LOG(LogNav, Warning,
			TEXT("[status] NavLocalizer 를 못 찾았습니다. 측위 경고 자동 연결 실패(월드 준비 전?)."));
	}

	// §D 안내 로그가 화면 최상단을 쓰므로, 측위 경고(QR 다시 찍기)는 그 아래로 내려
	// 겹치지 않게 한다. RenderTranslation 은 레이아웃을 안 건드리고 그리기만 내린다.
	if (WarningPanel != nullptr)
	{
		WarningPanel->SetRenderTranslation(FVector2D(0.f, 150.f));
	}

	// 시작 상태: 전부 숨김. 디자이너에서는 배치 확인을 위해 배너를 보여 준다.
	if (IsDesignTime() && bPreviewInDesigner)
	{
		SetWidgetShown(BannerPanel, true);
		SetWidgetShown(WarningPanel, false);
		SetWidgetShown(ArrivalPanel, false);
		SetTextSafe(InstructionText, TEXT("앞으로 6m 직진하세요"));
		SetTextSafe(DistanceText, TEXT("6m"));
		SetTextSafe(NextText, TEXT("다음: 우회전"));
	}
	else
	{
		bWarningActive = false;
		LastGuidance = FNavGuidance();
		RefreshPanels();
	}
}

void UNavStatusWidget::NativeDestruct()
{
	if (BoundLocalizer.IsValid())
	{
		BoundLocalizer->OnTrackingDegraded.RemoveDynamic(this, &UNavStatusWidget::HandleTrackingDegraded);
		BoundLocalizer->OnTrackingRecovered.RemoveDynamic(this, &UNavStatusWidget::HandleTrackingRecovered);
	}
	if (BoundMinimap.IsValid())
	{
		BoundMinimap->OnGuidanceUpdated.RemoveDynamic(this, &UNavStatusWidget::ApplyGuidance);
	}
	Super::NativeDestruct();
}

void UNavStatusWidget::AttachToMinimap(UNavMinimapWidget* Minimap)
{
	if (Minimap == nullptr || Minimap == BoundMinimap.Get())
	{
		return;   // 이미 붙었거나 대상이 없다.
	}
	if (BoundMinimap.IsValid())
	{
		BoundMinimap->OnGuidanceUpdated.RemoveDynamic(this, &UNavStatusWidget::ApplyGuidance);
	}
	Minimap->OnGuidanceUpdated.AddDynamic(this, &UNavStatusWidget::ApplyGuidance);
	BoundMinimap = Minimap;
}

void UNavStatusWidget::ApplyGuidance(const FNavGuidance& Guidance)
{
	LastGuidance = Guidance;
	RefreshPanels();
}

void UNavStatusWidget::ShowTrackingWarning(const FString& Reason)
{
	bWarningActive = true;
	WarningMessage = Reason;
	RefreshPanels();
}

void UNavStatusWidget::HideTrackingWarning()
{
	bWarningActive = false;
	RefreshPanels();
}

void UNavStatusWidget::RequestRescan()
{
	if (UNavLocalizer* Localizer = ResolveLocalizer())
	{
		Localizer->RescanFromUser();
	}
}

void UNavStatusWidget::HandleTrackingDegraded(const FString& Reason)
{
	ShowTrackingWarning(Reason);
}

void UNavStatusWidget::HandleTrackingRecovered()
{
	HideTrackingWarning();
}

void UNavStatusWidget::RefreshPanels()
{
	if (IsDesignTime())
	{
		return;   // 디자이너 미리보기는 NativeConstruct 에서 고정.
	}

	// 턴바이턴 배너(5-D)·도착 패널은 8단계 §D 안내 로그(NavGuideLogWidget, 그래픽/문구)가
	// 대체하므로 더 이상 띄우지 않는다. 이 위젯은 측위 경고(QR 다시 찍기)만 담당한다.
	SetWidgetShown(BannerPanel, false);
	SetWidgetShown(ArrivalPanel, false);
	SetWidgetShown(WarningPanel, bWarningActive);

	if (bWarningActive)
	{
		SetTextSafe(WarningText, WarningMessage);
	}
}

FString UNavStatusWidget::FormatDistanceCm(float DistanceCm)
{
	const int32 M = FMath::RoundToInt(DistanceCm / 100.f);
	return (M <= 0) ? FString(TEXT("곧")) : FString::Printf(TEXT("%dm"), M);
}

void UNavStatusWidget::SetWidgetShown(UWidget* W, bool bShown)
{
	if (W != nullptr)
	{
		// SelfHitTestInvisible: 패널 자체는 터치를 안 먹지만 안의 버튼("다시 찍기")은
		// 그대로 눌린다 → 미니맵/네비 상호작용을 막지 않는다.
		W->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UNavStatusWidget::SetTextSafe(UTextBlock* T, const FString& S)
{
	if (T != nullptr)
	{
		T->SetText(FText::FromString(S));
	}
}

UNavLocalizer* UNavStatusWidget::ResolveLocalizer() const
{
	return UNavLocalizer::GetNavLocalizer(this);
}
