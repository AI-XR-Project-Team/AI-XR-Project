#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoInfoData.h"
#include "DinoStatTile.generated.h"

class UTextBlock;

/**
 * 정보 카드 아래쪽 요약 타일 하나 (크기 / 무게 / 식성 / 기간).
 *
 * WBP 로 상속해 모양만 잡는다. 필요한 자식 위젯:
 *   - StatLabel (Text Block) : 제목
 *   - StatValue (Text Block) : 값
 *   - StatSub   (Text Block) : 값 아래 작은 글씨
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

private:
	/** SetStat 이 NativeConstruct 보다 먼저 올 수 있어 값을 들고 있다가 다시 칠한다. */
	void Apply();

	FDinoStat Stat;
};
