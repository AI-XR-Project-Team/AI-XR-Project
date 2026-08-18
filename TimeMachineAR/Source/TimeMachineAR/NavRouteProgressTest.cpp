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

// ---------------------------------------------------------------------- 턴바이턴 안내(5-D)
//
// GetGuidance 가 서버 steps 를 "지금 어느 step 인가" 로 매핑하는지 검증한다.
// 핵심: step 수 ≠ 웨이포인트 수(서버가 30° 미만 꺾임을 직진으로 합침) → 세그먼트 s 를
// 곧바로 steps[s] 로 쓰면 어긋난다(spec §3.5).

namespace
{
	FNavStep MakeStraight(float DistCm)
	{
		FNavStep S;
		S.Instruction = FString::Printf(TEXT("%dm 직진"), FMath::RoundToInt(DistCm / 100.f));
		S.DistanceCm = DistCm;
		S.bHasDistance = true;
		S.Turn = TEXT("straight");
		S.bArrive = false;
		return S;
	}
	FNavStep MakeTurn(const FString& Dir)
	{
		FNavStep S;
		S.Instruction = (Dir == TEXT("right")) ? TEXT("우회전") : TEXT("좌회전");
		S.bHasDistance = false;
		S.Turn = Dir;
		S.bArrive = false;
		return S;
	}
	FNavStep MakeArrive()
	{
		FNavStep S;
		S.Instruction = TEXT("도착");
		S.bHasDistance = false;
		S.Turn = TEXT("");
		S.bArrive = true;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavGuidanceTest,
	"TimeMachineAR.Nav.Guidance",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavGuidanceTest::RunTest(const FString& Parameters)
{
	// --- (A) 회전이 있는 ㄱ자 경로: (0,0)→(0,1000)→(500,1000). 총 1500cm ---
	// 서버 steps = [직진1000, 우회전, 직진500, 도착]. StepSpan = [0,1000][1000,1000][1000,1500][1500,1500].
	{
		UNavRouteProgress* P = NewObject<UNavRouteProgress>();
		P->DrawSmoothingAlpha = 1.f;
		TArray<FVector2D> Pts = { FVector2D(0, 0), FVector2D(0, 1000), FVector2D(500, 1000) };
		P->SetRoutePoints(Pts);
		P->SetSteps({ MakeStraight(1000.f), MakeTurn(TEXT("right")), MakeStraight(500.f), MakeArrive() });

		// 첫 직진 구간(seg 0) → step 0. 다음 안내는 우회전.
		P->UpdatePose(FVector2D(0, 600));
		{
			const FNavGuidance G = P->GetGuidance();
			TestTrue(TEXT("A: 안내 유효"), G.bValid);
			TestEqual(TEXT("A: seg0 → step 0"), G.StepIndex, 0);
			TestTrue(TEXT("A: step 남은거리≈400"), FMath::IsNearlyEqual(G.StepRemainingCm, 400.f, 1.f));
			TestEqual(TEXT("A: 다음 회전=우회전"), G.NextTurn, FString(TEXT("right")));
		}

		// 회전 뒤 두번째 직진(seg 1) → step 2(회전 step 은 현재로 잡히지 않는다).
		P->UpdatePose(FVector2D(300, 1000));
		{
			const FNavGuidance G = P->GetGuidance();
			TestEqual(TEXT("A: seg1 → step 2"), G.StepIndex, 2);
			TestTrue(TEXT("A: step 남은거리≈200"), FMath::IsNearlyEqual(G.StepRemainingCm, 200.f, 1.f));
			TestTrue(TEXT("A: step 남은거리 음수 아님"), G.StepRemainingCm >= 0.f);
		}

		// 도착 지점/직후 — 마지막(도착) step, 남은거리 0, bArrived 유지.
		P->UpdatePose(FVector2D(500, 1000));
		{
			const FNavGuidance G = P->GetGuidance();
			TestEqual(TEXT("A: 도착 → 마지막 step 3"), G.StepIndex, 3);
			TestTrue(TEXT("A: 도착 step 남은거리 음수 아님"), G.StepRemainingCm >= 0.f);
			TestTrue(TEXT("A: bArrived 유지"), G.bArrived);
		}
		// 도착점을 살짝 지나쳐도 bArrived 가 떨지 않는다.
		P->UpdatePose(FVector2D(505, 1000));
		{
			const FNavGuidance G = P->GetGuidance();
			TestTrue(TEXT("A: 도착 직후에도 bArrived"), G.bArrived);
		}
	}

	// --- (B) 30° 미만 꺾임이 직진으로 합쳐진 경로 ---
	// 웨이포인트 3개(세그먼트 2개)지만 서버 steps 는 [직진2000, 도착] 하나로 합침.
	// 두 세그먼트 모두 같은 step 0 으로 매핑돼야 한다("s 를 그대로 쓰면 어긋난다"의 검증).
	{
		UNavRouteProgress* P = NewObject<UNavRouteProgress>();
		P->DrawSmoothingAlpha = 1.f;
		TArray<FVector2D> Pts = { FVector2D(0, 0), FVector2D(0, 1000), FVector2D(0, 2000) };
		P->SetRoutePoints(Pts);
		P->SetSteps({ MakeStraight(2000.f), MakeArrive() });

		P->UpdatePose(FVector2D(0, 600));    // seg 0
		TestEqual(TEXT("B: seg0 → step 0"), P->GetGuidance().StepIndex, 0);

		P->UpdatePose(FVector2D(0, 1500));   // seg 1 — 여전히 같은 직진 step
		TestEqual(TEXT("B: seg1 → 여전히 step 0"), P->GetGuidance().StepIndex, 0);
	}

	// --- (C) steps 가 없으면 안내는 무효(경로만 있고 배너 없음) ---
	{
		UNavRouteProgress* P = NewObject<UNavRouteProgress>();
		TArray<FVector2D> Pts = { FVector2D(0, 0), FVector2D(0, 1000) };
		P->SetRoutePoints(Pts);
		P->UpdatePose(FVector2D(0, 300));
		TestFalse(TEXT("C: steps 없으면 안내 무효"), P->GetGuidance().bValid);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
