// UNavRouteProgress 단위 테스트 (spec §4) — 전진·후퇴·이탈·도착 4케이스.
//
// 순수 계산 클래스라 PIE 없이 검증한다. 에디터에서
//   Session Frontend > Automation > "TimeMachineAR.Nav.RouteProgress" 실행,
// 또는 커맨드라인:
//   UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests TimeMachineAR.Nav.RouteProgress; Quit"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavRouteProgress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavRouteProgressTest,
	"TimeMachineAR.Nav.RouteProgress",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavRouteProgressTest::RunTest(const FString& Parameters)
{
	// ㄱ자 경로: (0,0) → (0,1000) → (500,1000). 총거리 1500cm.
	UNavRouteProgress* P = NewObject<UNavRouteProgress>();
	P->DrawSmoothingAlpha = 1.f;   // 평활 끄고 결정적으로.
	TArray<FVector2D> Pts = { FVector2D(0, 0), FVector2D(0, 1000), FVector2D(500, 1000) };
	P->SetRoutePoints(Pts);

	// (1) 시작 시점 RemainingCm 이 총거리와 일치한다.
	{
		const FNavProgress R = P->UpdatePose(FVector2D(0, 0));
		TestEqual(TEXT("시작 세그먼트=0"), R.SegmentIndex, 0);
		TestTrue(TEXT("시작 남은거리≈총거리(1500)"), FMath::IsNearlyEqual(R.RemainingCm, 1500.f, 1.f));
		TestFalse(TEXT("시작은 이탈 아님"), R.bOffRoute);
	}

	// (2) 전진 — 세그먼트 중간에서 s,t 와 남은거리.
	{
		const FNavProgress R = P->UpdatePose(FVector2D(0, 600));
		TestEqual(TEXT("전진 세그먼트=0"), R.SegmentIndex, 0);
		TestTrue(TEXT("t≈0.6"), FMath::IsNearlyEqual(R.SegmentT, 0.6f, 0.01f));
		TestTrue(TEXT("남은거리≈900"), FMath::IsNearlyEqual(R.RemainingCm, 900.f, 1.f));
	}
	{
		const FNavProgress R = P->UpdatePose(FVector2D(300, 1000));
		TestEqual(TEXT("두번째 세그먼트=1"), R.SegmentIndex, 1);
		TestTrue(TEXT("남은거리≈200"), FMath::IsNearlyEqual(R.RemainingCm, 200.f, 1.f));
	}

	// (3) 후퇴 — s 가 줄고, 그리기 폴리라인에 이전 노드가 다시 낀다.
	{
		const FNavProgress R = P->UpdatePose(FVector2D(0, 400));
		TestEqual(TEXT("후퇴 세그먼트=0"), R.SegmentIndex, 0);
		TestTrue(TEXT("후퇴 남은거리≈1100"), FMath::IsNearlyEqual(R.RemainingCm, 1100.f, 1.f));

		TArray<FVector2D> Draw;
		P->GetDrawPolyline(Draw);
		// [내 위치, (0,1000), (500,1000)] — 지나쳤던 노드 (0,1000)이 다시 낀다.
		TestEqual(TEXT("그리기 폴리라인 점 3개"), Draw.Num(), 3);
		if (Draw.Num() == 3)
		{
			TestTrue(TEXT("두번째 점이 (0,1000)"), Draw[1].Equals(FVector2D(0, 1000), 1.0));
		}
	}

	// (4) 이탈 — 옆으로 300cm(>200) 벗어나면 bOffRoute.
	{
		const FNavProgress R = P->UpdatePose(FVector2D(300, 400));
		TestTrue(TEXT("이탈 lateral≈300"), FMath::IsNearlyEqual(R.LateralOffsetCm, 300.f, 1.f));
		TestTrue(TEXT("bOffRoute 선다"), R.bOffRoute);
	}
	// 경로로 복귀하면 이탈이 풀린다(판정용은 원본 위치).
	{
		const FNavProgress R = P->UpdatePose(FVector2D(0, 500));
		TestFalse(TEXT("복귀하면 이탈 해제"), R.bOffRoute);
	}

	// (5) 도착 — 목적지 근처에서 bArrived.
	{
		const FNavProgress R = P->UpdatePose(FVector2D(480, 1000));
		TestEqual(TEXT("도착 세그먼트=1"), R.SegmentIndex, 1);
		TestTrue(TEXT("남은거리 작음(≤80)"), R.RemainingCm <= 80.f);
		TestTrue(TEXT("bArrived 선다"), R.bArrived);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
