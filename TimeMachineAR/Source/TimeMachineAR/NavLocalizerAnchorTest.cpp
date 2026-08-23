// UNavLocalizer 앵커 전환·마커 등록 파싱 단위 테스트 (7단계 §A).
//
// 순수 static 로직이라 PIE·AR 세션 없이 검증한다. 에디터에서
//   Session Frontend > Automation > "TimeMachineAR.Nav.LocalizerAnchor" 실행,
// 또는 커맨드라인(헤드리스):
//   UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests TimeMachineAR.Nav.LocalizerAnchor; Quit" -unattended -nop4 -nosplash

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavLocalizer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavLocalizerAnchorTest,
	"TimeMachineAR.Nav.LocalizerAnchor",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavLocalizerAnchorTest::RunTest(const FString& Parameters)
{
	// ─── ParseMarkerEntry: "이름|경로" 파싱 ───
	{
		FString Name, Path;
		TestTrue(TEXT("정상 항목 파싱"),
			UNavLocalizer::ParseMarkerEntry(TEXT("EX3-TREX|/Game/UI/Nav/Markers/T_Marker_EX3.T_Marker_EX3"), Name, Path));
		TestEqual(TEXT("이름 = code"), Name, TEXT("EX3-TREX"));
		TestEqual(TEXT("경로 분리"), Path, TEXT("/Game/UI/Nav/Markers/T_Marker_EX3.T_Marker_EX3"));
	}
	{
		FString Name, Path;
		TestTrue(TEXT("양끝 공백 제거"),
			UNavLocalizer::ParseMarkerEntry(TEXT("  A-1  |  /Game/T  "), Name, Path));
		TestEqual(TEXT("이름 trim"), Name, TEXT("A-1"));
		TestEqual(TEXT("경로 trim"), Path, TEXT("/Game/T"));
	}
	{
		FString Name, Path;
		TestFalse(TEXT("구분자 없음 → 실패"), UNavLocalizer::ParseMarkerEntry(TEXT("noseparator"), Name, Path));
		TestFalse(TEXT("이름 비면 실패"),     UNavLocalizer::ParseMarkerEntry(TEXT("|/Game/T"), Name, Path));
		TestFalse(TEXT("경로 비면 실패"),     UNavLocalizer::ParseMarkerEntry(TEXT("A-1|"), Name, Path));
	}

	// ─── DecideAnchorTransition: 앵커 전환 결정 ───

	// (1) 보이는 known 마커가 없으면 그대로 둔다.
	{
		const FString R = UNavLocalizer::DecideAnchorTransition(TEXT("A"), true, {}, {});
		TestEqual(TEXT("마커 없음 → 전환 없음"), R, FString());
	}

	// (2) 앵커가 추적 불가(멀어져 놓침)이고 다른 마커가 보이면 그쪽으로 재측위.
	//     A 에서 출발 → 걸어가 F 에 도착, A 는 시야 밖.
	{
		const FString R = UNavLocalizer::DecideAnchorTransition(TEXT("A"), false, { TEXT("F") }, { TEXT("A") });
		TestEqual(TEXT("앵커 상실 + 새 마커 → F 로 전환"), R, TEXT("F"));
	}

	// (3) 앵커가 추적 불가인데 같은 마커만 보이면 같은 것으로라도 다시 잡는다.
	{
		const FString R = UNavLocalizer::DecideAnchorTransition(TEXT("A"), false, { TEXT("A") }, {});
		TestEqual(TEXT("앵커 상실 + 같은 마커만 → 같은 것 재고정"), R, TEXT("A"));
	}

	// (4) 앵커가 살아 있고, 다른 마커가 **이번에 새로** 잡히면 그쪽으로 옮긴다(도착=드리프트 보정).
	{
		const FString R = UNavLocalizer::DecideAnchorTransition(
			TEXT("A"), true, { TEXT("A"), TEXT("F") }, { TEXT("A") });
		TestEqual(TEXT("앵커 유지 + 새 마커 F 등장 → F 로 전환"), R, TEXT("F"));
	}

	// (5) 앵커가 살아 있고, 다른 마커가 지난 스캔에도 보였으면(새로 잡힌 게 아님) 옮기지 않는다 → 요동 방지.
	{
		const FString R = UNavLocalizer::DecideAnchorTransition(
			TEXT("A"), true, { TEXT("A"), TEXT("F") }, { TEXT("A"), TEXT("F") });
		TestEqual(TEXT("앵커 유지 + 계속 보이던 마커 → 전환 없음(thrash 방지)"), R, FString());
	}

	// (6) 앵커가 살아 있고 자기 마커만 보이면 전환 없음.
	{
		const FString R = UNavLocalizer::DecideAnchorTransition(
			TEXT("A"), true, { TEXT("A") }, { TEXT("A") });
		TestEqual(TEXT("자기 마커만 → 전환 없음"), R, FString());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
