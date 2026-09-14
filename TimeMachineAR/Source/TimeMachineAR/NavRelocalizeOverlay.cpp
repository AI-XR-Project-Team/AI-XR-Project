// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavRelocalizeOverlay.h"

#include "NavLocalizer.h"
#include "NavClient.h"   // LogNav
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace
{
	// 설계 공간 = 시안 폰 목업(CSS px). UScaleBox 가 뷰포트에 맞춰 통째로 늘린다.
	constexpr float kRelocDesignW = 320.f;
	constexpr float kRelocDesignH = 676.f;
	constexpr float kRelocStageW = 220.f;
	constexpr float kRelocStageH = 170.f;

	// 런북 §B-3 구간표(초) — 왼쪽 정지 · → 느린 스캔 · 오른쪽 정지 · ← 빠른 복귀 · 왼쪽 정지.
	constexpr float kRelocScanStart = 0.30f;
	constexpr float kRelocScanEnd = 2.60f;
	constexpr float kRelocReturnStart = 2.85f;
	constexpr float kRelocReturnEnd = 3.35f;
	/** 안내판 아이콘이 켜질 때 1.5 → 1.15 로 줄어드는 시간(초). */
	constexpr float kRelocIconFlashSeconds = 0.25f;
	/** 안내판 아이콘 5개의 가로 중심(설계 px). 가운데 110 기준 −37·−19·0·+19·+37 — 폰 이동폭과 같다. */
	constexpr float kRelocIconX[5] = { 73.f, 91.f, 110.f, 129.f, 147.f };

	// 복구 화면 — ✓ pop 0.5초 · 1.5초 뒤 0.3초 페이드(명세 §3.4).
	constexpr float kRelocOkPopSeconds = 0.5f;
	constexpr float kRelocOkHoldSeconds = 1.5f;
	constexpr float kRelocOkFadeSeconds = 0.3f;

	struct FRelocKey
	{
		float T;
		float V;
	};

	float EvalRelocKeys(const FRelocKey* Keys, int32 Num, float T)
	{
		if (T <= Keys[0].T)
		{
			return Keys[0].V;
		}
		for (int32 i = 1; i < Num; ++i)
		{
			if (T <= Keys[i].T)
			{
				const float Span = Keys[i].T - Keys[i - 1].T;
				const float U = Span > KINDA_SMALL_NUMBER ? (T - Keys[i - 1].T) / Span : 1.f;
				return FMath::Lerp(Keys[i - 1].V, Keys[i].V, U);
			}
		}
		return Keys[Num - 1].V;
	}

	/** 시안 색은 sRGB 로 적혀 있다. 알파는 그대로. */
	FLinearColor RelocSrgb(uint8 R, uint8 G, uint8 B, float A = 1.f)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B)).CopyWithNewOpacity(A);
	}
	FLinearColor RelocWhite(float A = 1.f) { return RelocSrgb(255, 255, 255, A); }
	FLinearColor RelocSky(float A = 1.f) { return RelocSrgb(143, 211, 255, A); }
	FLinearColor RelocOk(float A = 1.f) { return RelocSrgb(61, 220, 132, A); }

	FSlateBrush RelocRounded(const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
	{
		return FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth);
	}

	/** 내용 없는 부품 하나(둥근 사각). 표시 전용이라 터치 판정에서 뺀다. */
	UBorder* MakeRelocPart(UWidgetTree* Tree, const FSlateBrush& Brush)
	{
		UBorder* Part = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Part->SetBrush(Brush);
		Part->SetPadding(FMargin(0.f));
		Part->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Part;
	}

	UCanvasPanelSlot* PlaceReloc(UCanvasPanel* Parent, UWidget* Child, const FVector2D& Pos, const FVector2D& Size)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.f, 0.f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetAutoSize(false);
		Slot->SetPosition(Pos);
		Slot->SetSize(Size);
		return Slot;
	}

	void PlaceRelocFill(UCanvasPanel* Parent, UWidget* Child)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		Slot->SetOffsets(FMargin(0.f));
	}

	/** A→B 막대(가운데 기준 회전). 화살촉·시야 부채꼴처럼 대각선을 에셋 없이 그린다. */
	UBorder* PlaceRelocBar(UWidgetTree* Tree, UCanvasPanel* Parent, const FVector2D& A, const FVector2D& B,
		float Thickness, const FLinearColor& Color)
	{
		const FVector2D D = B - A;
		const float Len = static_cast<float>(D.Size());
		UBorder* Bar = MakeRelocPart(Tree, RelocRounded(Color, Thickness * 0.5f));
		PlaceReloc(Parent, Bar, (A + B) * 0.5f - FVector2D(Len, Thickness) * 0.5f, FVector2D(Len, Thickness));
		Bar->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Bar->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)));
		return Bar;
	}

	UTextBlock* MakeRelocText(UWidgetTree* Tree, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetShadowOffset(FVector2D(0.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		FSlateFontInfo Font = Text->GetFont();   // 기본 폰트 — 한글 폴백 포함(토스트 HUD 와 같다)
		Font.Size = Size;
		Font.TypefaceFontName = FName(TEXT("Bold"));
		Text->SetFont(Font);
		return Text;
	}

	/** 튀어 오르는 pop(시안 cubic-bezier(.2,1.2,.4,1) 근사). f(0)=0 · f(1)=1 · 중간에 살짝 넘친다. */
	float RelocEaseOutBack(float U)
	{
		const float C1 = 1.70158f;
		const float C3 = C1 + 1.f;
		const float X = U - 1.f;
		return 1.f + C3 * X * X * X + C1 * X * X;
	}

	const TCHAR* const kRelocTitleScan = TEXT("가만히 서서 주변을\n천천히 스캔해 주세요");
	const TCHAR* const kRelocTitleOk = TEXT("측위가 잡혔습니다");
}

// ==================================================================== 순수 헬퍼

FString UNavRelocalizeOverlay::SubTextForReason(const FString& InReason)
{
	if (InReason == TEXT("shake"))
	{
		return TEXT("너무 빨리 움직였어요");
	}
	if (InReason == TEXT("occluded") || InReason == TEXT("dark"))
	{
		return TEXT("카메라가 가려졌거나 어두워요");
	}
	if (InReason == TEXT("featureless"))
	{
		return TEXT("무늬가 있는 곳을 비춰 주세요");
	}
	return TEXT("위치를 다시 확인하고 있어요");   // jump · resume · 콘솔의 다른 코드
}

FString UNavRelocalizeOverlay::StageSubText(int32 InStage, const FString& InReason)
{
	if (InStage >= 2)
	{
		return TEXT("밝은 곳으로 몇 걸음 옮긴 뒤 다시 천천히 둘러봐 주세요");
	}
	if (InStage == 1)
	{
		return TEXT("전시 안내판이나 벽면이 보이도록 천천히 돌려 주세요");
	}
	return SubTextForReason(InReason);
}

FNavRelocScanPose UNavRelocalizeOverlay::EvalScanPose(float CycleTime)
{
	const float T = FMath::Fmod(FMath::Max(CycleTime, 0.f), ScanCycleSeconds);

	// x: 왼쪽 정지 → 느린 스캔(+74px/2.3초) → 오른쪽 정지 → 빠른 복귀(0.5초) → 왼쪽 정지.
	static const FRelocKey XKeys[] = {
		{ 0.f, -37.f }, { kRelocScanStart, -37.f }, { kRelocScanEnd, 37.f },
		{ kRelocReturnStart, 37.f }, { kRelocReturnEnd, -37.f }, { ScanCycleSeconds, -37.f } };
	// 각도: 스캔 6° · 오른쪽 정지 0 · 복귀 −14° · 왼쪽 정지 0. 끊기지 않게 짧게(0.12~0.2초) 이어 준다.
	static const FRelocKey AngleKeys[] = {
		{ 0.f, 0.f }, { kRelocScanStart, 0.f }, { 0.45f, 6.f }, { kRelocScanEnd, 6.f }, { 2.72f, 0.f },
		{ kRelocReturnStart, 0.f }, { 3.05f, -14.f }, { kRelocReturnEnd, -14.f }, { 3.47f, 0.f }, { ScanCycleSeconds, 0.f } };
	// 불투명도: 복귀 구간만 0.55.
	static const FRelocKey AlphaKeys[] = {
		{ 0.f, 1.f }, { kRelocReturnStart, 1.f }, { 2.95f, 0.55f }, { kRelocReturnEnd, 0.55f }, { 3.45f, 1.f },
		{ ScanCycleSeconds, 1.f } };

	FNavRelocScanPose Pose;
	Pose.X = EvalRelocKeys(XKeys, UE_ARRAY_COUNT(XKeys), T);
	Pose.AngleDeg = EvalRelocKeys(AngleKeys, UE_ARRAY_COUNT(AngleKeys), T);
	Pose.Opacity = EvalRelocKeys(AlphaKeys, UE_ARRAY_COUNT(AlphaKeys), T);
	return Pose;
}

// ==================================================================== 위젯

TSharedRef<SWidget> UNavRelocalizeOverlay::RebuildWidget()
{
	// WBP 없이 만들어졌으면(순수 C++ 경로) 트리를 여기서 세운다 — NavCloudResolveHud 와 같은 패턴.
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UNavRelocalizeOverlay::BuildTree()
{
	UWidgetTree* Tree = WidgetTree;

	// ① 화면 전체 어둡기 — 이것 하나가 터치를 받는다(나머지는 HitTestInvisible → 여기로 올라와 Handled).
	Dim = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RelocDim"));
	Dim->SetPadding(FMargin(0.f));
	Dim->SetHorizontalAlignment(HAlign_Fill);
	Dim->SetVerticalAlignment(VAlign_Fill);
	Dim->SetVisibility(ESlateVisibility::Visible);
	Tree->RootWidget = Dim;

	UScaleBox* Scale = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
	Scale->SetStretch(EStretch::ScaleToFit);
	Scale->SetVisibility(ESlateVisibility::HitTestInvisible);
	Dim->SetContent(Scale);

	USizeBox* Design = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Design->SetWidthOverride(kRelocDesignW);
	Design->SetHeightOverride(kRelocDesignH);
	Scale->SetContent(Design);

	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	if (USizeBoxSlot* ColumnSlot = Cast<USizeBoxSlot>(Design->SetContent(Column)))
	{
		ColumnSlot->SetHorizontalAlignment(HAlign_Center);
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}

	// ② 스캔 그래픽 무대 220×170
	USizeBox* StageBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	StageBox->SetWidthOverride(kRelocStageW);
	StageBox->SetHeightOverride(kRelocStageH);
	if (UVerticalBoxSlot* StageSlot = Column->AddChildToVerticalBox(StageBox))
	{
		StageSlot->SetHorizontalAlignment(HAlign_Center);
		StageSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
	}
	UCanvasPanel* StageCanvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	StageBox->SetContent(StageCanvas);

	ScanGroup = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	PlaceRelocFill(StageCanvas, ScanGroup);
	OkGroup = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	PlaceRelocFill(StageCanvas, OkGroup);

	// 안내판 아이콘 5(12×9) — 폰이 지나간 자리부터 차례로 켜진다(= 앵커가 읽힌다).
	for (int32 i = 0; i < UE_ARRAY_COUNT(kRelocIconX); ++i)
	{
		UBorder* Icon = MakeRelocPart(Tree, RelocRounded(RelocSky(), 2.f));
		PlaceReloc(ScanGroup, Icon, FVector2D(kRelocIconX[i] - 6.f, 27.f - 4.5f), FVector2D(12.f, 9.f));
		Icon->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Icons.Add(Icon);
	}

	// 속도선 3 — 빠른 복귀 구간에만 뒤로 흘러간다.
	const FVector2D SpeedPos[3] = { FVector2D(118.f, 88.f), FVector2D(124.f, 95.f), FVector2D(120.f, 102.f) };
	const float SpeedLen[3] = { 26.f, 34.f, 22.f };
	for (int32 i = 0; i < 3; ++i)
	{
		UBorder* Line = MakeRelocPart(Tree, RelocRounded(RelocWhite(0.45f), 1.f));
		PlaceReloc(ScanGroup, Line, SpeedPos[i], FVector2D(SpeedLen[i], 2.f));
		Line->SetRenderOpacity(0.f);
		SpeedLines.Add(Line);
	}

	// 트랙 + 오른쪽 화살촉(막대 2 를 > 로).
	PlaceReloc(ScanGroup, MakeRelocPart(Tree, RelocRounded(RelocWhite(0.28f), 1.5f)),
		FVector2D(58.f, 150.f), FVector2D(104.f, 3.f));
	PlaceRelocBar(Tree, ScanGroup, FVector2D(155.f, 145.f), FVector2D(166.f, 151.5f), 2.5f, RelocWhite(0.55f));
	PlaceRelocBar(Tree, ScanGroup, FVector2D(155.f, 158.f), FVector2D(166.f, 151.5f), 2.5f, RelocWhite(0.55f));

	// 폰 — 시야 부채꼴·실루엣·카메라 점을 한 캔버스(64×115)에 묶어 같이 움직인다.
	// 몸체는 (10,31) 44×84 → 무대 (88,52). 회전 중심은 몸체의 (50%, 90%)(시안 transform-origin).
	PhoneGroup = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	PlaceReloc(ScanGroup, PhoneGroup, FVector2D(78.f, 21.f), FVector2D(64.f, 115.f));
	PhoneGroup->SetRenderTransformPivot(FVector2D(0.5f, (31.f + 84.f * 0.9f) / 115.f));
	PlaceRelocBar(Tree, PhoneGroup, FVector2D(32.f, 32.f), FVector2D(0.f, 0.f), 2.f, RelocSky(0.38f));
	PlaceRelocBar(Tree, PhoneGroup, FVector2D(32.f, 32.f), FVector2D(64.f, 0.f), 2.f, RelocSky(0.38f));
	PhoneBody = MakeRelocPart(Tree, RelocRounded(RelocWhite(0.06f), 9.f, RelocWhite(), 3.f));
	PlaceReloc(PhoneGroup, PhoneBody, FVector2D(10.f, 31.f), FVector2D(44.f, 84.f));
	PlaceReloc(PhoneGroup, MakeRelocPart(Tree, RelocRounded(RelocWhite(), 3.f)), FVector2D(29.f, 40.f), FVector2D(6.f, 6.f));

	// ✓ 링(복구) — 원 96 + 막대 2. 무대 가운데(110,85)가 링 중심이라 그룹 pivot(0.5,0.5) 으로 pop 한다.
	OkRing = MakeRelocPart(Tree, RelocRounded(FLinearColor::Transparent, 48.f, RelocOk(), 4.f));
	PlaceReloc(OkGroup, OkRing, FVector2D(62.f, 37.f), FVector2D(96.f, 96.f));
	UBorder* CheckShort = MakeRelocPart(Tree, RelocRounded(RelocOk(), 3.f));
	PlaceReloc(OkGroup, CheckShort, FVector2D(96.f, 85.f), FVector2D(6.f, 22.f));
	CheckShort->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	CheckShort->SetRenderTransformAngle(-45.f);
	UBorder* CheckLong = MakeRelocPart(Tree, RelocRounded(RelocOk(), 3.f));
	PlaceReloc(OkGroup, CheckLong, FVector2D(118.f, 67.f), FVector2D(6.f, 40.f));
	CheckLong->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	CheckLong->SetRenderTransformAngle(45.f);
	OkGroup->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	// ③ 글씨 박스 — 제목 + 보조 문구.
	TextBox = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	TextBox->SetPadding(FMargin(16.f, 12.f, 16.f, 13.f));
	TextBox->SetHorizontalAlignment(HAlign_Center);
	TextBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 긴 문구(90초 단계)가 화면 끝까지 늘어나지 않게 시안 여백(좌우 18px)만큼 폭을 묶고 줄바꿈한다.
	USizeBox* TextMaxWidth = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	TextMaxWidth->SetMaxDesiredWidth(kRelocDesignW - 36.f);
	TextMaxWidth->SetVisibility(ESlateVisibility::HitTestInvisible);
	TextMaxWidth->SetContent(TextBox);
	if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(TextMaxWidth))
	{
		BoxSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UVerticalBox* Lines = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Lines->SetVisibility(ESlateVisibility::HitTestInvisible);
	TextBox->SetContent(Lines);

	// 글자 크기: 시안 16.5px·12.5px → Slate pt(×0.75). 설계 공간째 늘어나므로 폰에서도 시안 비율이다.
	TitleText = MakeRelocText(Tree, 12, RelocWhite());
	if (UVerticalBoxSlot* TitleSlot = Lines->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}
	SubText = MakeRelocText(Tree, 10, RelocSky());
	// 박스 안쪽 폭(284 − 좌우 패딩 32)에서 고정 줄바꿈. AutoWrap 은 첫 프레임에 폭을 몰라 짧은 문구도 꺾였다가 펴진다.
	SubText->SetWrapTextAt(kRelocDesignW - 36.f - 32.f);
	if (UVerticalBoxSlot* SubSlot = Lines->AddChildToVerticalBox(SubText))
	{
		SubSlot->SetHorizontalAlignment(HAlign_Center);
		SubSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}

	ApplyStyle(false);
}

void UNavRelocalizeOverlay::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Visible);   // 떠 있는 동안 터치를 먹는다(통과 금지 — 런북 §B-3).
}

