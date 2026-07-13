// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimeMachineAR : ModuleRules
{
	public TimeMachineAR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
	}
}
