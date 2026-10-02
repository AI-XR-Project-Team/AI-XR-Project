#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoInfoData.h"
#include "DinoStatTile.generated.h"

class UImage;
class UTextBlock;

/**
 * 정보 카드 아래쪽 요약 타일 하나 (크기 / 무게 / 식성 / 기간).
 *
 * WBP 로 상속해 모양만 잡는다. 필요한 자식 위젯:
 *   - StatLabel (Text Block) : 제목
 *   - StatValue (Text Block) : 값
 *   - StatSub   (Text Block) : 값 아래 작은 글씨
 *   - StatIcon  (Image)      : 제목 위 아이콘 (선택)
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UDinoStatTile : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 타일에 표시할 내용. 카드가 타일을 만든 직후 부른다. */
	void SetStat(const FDinoStat& InStat);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Dino|Card")
	TObjectPtr<UTextBlock> StatLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Dino|Card")
	TObjectPtr<UTextBlock> StatValue;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UTextBlock> StatSub;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UImage> StatIcon;

	/**
	 * FDinoStat::IconTint 를 아이콘에 입힐지. 기본 false — 기존 WBP 타일은 자기 색을
	 * 유지한다. 흰 마스크 아이콘을 쓰는 바다 스킨 타일만 켠다.
	 */
	bool bApplyIconTint = false;

private:
	/** SetStat 이 NativeConstruct 보다 먼저 올 수 있어 값을 들고 있다가 다시 칠한다. */
	void Apply();

	FDinoStat Stat;
};
