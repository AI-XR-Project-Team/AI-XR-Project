// UNavCloudResolverSubsystem 근접 앵커 가중 보정(13-3 D74) 순수 계산 단위 테스트.
//
// 플러그인·AR 세션 없이 도는 static 수학만 검증한다. 에디터에서
//   Session Frontend > Automation > "TimeMachineAR.Nav.CloudBlend" 실행,
// 또는 커맨드라인(헤드리스):
//   UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests TimeMachineAR.Nav.CloudBlend; Quit" -unattended -nop4 -nosplash -NullRHI

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavCloudResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavCloudBlendMathTest, "TimeMachineAR.Nav.CloudBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavCloudBlendMathTest::RunTest(const FString& Parameters)
{
	using FResolver = UNavCloudResolverSubsystem;
	const double Tol = 1e-3;

	// ① 앵커 한 개로 세운 변환(SolveTransform 규약) — 앵커 자신의 월드 위치는 자기 맵 좌표로 돌아오고, yaw = 핀 yaw + heading.
	const FTransform AnchorWorld(FRotator(0.0, 37.0, 0.0), FVector(120.0, -40.0, -150.0));
	const FVector AnchorMap(4848.9, 2422.6, 0.0);
	const FTransform AnchorXf = FResolver::MakeAnchorMapToWorld(AnchorWorld, AnchorMap, 359.2);
	TestTrue(TEXT("① 앵커 자기 좌표 왕복"),
		FResolver::CameraMapFromTransform(AnchorXf, AnchorWorld.GetLocation()).Equals(AnchorMap, Tol));
	TestEqual(TEXT("① 변환 yaw = 핀 yaw + heading"),
		FRotator::NormalizeAxis(AnchorXf.Rotator().Yaw), FRotator::NormalizeAxis(37.0 + 359.2), Tol);
	// 맵 +Y(북) 100cm 는 거울상 규약상 월드에서 변환 yaw 기준 −Y 쪽이다(NavLocalizer::MapToWorld 와 같은 식).
	const FVector NorthWorld = AnchorXf.TransformPosition(FVector(AnchorMap.X, -(AnchorMap.Y + 100.0), AnchorMap.Z));
	const FVector ExpectedNorth = AnchorWorld.GetLocation() + FRotator(0.0, AnchorXf.Rotator().Yaw, 0.0).RotateVector(FVector(0.0, -100.0, 0.0));
	TestTrue(TEXT("① 맵 +Y 방향 규약"), NorthWorld.Equals(ExpectedNorth, Tol));

	// ② 카메라 기준 변환 왕복 — 카메라가 원하는 맵 좌표에 보이고 yaw 가 그대로다.
	const FVector CamWorld(10.0, 20.0, 30.0);
	const FVector CamMap(5000.0, 3000.0, 140.0);
	const FTransform CamXf = FResolver::MakeMapToWorldFromCamera(123.4, CamWorld, CamMap);
	TestTrue(TEXT("② 카메라 맵 좌표 왕복"), FResolver::CameraMapFromTransform(CamXf, CamWorld).Equals(CamMap, Tol));
	TestEqual(TEXT("② yaw 유지"), FRotator::NormalizeAxis(CamXf.Rotator().Yaw), 123.4, Tol);

	// ③ 가중 1/max(d, 최소)² — 150cm·300cm 면 4:1.
	{
		TArray<FNavBlendSample> S;
		S.Add(FNavBlendSample{FVector(0.0, 0.0, 0.0), 10.0, 150.0 });
		S.Add(FNavBlendSample{FVector(30.0, 0.0, 0.0), 20.0, 300.0 });
		FVector P; double Yaw = 0.0;
		TestTrue(TEXT("③ 입력 있음"), FResolver::BlendImpliedPoses(S, 150.0, P, Yaw));
		TestEqual(TEXT("③ 위치 가중 평균"), P.X, 6.0, Tol);
		const double ExpectedYaw = FMath::RadiansToDegrees(FMath::Atan2(
			4.0 * FMath::Sin(FMath::DegreesToRadians(10.0)) + FMath::Sin(FMath::DegreesToRadians(20.0)),
			4.0 * FMath::Cos(FMath::DegreesToRadians(10.0)) + FMath::Cos(FMath::DegreesToRadians(20.0))));
		TestEqual(TEXT("③ yaw 원형 가중 평균"), Yaw, ExpectedYaw, Tol);
	}

	// ④ 최소 거리 하한 — 둘 다 하한(150) 안이면 같은 가중.
	{
		TArray<FNavBlendSample> S;
		S.Add(FNavBlendSample{FVector(0.0, 0.0, 0.0), 0.0, 50.0 });
		S.Add(FNavBlendSample{FVector(30.0, 0.0, 0.0), 0.0, 100.0 });
		FVector P; double Yaw = 0.0;
		FResolver::BlendImpliedPoses(S, 150.0, P, Yaw);
		TestEqual(TEXT("④ 하한 안은 같은 가중"), P.X, 15.0, Tol);
	}

	// ⑤ ±180° 경계 — 179° 와 −179° 의 평균은 0° 가 아니라 180°.
	{
		TArray<FNavBlendSample> S;
		S.Add(FNavBlendSample{FVector::ZeroVector, 179.0, 200.0 });
		S.Add(FNavBlendSample{FVector::ZeroVector, -179.0, 200.0 });
		FVector P; double Yaw = 0.0;
		FResolver::BlendImpliedPoses(S, 150.0, P, Yaw);
		TestTrue(TEXT("⑤ 경계를 넘는 원형 평균"), FMath::Abs(Yaw) > 179.9);
	}

	// ⑥ 지수 평활 — dt = τ 이면 63.2% 따라가고, yaw 는 짧은 쪽(170° → −170° 는 +20°)으로 돈다.
	{
		FVector P(0.0, 0.0, 0.0); double Yaw = 170.0;
		FResolver::SmoothToward(P, Yaw, FVector(100.0, 0.0, 0.0), -170.0, 0.8, 0.8);
		const double A = 1.0 - FMath::Exp(-1.0);
		TestEqual(TEXT("⑥ 위치 평활"), P.X, 100.0 * A, Tol);
		TestEqual(TEXT("⑥ yaw 짧은 쪽 평활"), Yaw, FRotator::NormalizeAxis(170.0 + 20.0 * A), Tol);
	}

	// ⑦ 경계값 — 입력이 없으면 false · dt 0 이면 그대로 · τ≈0 이면 목표로 바로.
	{
		TArray<FNavBlendSample> Empty;
		FVector P; double Yaw = 0.0;
		TestFalse(TEXT("⑦ 입력 없음"), FResolver::BlendImpliedPoses(Empty, 150.0, P, Yaw));
		FVector Q(1.0, 2.0, 3.0); double Y2 = 45.0;
		FResolver::SmoothToward(Q, Y2, FVector(9.0, 9.0, 9.0), 90.0, 0.0, 0.8);
		TestTrue(TEXT("⑦ dt 0 은 그대로"), Q.Equals(FVector(1.0, 2.0, 3.0), Tol) && FMath::Abs(Y2 - 45.0) < Tol);
		FResolver::SmoothToward(Q, Y2, FVector(9.0, 9.0, 9.0), 90.0, 0.2, 0.0);
		TestTrue(TEXT("⑦ τ 0 은 목표로"), Q.Equals(FVector(9.0, 9.0, 9.0), Tol) && FMath::Abs(Y2 - 90.0) < Tol);
	}

	// ⑧ 게이트 — 가까운 순 앞 K 개만 보고, 게이트 중심에서 게이트 안인 것만 고른다.
	//    현장 0915: 기점이 먼 #104(틀림)에 끌려 있으면 더 가까운 새 기준 #106 이 85cm 어긋나 빠진다 → 중심은 기준 핀이어야 한다.
	{
		TArray<FNavBlendSample> S;
		S.Add(FNavBlendSample{FVector(100.0, 0.0, 0.0), 0.0, 263.0 });   // #106 — 새 기준
		S.Add(FNavBlendSample{FVector(185.0, 0.0, 0.0), 0.0, 591.0 });   // #104 — 옛 기준(85cm 어긋남)
		S.Add(FNavBlendSample{FVector(110.0, 20.0, 0.0), 0.0, 700.0 });  // #106 과 22cm — 맞는 핀
		S.Add(FNavBlendSample{FVector(100.0, 0.0, 0.0), 0.0, 800.0 });   // K=3 밖
		TArray<int32> Picked;
		FResolver::SelectBlendIndices(S, FVector2D(100.0, 0.0), 50.0, 3, Picked);
		TestTrue(TEXT("⑧ 기준 핀 중심 — 기준과 맞는 핀만"), Picked == TArray<int32>({ 0, 2 }));
		FResolver::SelectBlendIndices(S, FVector2D(185.0, 0.0), 50.0, 3, Picked);
		TestTrue(TEXT("⑧ 옛 기점 중심이면 틀린 #104 만 남는다(고친 버그 재현)"), Picked == TArray<int32>({ 1 }));
		FResolver::SelectBlendIndices(S, FVector2D(100.0, 0.0), 50.0, 0, Picked);
		TestTrue(TEXT("⑧ K 하한 1"), Picked == TArray<int32>({ 0 }));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
