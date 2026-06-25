// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MechRaid : ModuleRules
{
	public MechRaid(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"CommonUI",
			"CommonInput"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"MechRaid",
			"MechRaid/Variant_Horror",
			"MechRaid/Variant_Horror/UI",
			"MechRaid/Variant_Shooter",
			"MechRaid/Variant_Shooter/AI",
			"MechRaid/Variant_Shooter/UI",
			"MechRaid/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