void UNavRelocalizeOverlay::SetAlphas(float InDimAlpha, float InTextBoxAlpha)
{
	DimAlpha = FMath::Clamp(InDimAlpha, 0.f, 1.f);
	TextBoxAlpha = FMath::Clamp(InTextBoxAlpha, 0.f, 1.f);
	ApplyStyle(bRecovered);
}

void UNavRelocalizeOverlay::ShowScanning(const FString& InReason)
{
	Reason = InReason;
	Stage = 0;
	bRecovered = false;
	bFinished = false;
	Clock = 0.f;
	AppliedPhoneAlpha = -1.f;
	SetRenderOpacity(1.f);
	SetVisibility(ESlateVisibility::Visible);
	ApplyStyle(false);
	if (SubText != nullptr)
	{
		SubText->SetText(FText::FromString(StageSubText(0, Reason)));
	}
	TickScan(0.f);
}

void UNavRelocalizeOverlay::SetStageText(int32 InStage)
{
	Stage = InStage;
	if (!bRecovered && SubText != nullptr)
	{
		SubText->SetText(FText::FromString(StageSubText(Stage, Reason)));
	}
}

void UNavRelocalizeOverlay::ShowRecovered()
{
	bRecovered = true;
	Clock = 0.f;
	ApplyStyle(true);
	TickRecovered(0.f);
}

