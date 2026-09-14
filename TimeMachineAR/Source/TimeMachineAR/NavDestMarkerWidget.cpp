#include "NavDestMarkerWidget.h"

#include "NavDestinations.h"
#include "NavLocalizer.h"
#include "NavClient.h"                 // LogNav
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"

TSharedRef<SWidget> UNavDestMarkerWidget::RebuildWidget()
{
	// 순수 C++ 경로(WBP 미상속): 전체화면 빈 캔버스를 루트로 세워 NativePaint 가 화면 전체를 받게 한다.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Root;
	}
	return Super::RebuildWidget();
}

void UNavDestMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// AR 탭·미니맵 터치를 가리지 않는다(그림만 얹는 오버레이).
	SetVisibility(ESlateVisibility::HitTestInvisible);
	LoadChevron();
}

void UNavDestMarkerWidget::LoadChevron()
{
	bHasChevron = false;
	ChevronBrush = FSlateBrush();
	const FString Path = FNavDestinations::ForwardChevronObjectPath();
	if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path))
	{
		ChevronBrush.SetResourceObject(Tex);
		ChevronBrush.DrawAs = ESlateBrushDrawType::Image;
		ChevronBrush.ImageSize = FVector2D(ChevronSizePx, ChevronSizePx);
		bHasChevron = true;
	}
	else
	{
		UE_LOG(LogNav, Warning, TEXT("[destmarker] 전방 셰브론 로드 실패(거리 배지만 그림): %s"), *Path);
	}
}

void UNavDestMarkerWidget::SetDestination(const FVector2D& MapXY, const FString& InNodeType, const FString& InLabel)
{
	DestMapXY = MapXY;
	NodeType = InNodeType;
	Label = InLabel;
	Accent = FNavDestinations::AccentColor(InNodeType);
	bHasDest = FNavDestinations::IsDestination(InNodeType);
	LoadIcon();
}

void UNavDestMarkerWidget::ClearDestination()
{
	bHasDest = false;
}

void UNavDestMarkerWidget::LoadIcon()
{
	bHasIcon = false;
	IconBrush = FSlateBrush();
	const FString Path = FNavDestinations::IconObjectPath(NodeType, Label);
	if (Path.IsEmpty())
	{
		return;
	}
	if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path))
	{
		IconBrush.SetResourceObject(Tex);
		IconBrush.DrawAs = ESlateBrushDrawType::Image;
		IconBrush.ImageSize = FVector2D(IconSizePx, IconSizePx);
		bHasIcon = true;
	}
	else
	{
		UE_LOG(LogNav, Warning, TEXT("[destmarker] 아이콘 로드 실패: %s"), *Path);
	}
}

FString UNavDestMarkerWidget::DistanceText() const
{
	// 도착 판정은 NavRouteProgress 하나(히스테리시스 포함). 여기서 거리로 다시 판정하지 않는다.
	if (bArrived)
	{
		return TEXT("도착");
	}
	return FString::Printf(TEXT("%.0f m"), FMath::Max(0.f, RemainingCm) / 100.f);
}

