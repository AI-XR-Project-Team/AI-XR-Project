// FNavFloorGuide·FNavDestinations 바닥 안내 단위 테스트 (9단계 §C-1).
//
// 순수 static 이라 PIE 없이 검증한다. 커맨드라인:
//   UnrealEditor-Cmd -project=<abs.uproject> \
//     -ExecCmds="Automation RunTests TimeMachineAR.Nav.FloorGuide; Quit" -unattended -nullrhi -nosplash

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavFloorGuide.h"
#include "NavDestinations.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavFloorGuideTest,
	"TimeMachineAR.Nav.FloorGuide",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNavFloorGuideTest::RunTest(const FString& Parameters)
{
	auto MakeLine = [](float X0, float X1, int32 N)
	{
		TArray<FVector2D> Pts;
		for (int32 i = 0; i <= N; ++i)
		{
			Pts.Add(FVector2D(FMath::Lerp(X0, X1, (float)i / N), 0.f));
		}
		return Pts;
	};

	TArray<FNavFloorPlacement> Out;

	// (1) 직선(+X) 1000cm, 사용자 시작점, 간격 100·범위 600 → 6개(x=100..600), yaw≈0.
	{
		const TArray<FVector2D> Route = { FVector2D(0, 0), FVector2D(1000, 0) };
		FNavFloorGuide::BuildPlacements(Route, FVector2D(0, 0), 100.f, 600.f, Out);
		TestEqual(TEXT("직선: 6개"), Out.Num(), 6);
		if (Out.Num() == 6)
		{
			TestTrue(TEXT("첫 발자국 x=100"), FMath::IsNearlyEqual(Out[0].MapPos.X, 100.f, 0.5f));
			TestTrue(TEXT("끝 발자국 x=600"), FMath::IsNearlyEqual(Out.Last().MapPos.X, 600.f, 0.5f));
			TestTrue(TEXT("진행 yaw≈0"), FMath::IsNearlyEqual(Out[0].YawDeg, 0.f, 0.5f));
			TestTrue(TEXT("y=0 유지"), FMath::IsNearlyEqual(Out[3].MapPos.Y, 0.f, 0.5f));
		}
	}

	// (2) 사용자가 중간(250)에 있으면 그 앞에서만 낸다(350..850).
	{
		const TArray<FVector2D> Route = { FVector2D(0, 0), FVector2D(1000, 0) };
		FNavFloorGuide::BuildPlacements(Route, FVector2D(250, 0), 100.f, 600.f, Out);
		TestEqual(TEXT("중간 사용자: 6개"), Out.Num(), 6);
		if (Out.Num() >= 1)
		{
			TestTrue(TEXT("첫 발자국 x=350(앞)"), FMath::IsNearlyEqual(Out[0].MapPos.X, 350.f, 0.5f));
		}
	}

	// (3) 범위가 경로 끝을 넘으면 끝에서 멈춘다(700에서 범위600이어도 끝=1000).
	{
		const TArray<FVector2D> Route = { FVector2D(0, 0), FVector2D(1000, 0) };
		FNavFloorGuide::BuildPlacements(Route, FVector2D(700, 0), 100.f, 600.f, Out);
		TestEqual(TEXT("경로 끝 클립: 3개(800·900·1000)"), Out.Num(), 3);
		if (Out.Num() == 3)
		{
			TestTrue(TEXT("끝 발자국이 경로 끝(1000) 이내"), Out.Last().MapPos.X <= 1000.5f);
		}
	}

	// (4) 점 2개 미만·잘못된 파라미터 → 빈 결과.
	{
		const TArray<FVector2D> One = { FVector2D(0, 0) };
		FNavFloorGuide::BuildPlacements(One, FVector2D(0, 0), 100.f, 600.f, Out);
		TestEqual(TEXT("1점 → 없음"), Out.Num(), 0);

		const TArray<FVector2D> Route = { FVector2D(0, 0), FVector2D(1000, 0) };
		FNavFloorGuide::BuildPlacements(Route, FVector2D(0, 0), 0.f, 600.f, Out);
		TestEqual(TEXT("간격 0 → 없음"), Out.Num(), 0);
		FNavFloorGuide::BuildPlacements(Route, FVector2D(0, 0), 100.f, 0.f, Out);
		TestEqual(TEXT("범위 0 → 없음"), Out.Num(), 0);
	}

	// (5) ㄱ자 경로: (0,0)->(500,0)->(500,500). 앞구간 yaw≈0, 꺾은 뒤 yaw≈90.
	{
		const TArray<FVector2D> Route = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 500) };
		FNavFloorGuide::BuildPlacements(Route, FVector2D(0, 0), 100.f, 1000.f, Out);
		TestTrue(TEXT("ㄱ자: 충분히 많음"), Out.Num() >= 8);
		if (Out.Num() >= 8)
		{
			TestTrue(TEXT("초반 yaw≈0(+X)"), FMath::IsNearlyEqual(Out[0].YawDeg, 0.f, 1.f));
			// 꺾은 뒤(누적 500 이후)의 발자국은 +Y 방향(yaw 90).
			TestTrue(TEXT("후반 yaw≈90(+Y)"), FMath::IsNearlyEqual(Out.Last().YawDeg, 90.f, 1.f));
			// 꺾인 뒤 발자국은 x=500 축에 붙는다(엣지 고정).
			TestTrue(TEXT("후반 x=500 고정"), FMath::IsNearlyEqual(Out.Last().MapPos.X, 500.f, 1.f));
		}
	}

	// (6) 바닥 텍스처 경로 — node_type·공룡별 분기.
	{
		const FString Tri = FNavDestinations::FloorTextureObjectPath(TEXT("exhibit"), TEXT("트리케라톱스"));
		TestTrue(TEXT("전시물1 = EX1 발자국"), Tri.Contains(TEXT("T_Floor_FP_EX1_Triceratops")));
		const FString Rex = FNavDestinations::FloorTextureObjectPath(TEXT("exhibit"), TEXT("티라노사우르스 렉스"));
		TestTrue(TEXT("전시물3 = EX3 발자국"), Rex.Contains(TEXT("T_Floor_FP_EX3_TRex")));
		const FString Toilet = FNavDestinations::FloorTextureObjectPath(TEXT("facility"), TEXT("화장실"));
		TestTrue(TEXT("화장실 = 화살표"), Toilet.Contains(TEXT("T_Floor_Arrow_Facility")));
		const FString Ent = FNavDestinations::FloorTextureObjectPath(TEXT("entrance"), TEXT("입구/출구"));
		TestTrue(TEXT("입구 = 화살표"), Ent.Contains(TEXT("T_Floor_Arrow_Facility")));
		TestTrue(TEXT("junction = 없음"),
			FNavDestinations::FloorTextureObjectPath(TEXT("junction"), TEXT("")).IsEmpty());
		TestTrue(TEXT("Floor 폴더로 간다"), Tri.Contains(TEXT("/Game/UI/Nav/Floor/")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