void UNavRelocalizeOverlay::ApplyStyle(bool bRecoveredStyle)
{
	if (Dim != nullptr)
	{
		Dim->SetBrushColor(bRecoveredStyle ? RelocSrgb(13, 51, 26, DimAlpha) : RelocSrgb(10, 18, 32, DimAlpha));
	}
	if (ScanGroup != nullptr)
	{
		ScanGroup->SetVisibility(bRecoveredStyle ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (OkGroup != nullptr)
	{
		OkGroup->SetVisibility(bRecoveredStyle ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TitleText != nullptr)
	{
		TitleText->SetText(FText::FromString(bRecoveredStyle ? kRelocTitleOk : kRelocTitleScan));
		TitleText->SetColorAndOpacity(FSlateColor(bRecoveredStyle ? RelocOk() : RelocWhite()));
		FSlateFontInfo Font = TitleText->GetFont();
		Font.Size = bRecoveredStyle ? 13 : 12;
		TitleText->SetFont(Font);
	}
	if (SubText != nullptr)
	{
		SubText->SetVisibility(bRecoveredStyle ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	SetOutlinedAlpha(1.f, bRecoveredStyle ? 0.f : 1.f);
}

void UNavRelocalizeOverlay::SetOutlinedAlpha(float TextBoxOutlineAlpha, float RingAlpha)
{
	if (TextBox != nullptr)
	{
		TextBox->SetBrush(bRecovered
			? RelocRounded(RelocSrgb(8, 40, 22, FMath::Max(TextBoxAlpha, 0.70f)), 14.f, RelocOk(0.45f * TextBoxOutlineAlpha), 1.f)
			: RelocRounded(RelocSrgb(8, 20, 44, TextBoxAlpha), 14.f, RelocSky(0.35f * TextBoxOutlineAlpha), 1.f));
	}
	if (OkRing != nullptr)
	{
		OkRing->SetBrush(RelocRounded(FLinearColor::Transparent, 48.f, RelocOk(RingAlpha), 4.f));
	}
}

void UNavRelocalizeOverlay::SetPhoneAlpha(float Alpha)
{
	if (PhoneBody == nullptr || FMath::Abs(Alpha - AppliedPhoneAlpha) < 0.01f)
	{
		return;
	}
	AppliedPhoneAlpha = Alpha;
	PhoneBody->SetBrush(RelocRounded(RelocWhite(0.06f), 9.f, RelocWhite(Alpha), 3.f));
}

void UNavRelocalizeOverlay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bFinished)
	{
		return;
	}
	Clock += InDeltaTime;
	if (bRecovered)
	{
		TickRecovered(Clock);
	}
	else
	{
		TickScan(FMath::Fmod(Clock, ScanCycleSeconds));
	}
}

void UNavRelocalizeOverlay::TickScan(float CycleTime)
{
	if (PhoneGroup == nullptr)
	{
		return;
	}
	const FNavRelocScanPose Pose = EvalScanPose(CycleTime);
	PhoneGroup->SetRenderTranslation(FVector2D(Pose.X, 0.f));
	PhoneGroup->SetRenderTransformAngle(Pose.AngleDeg);
	PhoneGroup->SetRenderOpacity(Pose.Opacity);
	SetPhoneAlpha(Pose.Opacity);   // 외곽선은 렌더 불투명도를 안 따른다(헤더 ⚠️)

	// 아이콘 i 는 폰 가운데가 자기 x 를 지나는 순간 켜지고(1.5 → 1.15), 복귀가 끝나는 3.35초에 전부 꺼진다.
	for (int32 i = 0; i < Icons.Num(); ++i)
	{
		const float LitAt = kRelocScanStart + (kRelocIconX[i] - 73.f) / 74.f * (kRelocScanEnd - kRelocScanStart);
		const float Age = CycleTime - LitAt;
		const bool bLit = Age >= 0.f && CycleTime < kRelocReturnEnd;
		const float U = FMath::Clamp(Age / kRelocIconFlashSeconds, 0.f, 1.f);
		const float IconScale = bLit ? FMath::Lerp(1.5f, 1.15f, U) : 1.f;
		Icons[i]->SetRenderOpacity(bLit ? FMath::Lerp(1.f, 0.85f, U) : 0.18f);
		Icons[i]->SetRenderScale(FVector2D(IconScale, IconScale));
	}

	// 속도선: 복귀 구간 앞 30% 에 나타나며 12px, 나머지 70% 에 사라지며 64px 뒤로.
	const float W = (CycleTime - kRelocReturnStart) / (kRelocReturnEnd - kRelocReturnStart);
	float LineOpacity = 0.f;
	float LineShift = 0.f;
	if (W >= 0.f && W <= 1.f)
	{
		if (W < 0.3f)
		{
			LineOpacity = FMath::Lerp(0.f, 0.9f, W / 0.3f);
			LineShift = FMath::Lerp(0.f, -12.f, W / 0.3f);
		}
		else
		{
			LineOpacity = FMath::Lerp(0.9f, 0.f, (W - 0.3f) / 0.7f);
			LineShift = FMath::Lerp(-12.f, -64.f, (W - 0.3f) / 0.7f);
		}
	}
	for (UBorder* Line : SpeedLines)
	{
		Line->SetRenderOpacity(LineOpacity);
		Line->SetRenderTranslation(FVector2D(LineShift, 0.f));
	}
}

void UNavRelocalizeOverlay::TickRecovered(float T)
{
	const float Pop = FMath::Clamp(T / kRelocOkPopSeconds, 0.f, 1.f);
	const float Fade = 1.f - FMath::Clamp((T - kRelocOkHoldSeconds) / kRelocOkFadeSeconds, 0.f, 1.f);
	if (OkGroup != nullptr)
	{
		const float PopScale = 0.6f + 0.4f * RelocEaseOutBack(Pop);
		OkGroup->SetRenderScale(FVector2D(PopScale, PopScale));
		OkGroup->SetRenderOpacity(Pop);
	}
	SetRenderOpacity(Fade);
	SetOutlinedAlpha(Fade, Pop * Fade);

	if (T >= kRelocOkHoldSeconds + kRelocOkFadeSeconds)
	{
		bFinished = true;
		RemoveFromParent();
	}
}

FReply UNavRelocalizeOverlay::NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply UNavRelocalizeOverlay::NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply UNavRelocalizeOverlay::NativeOnTouchStarted(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply UNavRelocalizeOverlay::NativeOnTouchMoved(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply UNavRelocalizeOverlay::NativeOnTouchEnded(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }

// ==================================================================== 서브시스템

bool UNavRelocalizeOverlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

bool UNavRelocalizeOverlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavRelocalizeOverlaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UNavLocalizer* Loc = Collection.InitializeDependency<UNavLocalizer>();
	Localizer = Loc;
	if (Loc != nullptr)
	{
		Loc->OnRelocalizationStarted.AddDynamic(this, &UNavRelocalizeOverlaySubsystem::HandleRelocalizationStarted);
		Loc->OnRelocalized.AddDynamic(this, &UNavRelocalizeOverlaySubsystem::HandleRelocalized);
	}
}

void UNavRelocalizeOverlaySubsystem::Deinitialize()
{
	if (UNavLocalizer* Loc = Localizer.Get())
	{
		Loc->OnRelocalizationStarted.RemoveDynamic(this, &UNavRelocalizeOverlaySubsystem::HandleRelocalizationStarted);
		Loc->OnRelocalized.RemoveDynamic(this, &UNavRelocalizeOverlaySubsystem::HandleRelocalized);
	}
	if (Overlay != nullptr)
	{
		Overlay->RemoveFromParent();
		Overlay = nullptr;
	}
	Super::Deinitialize();
}

TStatId UNavRelocalizeOverlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNavRelocalizeOverlaySubsystem, STATGROUP_Tickables);
}

void UNavRelocalizeOverlaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Overlay == nullptr)
	{
		return;
	}
	if (Overlay->IsFinished())
	{
		Overlay = nullptr;   // 페이드까지 끝나 스스로 내려갔다.
		return;
	}
	const UNavLocalizer* Loc = Localizer.Get();
	if (Loc == nullptr)
	{
		return;
	}

	if (Loc->IsRelocalizing())
	{
		// 오래 못 잡으면 문구만 바꾼다(버튼 없음 — 명세 §3.3). 30초 · 90초.
		const float Elapsed = Loc->GetRelocElapsedSeconds();
		const int32 NewStage = Elapsed >= 90.f ? 2 : (Elapsed >= 30.f ? 1 : 0);
		if (NewStage != ShownStage)
		{
			ShownStage = NewStage;
			Overlay->SetStageText(NewStage);
			UE_LOG(LogNav, Log, TEXT("[NavReloc] OVERLAY stage=%d elapsed=%.0fs"), NewStage, Elapsed);
		}
	}
	else if (Loc->GetLocState() == ENavLocState::Localized && !Overlay->IsShowingRecovered())
	{
		// 복구 없이 측위가 버려졌다(안내 종료 · ResetLocalization) — ✓ 없이 조용히 내린다.
		UE_LOG(LogNav, Log, TEXT("[NavReloc] OVERLAY removed (측위 초기화)"));
		Overlay->RemoveFromParent();
		Overlay = nullptr;
	}
}