int32 UNavDestMarkerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Base = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!bHasDest || IsDesignTime())
	{
		return Base;
	}

	APlayerController* PC = GetOwningPlayer();
	UNavLocalizer* Loc = UNavLocalizer::GetNavLocalizer(this);
	if (PC == nullptr || Loc == nullptr || !Loc->IsLocalized())
	{
		return Base;   // 측위 전엔 목적지 월드 위치를 모른다.
	}

	// 목적지 맵 좌표 → 월드(바닥에서 MarkerHeightCm 띄움). 재측위로 변환이 바뀌면 자동 반영.
	const FVector World = Loc->MapToWorld(DestMapXY.X, DestMapXY.Y, 0.f)
		+ FVector(0.f, 0.f, MarkerHeightCm);

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D Center = Size * 0.5f;
	int32 Layer = Base + 1;

	// 전방 셰브론 + 거리 배지: 가장 먼 보이는 바닥 발자국 위(황금 발자국 시안 상태 1·2).
	// 카메라 뒤·화면 밖이면 그리지 않는다(가장자리 표시는 아래 마름모 몫). 도착이면 링 위 마름모가 대신한다.
	if (bHasGuideHead && !bArrived)
	{
		FVector2D HeadVP;
		if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, GuideHeadWorld, HeadVP, false)
			&& HeadVP.X >= 0.f && HeadVP.X <= Size.X && HeadVP.Y >= 0.f && HeadVP.Y <= Size.Y)
		{
			DrawGuideHead(OutDrawElements, Layer, AllottedGeometry, HeadVP);
			Layer += 3;
		}
	}

	// 화면 투영. DPI 보정된 위젯 로컬 좌표를 그대로 돌려준다(전체화면 오버레이라 로컬=뷰포트).
	FVector2D VP;
	const bool bFront = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, VP, false);

	const float M = EdgeMarginPx;
	const bool bOnScreen = bFront && VP.X >= M && VP.X <= Size.X - M && VP.Y >= M && VP.Y <= Size.Y - M;

	if (bOnScreen)
	{
		DrawMarker(OutDrawElements, Layer, AllottedGeometry, VP);
		return Layer + 2;
	}

	// 화면 밖(또는 뒤): 카메라 기준으로 화면 방향을 구해 가장자리에 화살표.
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FMatrix CamM = FRotationMatrix(CamRot);
	const FVector Fwd = CamM.GetScaledAxis(EAxis::X);
	const FVector Right = CamM.GetScaledAxis(EAxis::Y);
	const FVector Up = CamM.GetScaledAxis(EAxis::Z);
	const FVector To = World - CamLoc;

	FVector2D Dir;
	if (bFront)
	{
		// 앞이지만 화면 밖: 투영점이 화면 밖에 있으니 중심→투영점 방향.
		Dir = VP - Center;
	}
	else
	{
		// 뒤: 좌우 성분으로 가장 가까운 가장자리를 정한다.
		const float R = FVector::DotProduct(To, Right);
		const float U = FVector::DotProduct(To, Up);
		Dir = FVector2D(-R, U);   // 뒤라 좌우 반전, 화면 y 는 아래가 +.
	}
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(0.f, 1.f);
	}
	Dir.Normalize();

	// 중심에서 Dir 로 뻗어 여백 사각형 경계에 닿는 점.
	const float HalfW = Center.X - M;
	const float HalfH = Center.Y - M;
	const float tx = (FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER) ? HalfW / FMath::Abs(Dir.X) : TNumericLimits<float>::Max();
	const float ty = (FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER) ? HalfH / FMath::Abs(Dir.Y) : TNumericLimits<float>::Max();
	const float t = FMath::Min(tx, ty);
	const FVector2D EdgePos = Center + Dir * t;

	DrawEdgeArrow(OutDrawElements, Layer, AllottedGeometry, EdgePos, Dir);
	return Layer + 2;
}

void UNavDestMarkerWidget::DrawMarker(FSlateWindowElementList& Out, int32 Layer,
	const FGeometry& Geom, const FVector2D& At) const
{
	const FPaintGeometry LineGeom = Geom.ToPaintGeometry();
	const float Half = DiamondHalfPx;

	// 마름모 테두리(코드 그리기, 색=node_type).
	TArray<FVector2D> Diamond;
	Diamond.Add(At + FVector2D(0.f, -Half));
	Diamond.Add(At + FVector2D(Half, 0.f));
	Diamond.Add(At + FVector2D(0.f, Half));
	Diamond.Add(At + FVector2D(-Half, 0.f));
	Diamond.Add(At + FVector2D(0.f, -Half));   // 닫는다(자기참조 Add(Diamond[0]) 금지 — 재할당 시 크래시).
	FSlateDrawElement::MakeLines(Out, Layer, LineGeom, Diamond,
		ESlateDrawEffect::None, Accent, true, DiamondThicknessPx);

	// 안쪽 아이콘.
	if (bHasIcon)
	{
		const FVector2D IconSz(IconSizePx, IconSizePx);
		const FPaintGeometry IconGeom = Geom.ToPaintGeometry(
			IconSz, FSlateLayoutTransform(At - IconSz * 0.5f));
		FSlateDrawElement::MakeBox(Out, Layer + 1, IconGeom, &IconBrush,
			ESlateDrawEffect::None, FLinearColor::White);
	}

	// 남은 거리 pill(마름모 위). 도착이면 "도착".
	DrawPill(Out, Layer + 1, Geom, At + FVector2D(0.f, -Half - 23.f), DistanceText(), 18);
}

