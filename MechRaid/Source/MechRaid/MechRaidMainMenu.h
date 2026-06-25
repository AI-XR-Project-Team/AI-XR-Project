#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "MechRaidMainMenu.generated.h"

// UCommonButtonBase 클래스가 있다는 것을 임시로 알려줌 (전방 선언)
class UCommonButtonBase;

UCLASS()
class MECHRAID_API UMechRaidMainMenu : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	// 1. 위젯이 화면에 생성될 때 가장 먼저 호출되는 함수 (BeginPlay와 비슷함)
	virtual void NativeConstruct() override;

	// 2. 블루프린트와 연결될 버튼 변수 선언 (이름이 매우 중요합니다!)
	UPROPERTY(meta = (BindWidget))
	UCommonButtonBase* Btn_StartGame;

	// 3. 버튼이 클릭되었을 때 실행할 함수 선언
	// (CommonButton의 클릭 이벤트는 항상 파라미터로 버튼 자기 자신을 넘겨줍니다)
	UFUNCTION()
	void OnStartGameClicked();

	UFUNCTION()
	void OnQuitGameClicked();
};