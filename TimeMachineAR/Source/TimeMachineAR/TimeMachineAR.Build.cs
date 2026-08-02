// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimeMachineAR : ModuleRules
{
	public TimeMachineAR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// HTTP / Json : AI 도슨트 서버(FastAPI) 연동. ADR-001 에 따라 C++ 코어서버를
		// 거치지 않고 UE5 가 백엔드에 직접 HTTP 로 붙는다.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AugmentedReality", "AndroidPermission", "HTTP", "Json", "JsonUtilities" });
	}
}