int32 UNavDestMarkerWidget::DrawPill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geom,
	const FVector2D& Center, const FString& Text, int32 FontSize) const
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
	const float PillW = FMath::Max(48.f, Text.Len() * FontSize * 0.67f + 20.f);
	const float PillH = FontSize + 12.f;
	const FVector2D PillPos = Center - FVector2D(PillW, PillH) * 0.5f;

	FSlateBrush Pill;
	Pill.DrawAs = ESlateBrushDrawType::RoundedBox;   // 텍스처 없이 솔리드 둥근 사각형.
	Pill.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	const FPaintGeometry PillGeom = Geom.ToPaintGeometry(
		FVector2D(PillW, PillH), FSlateLayoutTransform(PillPos));
	FSlateDrawElement::MakeBox(Out, Layer, PillGeom, &Pill,
		ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.7f));

	const FPaintGeometry TextGeom = Geom.ToPaintGeometry(
		FVector2D(PillW, PillH), FSlateLayoutTransform(PillPos + FVector2D(10.f, 4.f)));
	FSlateDrawElement::MakeText(Out, Layer + 1, TextGeom, Text, Font,
		ESlateDrawEffect::None, FLinearColor::White);
	return Layer + 2;
}

void UNavDestMarkerWidget::DrawGuideHead(FSlateWindowElementList& Out, int32 Layer,
	const FGeometry& Geom, const FVector2D& At) const
{
	// 셰브론(황금 시트 04 "전방 화살표") 중심을 At 에, 거리 텍스트를 그 위에.
	float Top = At.Y;
	if (bHasChevron)
	{
		const FVector2D Sz(ChevronSizePx, ChevronSizePx);
		const FPaintGeometry ChevGeom = Geom.ToPaintGeometry(Sz, FSlateLayoutTransform(At - Sz * 0.5f));
		FSlateDrawElement::MakeBox(Out, Layer, ChevGeom, &ChevronBrush,
			ESlateDrawEffect::None, FLinearColor::White);
		Top = At.Y - ChevronSizePx * 0.5f;
	}
	// 거리 숫자: 시안처럼 검은 pill 없이 크게, 가독성용 그림자만. 값은 Progress.RemainingCm/100.
	const FString Text = DistanceText();
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 26);
	const float W = Text.Len() * 18.f + 20.f;
	const FVector2D TextPos(At.X - W * 0.5f + 10.f, Top - 40.f);
	const FPaintGeometry Shadow = Geom.ToPaintGeometry(FVector2D(W, 34.f), FSlateLayoutTransform(TextPos + FVector2D(1.5f, 1.5f)));
	FSlateDrawElement::MakeText(Out, Layer + 1, Shadow, Text, Font, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.75f));
	const FPaintGeometry TextGeom = Geom.ToPaintGeometry(FVector2D(W, 34.f), FSlateLayoutTransform(TextPos));
	FSlateDrawElement::MakeText(Out, Layer + 2, TextGeom, Text, Font, ESlateDrawEffect::None, FLinearColor::White);
}

void UNavDestMarkerWidget::DrawEdgeArrow(FSlateWindowElementList& Out, int32 Layer,
	const FGeometry& Geom, const FVector2D& EdgePos, const FVector2D& Dir) const
{
	const FPaintGeometry LineGeom = Geom.ToPaintGeometry();
	const FVector2D Perp(-Dir.Y, Dir.X);
	const float L = 22.f;   // 화살표 길이(px).
	const float W = 15.f;   // 밑변 반폭(px).

	const FVector2D Tip = EdgePos + Dir * L;
	const FVector2D BaseL = EdgePos - Dir * L * 0.4f + Perp * W;
	const FVector2D BaseR = EdgePos - Dir * L * 0.4f - Perp * W;

	TArray<FVector2D> Tri;
	Tri.Add(Tip);
	Tri.Add(BaseL);
	Tri.Add(BaseR);
	Tri.Add(Tip);
	FSlateDrawElement::MakeLines(Out, Layer, LineGeom, Tri,
		ESlateDrawEffect::None, Accent, true, 5.f);

	// 거리 문구(화살표 안쪽).
	const FString Text = DistanceText();
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FVector2D TextPos = EdgePos - Dir * (L + 34.f) - FVector2D(16.f, 10.f);
	const FPaintGeometry TextGeom = Geom.ToPaintGeometry(
		FVector2D(80.f, 24.f), FSlateLayoutTransform(TextPos));
	FSlateDrawElement::MakeText(Out, Layer + 1, TextGeom, Text, Font,
		ESlateDrawEffect::None, Accent);
}
