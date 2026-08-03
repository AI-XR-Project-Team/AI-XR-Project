#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DocentQuickChip.generated.h"

class UButton;
class UTextBlock;

/** 칩이 눌렸을 때 담고 있던 질문을 넘긴다. 위젯 내부 연결용이라 다이나믹이 아니다. */
DECLARE_DELEGATE_OneParam(FOnQuickChipClicked, const FString& /*Question*/);

/**
 * 빠른 질문 칩 하나.
 *
 * 모바일에서 한글 입력이 번거로워, 탭 한 번으로 대화를 시작할 수 있게 한다.
 *
 * WBP 로 상속해 모양만 잡는다. 필요한 자식 위젯:
 *   - ChipButton (Button)
 *   - ChipText   (Text Block)
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UDocentQuickChip : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 칩에 표시할 질문. 표시 문구와 실제 전송 문구가 같다. */
	void SetQuestion(const FString& InQuestion);

	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	FString GetQuestion() const { return Question; }

	/** 칩을 누르면 호출된다. 챗 위젯이 여기에 자기 핸들러를 꽂는다. */
	FOnQuickChipClicked OnClicked;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Docent|Chat")
	TObjectPtr<UButton> ChipButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Docent|Chat")
	TObjectPtr<UTextBlock> ChipText;

private:
	/** UButton::OnClicked 는 다이나믹 델리게이트라 UFUNCTION 이어야 한다. */
	UFUNCTION()
	void HandleButtonClicked();

	FString Question;
};
