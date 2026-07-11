// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeMachineARGameMode.h"
#include "TimeMachineARCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATimeMachineARGameMode::ATimeMachineARGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}
