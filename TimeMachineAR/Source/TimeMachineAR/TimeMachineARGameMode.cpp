// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeMachineARGameMode.h"
#include "TimeMachineARCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATimeMachineARGameMode::ATimeMachineARGameMode()
	: Super()
{
	// DefaultPawnClass set in Blueprint or use default
	DefaultPawnClass = APawn::StaticClass();

}
