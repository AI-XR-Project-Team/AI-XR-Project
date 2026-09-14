#pragma once

#include "CoreMinimal.h"
#include "DinoStatTile.h"
#include "DinoTabButton.h"
#include "DinoOceanWidgets.generated.h"

class UFont;
class UTexture2D;

/**
 * 바다(아르켈론) 스킨의 공통 팔레트·폰트·텍스처 로더.
 *
 * 시안에서 눈으로 읽은 근사값이다. 실제 픽셀 추출값이 아니다. 카드·탭·타일이 같은
 * 값을 써야 한 화면처럼 보이므로 한 곳에 모은다.
 */
namespace DinoOcean
{
	/** /Game/UI/DinoCard/ArchelonSkin 의 텍스처. 없으면 nullptr — 호출부가 건너뛴다. */
	TIMEMACHINEAR_API UTexture2D* Tex(const TCHAR* Name);

	/** 프로젝트 한국어 폰트(Pretendard). Typeface 는 "Regular" 또는 "SemiBold". */
	TIMEMACHINEAR_API FSlateFontInfo Font(int32 Size, bool bSemiBold = false);

	inline FLinearColor SRGB(uint8 R, uint8 G, uint8 B, uint8 A = 255)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B, A));
	}

	inline FLinearColor Background() { return SRGB(6, 21, 35); }          // #061523
	inline FLinearColor Panel()      { return SRGB(10, 32, 52, 200); }     // #0A2034 반투명
	inline FLinearColor PanelHi()    { return SRGB(16, 44, 70, 220); }
	inline FLinearColor Accent()     { return SRGB(80, 218, 255); }        // #50DAFF
	inline FLinearColor AccentDim()  { return SRGB(80, 218, 255, 90); }
	inline FLinearColor Text()       { return SRGB(232, 243, 252); }       // #E8F3FC
	inline FLinearColor TextDim()    { return SRGB(177, 198, 219); }       // #B1C6DB
	inline FLinearColor Orange()     { return SRGB(255, 180, 77); }        // #FFB44D
	inline FLinearColor Outline()    { return SRGB(80, 218, 255, 70); }
}

/**
 * 바다 스킨의 탭 버튼. WBP 없이 C++ 로 트리를 만든다.
 *
 * 네 탭 모두 아이콘을 보이고, 선택 탭만 청록 아이콘·밝은 글자·발광 밑줄로 강조한다.
 * 첫 탭이 아니면 왼쪽에 가는 구분선을 둔다(시안의 탭 바).
 */
UCLASS()
class TIMEMACHINEAR_API UDinoOceanTabButton : public UDinoTabButton
{
	GENERATED_BODY()

public:
	UDinoOceanTabButton(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void OnApplied() override;

private:
	/** WidgetTree 에 위젯을 만들고 TabButton/TabLabel/TabIcon/SelectedMark 를 잇는다. */
	void BuildTree();

	UPROPERTY()
	TObjectPtr<UImage> Divider;
};

/**
 * 바다 스킨의 요약 타일. 둥근 남색 유리 + 색 입힌 마스크 아이콘 + 제목/값/보조.
 */
UCLASS()
class TIMEMACHINEAR_API UDinoOceanStatTile : public UDinoStatTile
{
	GENERATED_BODY()

public:
	UDinoOceanStatTile(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BuildTree();
};
