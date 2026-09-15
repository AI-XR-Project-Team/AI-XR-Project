// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeMachineARGameMode.h"
#include "TimeMachineARCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATimeMachineARGameMode::ATimeMachineARGameMode()
	: Super()
{
	// DefaultPawnClass can be set in a Blueprint derived from this class
	// or assigned dynamically. The hardcoded FirstPerson template pawn was removed.
}
