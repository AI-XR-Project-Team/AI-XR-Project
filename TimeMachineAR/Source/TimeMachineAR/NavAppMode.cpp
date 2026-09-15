// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavAppMode.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* const kNavAppModeSection = TEXT("/Script/TimeMachineAR.NavAppMode");
}

bool NavAppMode::IsDevMode()
{
	bool bDevMode = false;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(kNavAppModeSection, TEXT("bDevMode"), bDevMode, GGameIni);
	}
	return bDevMode;
}
