// UNavCloudAssetSpawner::ComposeAssetTransform 단위 테스트 — 서버 배치 보정(asset_offset)을 앵커 축으로 얹는 식.
//
// 테스트 앱(feature/13-1test ComposeTarget)과 같은 식이어야 같은 저장값이 두 앱에서 같은 자리에 뜬다.
// 에디터에서 Session Frontend > Automation > "TimeMachineAR.Nav.AssetCompose" 실행, 또는 헤드리스:
//   UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests TimeMachineAR.Nav.AssetCompose; Quit" -unattended -nop4 -nosplash -NullRHI

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavCloudAssetSpawner.h"
#include "DinoInfoData.h"
#include "DinoOverlayActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavCloudAssetComposeTest, "TimeMachineAR.Nav.AssetCompose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavCloudAssetComposeTest::RunTest(const FString& Parameters)
{
	const double Tol = 1e-3;
	const FTransform Anchor(FRotator(0.0, 90.0, 0.0), FVector(100.0, 200.0, -50.0));

	// ① 보정 없음(서버 null · ini 0) → 앵커 그대로 — 이식 전과 같은 자리.
	{
		const FTransform T = UNavCloudAssetSpawner::ComposeAssetTransform(Anchor, FVector::ZeroVector, 0.f, 1.f, 0.f, 0.f, 1.f);
		TestTrue(TEXT("① 위치 = 앵커"), T.GetLocation().Equals(Anchor.GetLocation(), Tol));
		TestEqual(TEXT("① yaw = 앵커"), FRotator::NormalizeAxis(T.Rotator().Yaw), 90.0, Tol);
		TestEqual(TEXT("① 배율 1"), T.GetScale3D().X, 1.0, Tol);
	}

	// ② 위치 보정은 앵커 축 — 앵커 yaw 90° 에서 로컬 +X 100cm 는 월드 +Y 100cm. ini z 는 월드 위로 더한다.
	{
		const FTransform T = UNavCloudAssetSpawner::ComposeAssetTransform(Anchor, FVector(100.0, 0.0, 10.0), 0.f, 1.f, 0.f, 5.f, 1.f);
		TestTrue(TEXT("② 앵커 축 위치 + ini z"), T.GetLocation().Equals(FVector(100.0, 300.0, -35.0), Tol));
	}

	// ③ yaw 는 앵커 + ini + 저장값, 배율은 ini × 저장값.
	{
		const FTransform T = UNavCloudAssetSpawner::ComposeAssetTransform(Anchor, FVector::ZeroVector, 30.f, 1.5f, 15.f, 0.f, 2.f);
		TestEqual(TEXT("③ yaw = 90 + 15 + 30"), FRotator::NormalizeAxis(T.Rotator().Yaw), 135.0, Tol);
		TestEqual(TEXT("③ 배율 = 2 × 1.5"), T.GetScale3D().X, 3.0, Tol);
	}

	// ④ 종 전용 오버레이 선택 — 마커 흐름(ARTrackingManager)처럼 CustomOverlayClass 가 있으면 그것, 없으면 nullptr(ini 폴백).
	{
		TestNull(TEXT("④ 종 데이터 없음"), UNavCloudAssetSpawner::GetSpeciesOverlayClass(nullptr));
		UDinoInfoData* Info = NewObject<UDinoInfoData>();
		TestNull(TEXT("④ 전용 오버레이 미지정"), UNavCloudAssetSpawner::GetSpeciesOverlayClass(Info));
		Info->CustomOverlayClass = ADinoOverlayActor::StaticClass();
		TestTrue(TEXT("④ 전용 오버레이 지정 → 그 클래스"),
			UNavCloudAssetSpawner::GetSpeciesOverlayClass(Info) == ADinoOverlayActor::StaticClass());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
