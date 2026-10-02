// FNavArrivalAutoEnd 단위 테스트(nav-lexi-guide-design.md §4) — Dwell 리셋, Ended→Closed 순서·시간,
// Reset 후 재사용. 순수 상태 헬퍼라 PIE 없이 검증한다. 커맨드라인:
//   UnrealEditor-Cmd -project=<abs.uproject> \
//     -ExecCmds="Automation RunTests TimeMachineAR.Nav.ArrivalAutoEnd; Quit" -unattended -nullrhi -nosplash

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../NavArrivalAutoEnd.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavArrivalAutoEndTest,
	"TimeMachineAR.Nav.ArrivalAutoEnd",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavArrivalAutoEndTest::RunTest(const FString& Parameters)
{
	using EEvent = FNavArrivalAutoEnd::EEvent;

	// (1) 도착 전에는 아무 일도 없다.
	{
		FNavArrivalAutoEnd AutoEnd;
		AutoEnd.DwellSec = 4.f;
		AutoEnd.EndMessageSec = 2.5f;
		TestTrue(TEXT("미도착: None"), AutoEnd.Update(false, 1.f) == EEvent::None);
		TestTrue(TEXT("미도착 반복: None"), AutoEnd.Update(false, 10.f) == EEvent::None);
	}

	// (2) 도착 직후 Dwell 이 덜 찼으면 None, 다 차는 프레임에 정확히 한 번 Ended.
	{
		FNavArrivalAutoEnd AutoEnd;
		AutoEnd.DwellSec = 4.f;
		AutoEnd.EndMessageSec = 2.5f;
		TestTrue(TEXT("도착 1초: None"), AutoEnd.Update(true, 1.f) == EEvent::None);
		TestTrue(TEXT("도착 2초 더(누적3): None"), AutoEnd.Update(true, 2.f) == EEvent::None);
		TestTrue(TEXT("도착 0.9초 더(누적3.9): None"), AutoEnd.Update(true, 0.9f) == EEvent::None);
		TestTrue(TEXT("도착 0.2초 더(누적4.1): Ended"), AutoEnd.Update(true, 0.2f) == EEvent::Ended);
		// Ended 는 한 번만. 같은 틱 이후 다시 불러도 Ended 가 또 나오면 안 된다.
		TestTrue(TEXT("Ended 다음 틱: Ended 아님(None)"), AutoEnd.Update(true, 0.1f) == EEvent::None);
	}

	// (3) Ended → EndMessageSec 뒤 정확히 한 번 Closed. 그 뒤로는 계속 None(Reset 전까지).
	{
		FNavArrivalAutoEnd AutoEnd;
		AutoEnd.DwellSec = 4.f;
		AutoEnd.EndMessageSec = 2.5f;
		TestTrue(TEXT("Dwell 채움: Ended"), AutoEnd.Update(true, 4.f) == EEvent::Ended);
		TestTrue(TEXT("EndMessage 2초: None"), AutoEnd.Update(true, 2.f) == EEvent::None);
		TestTrue(TEXT("EndMessage 0.4초 더(누적2.4): None"), AutoEnd.Update(true, 0.4f) == EEvent::None);
		TestTrue(TEXT("EndMessage 0.2초 더(누적2.6): Closed"), AutoEnd.Update(true, 0.2f) == EEvent::Closed);
		TestTrue(TEXT("Closed 이후: None"), AutoEnd.Update(true, 100.f) == EEvent::None);
		TestTrue(TEXT("Closed 이후(미도착이어도): None"), AutoEnd.Update(false, 100.f) == EEvent::None);
	}

	// (4) Ended 전에 bArrived 가 false 로 돌아오면 Dwell 이 리셋된다(처음부터 다시 잰다).
	{
		FNavArrivalAutoEnd AutoEnd;
		AutoEnd.DwellSec = 4.f;
		AutoEnd.EndMessageSec = 2.5f;
		TestTrue(TEXT("도착 3초: None(아직 미달)"), AutoEnd.Update(true, 3.f) == EEvent::None);
		TestTrue(TEXT("해제 히스테리시스 밖으로: 리셋(None)"), AutoEnd.Update(false, 0.5f) == EEvent::None);
		// 리셋되었으므로 곧장 3초를 더 줘도(누적 6초처럼 보여도) 아직 Ended 가 아니어야 한다.
		TestTrue(TEXT("리셋 후 3초: 아직 None"), AutoEnd.Update(true, 3.f) == EEvent::None);
		TestTrue(TEXT("리셋 후 1초 더(누적4): Ended"), AutoEnd.Update(true, 1.f) == EEvent::Ended);
	}

	// (5) Ended 이후에는 bArrived=false 가 와도 리셋되지 않는다 — EndMessage 카운트다운을 계속한다.
	{
		FNavArrivalAutoEnd AutoEnd;
		AutoEnd.DwellSec = 4.f;
		AutoEnd.EndMessageSec = 2.5f;
		TestTrue(TEXT("Dwell 채움: Ended"), AutoEnd.Update(true, 4.f) == EEvent::Ended);
		TestTrue(TEXT("Ended 이후 미도착: None(리셋 아님)"), AutoEnd.Update(false, 1.f) == EEvent::None);
		TestTrue(TEXT("나머지 EndMessage 소진: Closed"), AutoEnd.Update(false, 1.5f) == EEvent::Closed);
	}

	// (6) Reset() 이후 처음부터 다시 사이클을 돌 수 있다.
	{
		FNavArrivalAutoEnd AutoEnd;
		AutoEnd.DwellSec = 4.f;
		AutoEnd.EndMessageSec = 2.5f;
		TestTrue(TEXT("1차 Ended"), AutoEnd.Update(true, 4.f) == EEvent::Ended);
		TestTrue(TEXT("1차 Closed"), AutoEnd.Update(true, 2.5f) == EEvent::Closed);
		AutoEnd.Reset();
		TestTrue(TEXT("Reset 직후: None"), AutoEnd.Update(false, 1.f) == EEvent::None);
		TestTrue(TEXT("2차 Ended(재사용 가능)"), AutoEnd.Update(true, 4.f) == EEvent::Ended);
		TestTrue(TEXT("2차 Closed"), AutoEnd.Update(true, 2.5f) == EEvent::Closed);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
