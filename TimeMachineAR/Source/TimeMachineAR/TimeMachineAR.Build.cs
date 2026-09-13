// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
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

		// ── 11단계 관리자 등록 모드 (D10·D11) ────────────────────────────────────
		// 이 상수를 true 로 둔 로컬 빌드에서만 Cloud Anchor 등록/호스팅/검증 코드
		// (NavCloudAnchor*) 가 컴파일·링크된다. 방문객 빌드는 이 상수를 false 로 두거나,
		// 더 근본적으로 이 브랜치(feature/ue-nav-stage11)를 develop 에 머지하지 않으므로
		// (D10) develop 의 Build.cs 엔 이 플래그가 아예 없어 관리자 코드를 구조적으로
		// 포함할 수 없다. 12단계에서 리졸브 코드를 옮길 때는 #if NAV_ADMIN_MODE 밖으로 꺼낸다.
		// 설계: docs/specs/feature-ue-nav-stage11.md §2(D11) · docs/nav-stage11-admin-mode.md §1
		//
		// ⚠️ Android 타깃에만 건다: ARCore Cloud Anchors 는 Android 전용이고, 그 의존
		// 모듈(GoogleARCoreSDK)이 Mac 에디터 타깃엔 안 올라와 무조건 걸면 에디터 빌드
		// (BuildCookRun 1단계)가 깨진다. 관리자 앱도 실기기(Android)에서만 돌리므로
		// Mac 에디터에선 NAV_ADMIN_MODE 가 정의되지 않아 NavCloudAnchor* 전체가 비게 된다.
		// ── 12단계 §E: Cloud Anchors 플러그인 의존을 관리자 게이트 **밖으로** 꺼낸다 ──
		// 방문객(일반모드) 빌드도 리졸브(NavCloudResolver)를 하므로 bNavAdminMode 와 무관하게
		// 건다. Android 조건은 그대로 유지한다(위 ⚠️ — Mac 에디터 타깃엔 GoogleARCoreSDK 가
		// 없어 무조건 걸면 BuildCookRun 1단계가 깨진다).
		// NAV_CLOUD_RESOLVE 는 이 의존과 1:1 로 붙는 정의다 — NavCloudResolver.cpp 의 플러그인
		// 호출은 전부 이 안에 있고, 정의가 없는 타깃에선 클래스 껍데기만 남는다.
		bool bCloudPlugin = (Target.Platform == UnrealTargetPlatform.Android);
		if (bCloudPlugin)
		{
			// Cloud Anchors 호스팅/리졸브 API(UCloudARPin, CreateAndResolveCloudARPin).
			PublicDependencyModuleNames.Add("GoogleARCoreServices");
		}
		// ⚠️ 엔진이 -Wundef -Werror 로 컴파일한다 — 정의 없는 매크로를 #if 에 쓰면 **에러**다.
		// 그래서 끄는 쪽도 0 으로 **항상 정의**한다(모든 타깃에서 #if 가 유효해진다).
		PublicDefinitions.Add("NAV_CLOUD_RESOLVE=" + (bCloudPlugin ? "1" : "0"));

		// 11단계 관리자 등록 코드는 **삭제하지 않는다**(D17 — D14/D15 가 무너질 때의 폴백).
		// 12단계 판정은 일반모드(방문객) 빌드에서 하므로 기본값을 false 로 둔다. 관리자 등록이
		// 다시 필요하면 이 한 줄만 true 로 바꿔 Android 로 빌드하면 된다.
		bool bNavAdminMode = false;
		PublicDefinitions.Add("NAV_ADMIN_MODE="
			+ ((bNavAdminMode && bCloudPlugin) ? "1" : "0"));

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// Launch : FJavaWrapper. GameActivity 에 심어 둔 메서드를 JNI 로 부른다.
			PrivateDependencyModuleNames.Add("Launch");

			// GoogleARCoreBase : 전면 카메라 세션 설정(UGoogleARCoreSessionConfig).
			// 플러그인이 uproject 에서 Android 전용이라 다른 플랫폼에는 없다.
			PrivateDependencyModuleNames.Add("GoogleARCoreBase");

			// UPL : 가상 키보드가 가리는 높이를 묻는 메서드를 GameActivity 에 주입한다.
			// UE 가 자체적으로 재는 값은 몰입 모드에서 음수로 나와 쓸 수 없다.
			AdditionalPropertiesForReceipt.Add(
				"AndroidPlugin", Path.Combine(ModuleDirectory, "TimeMachineAR_UPL.xml"));
		}
	}
}
