#include "DinoOceanWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

// ------------------------------------------------------------------ 공통

UTexture2D* DinoOcean::Tex(const TCHAR* Name)
{
	return LoadObject<UTexture2D>(nullptr,
		*FString::Printf(TEXT("/Game/UI/DinoCard/ArchelonSkin/%s.%s"), Name, Name));
}

FSlateFontInfo DinoOcean::Font(int32 Size, bool bSemiBold)
{
	// WBP_DinoInfoCard 와 같은 폰트. 없으면 엔진 기본 폰트로 떨어지는데, 한글이
	// 네모로 나오므로 패키징 누락을 바로 알 수 있다.
	static TWeakObjectPtr<UFont> Cached;
	UFont* FontObject = Cached.Get();
	if (FontObject == nullptr)
	{
		FontObject = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/F_Pretendard.F_Pretendard"));
		Cached = FontObject;
	}
	FSlateFontInfo Info(FontObject, Size, bSemiBold ? FName(TEXT("SemiBold")) : FName(TEXT("Regular")));
	return Info;
}

namespace
{
	/** 새로 만든 UImage 는 아직 Slate 가 없어 SetDesiredSizeOverride 가 무시된다. 브러시 크기로 박는다. */
	void SetImageTexture(UImage* Image, UTexture2D* Texture, const FVector2D& Size, const FLinearColor& Tint)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		// DrawAs 는 늘 Image 로 둔다. 나중에 SetBrushFromTexture 로 텍스처만 갈아 끼우는데,
		// NoDrawType 이 남아 있으면 그때도 안 그려진다. 텍스처가 없는 동안은 접어 둔다.
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Image->SetBrush(Brush);
		Image->SetColorAndOpacity(Tint);
		Image->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

// ------------------------------------------------------------------ 탭 버튼

UDinoOceanTabButton::UDinoOceanTabButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShowIconWhenUnselected = true;
	bTintIcon = true;
	SelectedLabelColor = DinoOcean::Text();
	UnselectedLabelColor = DinoOcean::TextDim();
	SelectedIconTint = DinoOcean::Accent();
	UnselectedIconTint = DinoOcean::TextDim();
}

TSharedRef<SWidget> UDinoOceanTabButton::RebuildWidget()
{
	if (WidgetTree == nullptr)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UDinoOceanTabButton::BuildTree()
{
	// UMG 단위(세로 화면 폭 1080). 시안 탭 바 높이 약 85px × 1.15.
	constexpr float BarHeight = 96.f;
	constexpr float IconSize = 44.f;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// 버튼: 배경은 투명. 눌림만 살짝 밝게.
	TabButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TabButton"));
	{
		FButtonStyle Style = TabButton->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 20.f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.04f), 20.f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.08f), 20.f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		TabButton->SetStyle(Style);
	}
	if (UOverlaySlot* S = Root->AddChildToOverlay(TabButton))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* Height = WidgetTree->ConstructWidget<USizeBox>();
	Height->SetHeightOverride(BarHeight);
	TabButton->SetContent(Height);
	if (UButtonSlot* S = Cast<UButtonSlot>(Height->Slot))
	{
		S->SetPadding(FMargin(0.f));
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Height->SetContent(Row);
	if (USizeBoxSlot* S = Cast<USizeBoxSlot>(Row->Slot))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}

	TabIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TabIcon"));
	SetImageTexture(TabIcon, nullptr, FVector2D(IconSize, IconSize), UnselectedIconTint);
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(TabIcon))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	}

	TabLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TabLabel"));
	TabLabel->SetFont(DinoOcean::Font(26, /*bSemiBold=*/true));
	TabLabel->SetColorAndOpacity(FSlateColor(UnselectedLabelColor));
	TabLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(TabLabel))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 선택 표시: 셀 아래 발광 선. 시트의 T_GlowLine 이 없으면 둥근 청록 막대.
	UImage* Mark = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SelectedMark"));
	if (UTexture2D* Line = DinoOcean::Tex(TEXT("T_GlowLine")))
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Line);
		Brush.ImageSize = FVector2D(200.f, 10.f);
		Mark->SetBrush(Brush);
	}
	else
	{
		Mark->SetBrush(FSlateRoundedBoxBrush(DinoOcean::Accent(), 3.f));
		Mark->SetDesiredSizeOverride(FVector2D(200.f, 6.f));
	}
	Mark->SetVisibility(ESlateVisibility::Hidden);
	if (UOverlaySlot* S = Root->AddChildToOverlay(Mark))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Bottom);
		S->SetPadding(FMargin(18.f, 0.f, 18.f, 2.f));
	}
	SelectedMark = Mark;

	// 구분선: 첫 탭이 아닐 때만. OnApplied 에서 인덱스를 보고 켠다.
	Divider = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Divider"));
	Divider->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.5f, 0.85f, 1.f, 0.18f), 1.f));
	Divider->SetDesiredSizeOverride(FVector2D(2.f, 40.f));
	Divider->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* S = Root->AddChildToOverlay(Divider))
	{
		S->SetHorizontalAlignment(HAlign_Left);
		S->SetVerticalAlignment(VAlign_Center);
	}
}

void UDinoOceanTabButton::OnApplied()
{
	if (Divider != nullptr)
	{
		Divider->SetVisibility(GetTabIndex() > 0
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

// ------------------------------------------------------------------ 요약 타일

UDinoOceanStatTile::UDinoOceanStatTile(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bApplyIconTint = true;
}

TSharedRef<SWidget> UDinoOceanStatTile::RebuildWidget()
{
	if (WidgetTree == nullptr)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UDinoOceanStatTile::BuildTree()
{
	constexpr float IconSize = 62.f;

	// 시안: 둥근 남색 유리 + 가는 청록 테두리. 발광은 테두리 알파로 절제해서 표현한다.
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
	Root->SetBrush(FSlateRoundedBoxBrush(DinoOcean::Panel(), 26.f, DinoOcean::Outline(), 1.5f));
	Root->SetBrushColor(FLinearColor::White);
	Root->SetPadding(FMargin(8.f, 22.f, 8.f, 20.f));
	Root->SetHorizontalAlignment(HAlign_Fill);
	Root->SetVerticalAlignment(VAlign_Fill);
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->SetContent(Column);

	StatIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StatIcon"));
	SetImageTexture(StatIcon, nullptr, FVector2D(IconSize, IconSize), DinoOcean::Orange());
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(StatIcon))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	}

	auto MakeText = [this, Column](const TCHAR* Name, int32 Size, bool bSemiBold, const FLinearColor& Color, float Below) -> UTextBlock*
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetFont(DinoOcean::Font(Size, bSemiBold));
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		Text->SetAutoWrapText(true);
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Text))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetPadding(FMargin(0.f, 0.f, 0.f, Below));
		}
		return Text;
	};

	StatLabel = MakeText(TEXT("StatLabel"), 21, false, DinoOcean::Text(), 6.f);
	StatValue = MakeText(TEXT("StatValue"), 29, true, DinoOcean::Accent(), 8.f);
	StatSub = MakeText(TEXT("StatSub"), 19, false, DinoOcean::TextDim(), 0.f);
}
