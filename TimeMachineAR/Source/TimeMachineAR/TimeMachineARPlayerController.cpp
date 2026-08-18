// Copyright Epic Games, Inc. All Rights Reserved.


#include "TimeMachineARPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"

ATimeMachineARPlayerController::ATimeMachineARPlayerController()
{
	// 공룡 오버레이를 탭해 정보 카드를 여는 데 필요하다. 이걸 켜지 않으면
	// 액터의 NotifyActorOnClicked / NotifyActorOnInputTouchBegin 이 아예
	// 호출되지 않는다. 폰은 터치, 에디터 PIE 는 클릭으로 같은 경로를 탄다.
	bEnableClickEvents = true;
	bEnableTouchEvents = true;
}

void ATimeMachineARPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// BP 의 Print String 이 실기기 화면에 초록 글자로 찍힌다. 시연·심사 화면에
	// 나가면 안 되므로 화면 출력만 끈다. UE_LOG/logcat 은 그대로 남는다.
	ConsoleCommand(TEXT("DisableAllScreenMessages"));

	// get the enhanced input subsystem
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		// add the mapping context so we get controls
		Subsystem->AddMappingContext(InputMappingContext, 0);
	}
}