void UNavRelocalizeOverlaySubsystem::HandleRelocalizationStarted(const FString& Reason)
{
	UWorld* World = GetWorld();
	const UNavLocalizer* Loc = Localizer.Get();
	if (World == nullptr || Loc == nullptr)
	{
		return;
	}
	if (Overlay == nullptr || Overlay->IsFinished())
	{
		Overlay = CreateWidget<UNavRelocalizeOverlay>(World, UNavRelocalizeOverlay::StaticClass());
		if (Overlay == nullptr)
		{
			UE_LOG(LogNav, Warning, TEXT("[NavReloc] 오버레이 위젯 생성 실패"));
			return;
		}
		Overlay->SetAlphas(Loc->RelocDimAlpha, Loc->RelocTextBoxAlpha);
		Overlay->AddToViewport(260);   // 토스트 250 위 · 관리자 오버레이 300 아래
	}
	ShownStage = 0;
	Overlay->ShowScanning(Reason);
	UE_LOG(LogNav, Log, TEXT("[NavReloc] OVERLAY scan reason=%s"), *Reason);
}

void UNavRelocalizeOverlaySubsystem::HandleRelocalized(const FString& SourceCode)
{
	if (Overlay != nullptr && !Overlay->IsFinished())
	{
		Overlay->ShowRecovered();
		UE_LOG(LogNav, Log, TEXT("[NavReloc] OVERLAY recovered via=%s"), *SourceCode);
	}
}
