#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoTabButton.generated.h"

class UButton;
class UTextBlock;

/** 탭이 눌렸을 때 몇 번째인지 넘긴다. 카드 내부 연결용이라 다이나믹이 아니다. */
DECLARE_DELEGATE_OneParam(FOnDinoTabClicked, int32 /*TabIndex*/);

/**
 * 정보 카드의 탭 버튼 하나 (소개 / 특징 / 서식지 / 발견).
 *
 * WBP 로 상속해 모양만 잡는다. 필요한 자식 위젯:
 *   - TabButton    (Button)     : 필수
 *   - TabLabel     (Text Block) : 필수
 *   - SelectedMark (아무 위젯)   : 선택. 선택됐을 때만 보이는 밑줄
 *
 * 선택/비선택 색은 WBP 에서 바꾼다. C++ 은 SelectedMark 를 켜고 끄는 것과
 * OnSelectionChanged 를 부르는 것까지만 한다. 색까지 코드로 정하면 디자인을
 * 손볼 때마다 재빌드해야 한다.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UDinoTabButton : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 카드가 탭을 만든 직후 부른다. */
	void Setup(int32 InIndex, const FText& InLabel);

	/** 선택 상태를 바꾼다. 카드가 탭을 갈아 끼울 때 이전 탭을 꺼 준다. */
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintPure, Category = "Dino|Card")
	bool IsSelected() const { return bSelected; }

	UFUNCTION(BlueprintPure, Category = "Dino|Card")
	int32 GetTabIndex() const { return TabIndex; }

	/** 탭을 누르면 호출된다. 카드가 여기에 자기 핸들러를 꽂는다. */
	FOnDinoTabClicked OnClicked;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Dino|Card")
	TObjectPtr<UButton> TabButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Dino|Card")
	TObjectPtr<UTextBlock> TabLabel;

	/** 선택된 탭에만 보이는 표시. 보통 글자 아래 파란 밑줄. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Dino|Card")
	TObjectPtr<UWidget> SelectedMark;

	/** 선택 상태가 바뀐 직후. 글자 색을 바꾸는 자리다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Dino|Card")
	void OnSelectionChanged(bool bIsSelected);

private:
	/** UButton::OnClicked 는 다이나믹 델리게이트라 UFUNCTION 이어야 한다. */
	UFUNCTION()
	void HandleButtonClicked();

	/** Setup 이 NativeConstruct 보다 먼저 올 수 있어 값을 들고 있다가 다시 칠한다. */
	void Apply();

	FText Label;
	int32 TabIndex = 0;
	bool bSelected = false;
};
