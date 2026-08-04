// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimeMachineAR : ModuleRules
{
	public TimeMachineAR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// HTTP / Json : AI 도슨트 서버(FastAPI) 연동. ADR-001 에 따라 C++ 코어서버를
		// 거치지 않고 UE5 가 백엔드에 직접 HTTP 로 붙는다.
		// UMG / Slate : 도슨트 챗봇 위젯. 로직은 C++ 에 두고 WBP 는 배치·스타일만 맡는다.
		// ApplicationCore : 안드로이드 가상 키보드 표시/숨김 이벤트(FPlatformRect).
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AugmentedReality", "AndroidPermission", "HTTP", "Json", "JsonUtilities", "UMG", "Slate", "SlateCore", "ApplicationCore" });
	}
}
