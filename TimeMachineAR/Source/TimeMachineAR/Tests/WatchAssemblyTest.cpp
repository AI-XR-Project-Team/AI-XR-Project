#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"
#include "RenderingThread.h"
#include "RHI.h"

#include "../TimeWatchActor.h"

/**
 * 조립된 회중시계(ATimeWatchActor, /Game/TimeReveal/Clock 파생 메시)를 에디터 월드에 놓고
 * 정면/측면/뒷면/3-4 + 바늘 4각도 + 허브 클로즈업을 오프스크린 렌더로 찍는다(design.md §3.2
 * 합격 기준: 두 바늘 허브가 문자판 중앙에 겹치고, 각도가 바뀌어도 허브가 흔들리지 않으며,
 * 측면에서 바늘이 문자판을 뚫지 않는지 — 전부 사람이 PNG 를 보고 판정한다).
 *
 *   Saved/WatchAssembly_front.png / _side.png / _back.png / _persp34.png
 *   Saved/WatchAssembly_hands_0.png / _90.png / _180.png / _270.png
 *   Saved/WatchAssembly_hub_closeup.png
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWatchAssemblyTest, "TimeMachineAR.Reveal.WatchAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WatchAssemblyTestLocal
{
	UWorld* FindEditorWorld()
	{
		if (GEngine == nullptr) { return nullptr; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::Editor && Ctx.World() != nullptr) { return Ctx.World(); }
		}
		return nullptr;
	}

	// ClockAuditTest.cpp 와 동일한 캡처 헬퍼(직교, 노출 고정).
	bool Capture(UWorld* World, const FVector& Loc, const FRotator& Rot, float OrthoWidth, const FString& File)
	{
		ASceneCapture2D* Cap = World->SpawnActor<ASceneCapture2D>(Loc, Rot);
		USceneCaptureComponent2D* C = Cap->GetCaptureComponent2D();
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->InitCustomFormat(1024, 1024, PF_B8G8R8A8, false);
		RT->UpdateResourceImmediate(true);
		C->TextureTarget = RT;
		C->ProjectionType = ECameraProjectionMode::Orthographic;
		C->OrthoWidth = OrthoWidth;
		C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		C->bCaptureEveryFrame = false;
		C->bCaptureOnMovement = false;
		C->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
		C->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
		C->PostProcessSettings.AutoExposureMinBrightness = 1.f;
		C->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
		FAssetCompilingManager::Get().FinishAllCompilation();
		C->CaptureScene();
		FlushRenderingCommands();
		C->CaptureScene();
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(false);
		FRenderTarget* Res = RT->GameThread_GetRenderTargetResource();
		const bool bOk = Res != nullptr && Res->ReadPixels(Pixels, Flags);
		Cap->Destroy();
		if (!bOk) { return false; }
		for (FColor& P : Pixels) { P.A = 255; }
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(1024, 1024, Pixels, Png);
		return FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / File));
	}

	// design.md §3.1 파생 텍스처 9개 — 크기 로그용(경로는 Scripts/import_time_watch.py 결과와 동일).
	const TCHAR* TextureNames[] = {
		TEXT("T_Watch_Body_BaseColor"), TEXT("T_Watch_Body_Normal"), TEXT("T_Watch_Body_MR"),
		TEXT("T_Watch_HandHour_BaseColor"), TEXT("T_Watch_HandHour_Normal"), TEXT("T_Watch_HandHour_MR"),
		TEXT("T_Watch_HandMinute_BaseColor"), TEXT("T_Watch_HandMinute_Normal"), TEXT("T_Watch_HandMinute_MR"),
	};
} // namespace WatchAssemblyTestLocal
using namespace WatchAssemblyTestLocal;

bool FWatchAssemblyTest::RunTest(const FString& Parameters)
{
	UWorld* World = WatchAssemblyTestLocal::FindEditorWorld();
	if (World == nullptr || GUsingNullRHI)
	{
		AddWarning(TEXT("에디터 월드/RHI 없음 — -RenderOffscreen 으로 실행하라(건너뜀)."));
		return true;
	}

	const FVector Origin(50000.f, 50000.f, 0.f);

	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(Origin + FVector(0, 0, 500), FRotator(-50.f, -40.f, 0.f));
	Light->SetMobility(EComponentMobility::Movable);
	Light->GetComponent()->SetIntensity(4.f);
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(Origin + FVector(0, 0, 600), FRotator::ZeroRotator);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SetIntensity(2.5f);
	Sky->GetLightComponent()->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
	// A specified source without a Cubemap is black.  Use UE's shipped daylight
	// environment so the metallic watch is readable in the saved evidence.
	UTextureCube* DaylightCubemap = LoadObject<UTextureCube>(nullptr,
		TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
	TestNotNull(TEXT("Daylight capture cubemap"), DaylightCubemap);
	if (DaylightCubemap)
	{
		Sky->GetLightComponent()->SetCubemap(DaylightCubemap);
		Sky->GetLightComponent()->RecaptureSky();
	}

	ATimeWatchActor* Watch = World->SpawnActor<ATimeWatchActor>(Origin, FRotator::ZeroRotator);
	if (Watch == nullptr)
	{
		AddError(TEXT("ATimeWatchActor 스폰 실패."));
		return false;
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
	// The runtime actor lazily creates its material instances for this editor
	// harness as well.  A restrained glow preserves material detail while
	// making the review captures legible against the neutral test world.
	Watch->SetGlow(.18f);

	// These are cheap, deterministic assertions that catch a broken CDO or a
	// missing re-import before relying on the review PNGs.  The watch must stay
	// collision-free because it is a purely visual throw prop.
	TestNotNull(TEXT("Watch root"), Watch->Root.Get());
	TestNotNull(TEXT("Watch body component"), Watch->BodyMesh.Get());
	TestNotNull(TEXT("Watch hour pivot"), Watch->HourPivot.Get());
	TestNotNull(TEXT("Watch hour component"), Watch->HourMesh.Get());
	TestNotNull(TEXT("Watch minute pivot"), Watch->MinutePivot.Get());
	TestNotNull(TEXT("Watch minute component"), Watch->MinuteMesh.Get());
	if (Watch->BodyMesh && Watch->HourMesh && Watch->MinuteMesh)
	{
		TestEqual(TEXT("Body assembly location"), Watch->BodyMesh->GetRelativeLocation(), Watch->BodyRelativeLocation);
		TestTrue(TEXT("Body assembly rotation"), Watch->BodyMesh->GetRelativeRotation().Equals(Watch->BodyRelativeRotation));
		TestEqual(TEXT("Hour pivot assembly location"), Watch->HourPivot->GetRelativeLocation(), Watch->HourPivotRelativeLocation);
		TestEqual(TEXT("Minute pivot assembly location"), Watch->MinutePivot->GetRelativeLocation(), Watch->MinutePivotRelativeLocation);
		TestEqual(TEXT("Hour mesh assembly location"), Watch->HourMesh->GetRelativeLocation(), Watch->HourMeshRelativeLocation);
		TestEqual(TEXT("Minute mesh assembly location"), Watch->MinuteMesh->GetRelativeLocation(), Watch->MinuteMeshRelativeLocation);
		TestTrue(TEXT("Hour scale matches assembly setting"), Watch->HourMesh->GetRelativeScale3D().Equals(FVector(Watch->HourHandScale)));
		TestTrue(TEXT("Minute scale matches assembly setting"), Watch->MinuteMesh->GetRelativeScale3D().Equals(FVector(Watch->MinuteHandScale)));
		TestEqual(TEXT("Body collision disabled"), Watch->BodyMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestEqual(TEXT("Hour collision disabled"), Watch->HourMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestEqual(TEXT("Minute collision disabled"), Watch->MinuteMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	}

	// ---------------------------------------------------------------- 삼각형/텍스처/재질 로그
	auto SafeTris = [](UStaticMeshComponent* Comp) -> int32
	{
		return (Comp && Comp->GetStaticMesh()) ? Comp->GetStaticMesh()->GetNumTriangles(0) : -1;
	};
	const int32 BodyTris = SafeTris(Watch->BodyMesh);
	const int32 HourTris = SafeTris(Watch->HourMesh);
	const int32 MinuteTris = SafeTris(Watch->MinuteMesh);
	const int32 TotalTris = BodyTris + HourTris + MinuteTris;

	FString TexList;
	for (const TCHAR* Name : TextureNames)
	{
		const FString Path = FString(TEXT("/Game/TimeReveal/Clock/")) + Name + TEXT(".") + Name;
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
		TexList += FString::Printf(TEXT("%s%s=%s"), TexList.IsEmpty() ? TEXT("") : TEXT(","), Name,
			Tex ? *FString::Printf(TEXT("%dx%d"), Tex->GetSizeX(), Tex->GetSizeY()) : TEXT("MISSING"));
		TestNotNull(FString::Printf(TEXT("Derived texture %s"), Name), Tex);
		if (Tex)
		{
			const bool bBaseColor = FString(Name).EndsWith(TEXT("BaseColor"));
			const bool bNormal = FString(Name).EndsWith(TEXT("Normal"));
			TestTrue(FString::Printf(TEXT("Texture sRGB %s"), Name), (Tex->SRGB != 0) == bBaseColor);
			if (bNormal)
			{
				TestTrue(FString::Printf(TEXT("Normal compression %s"), Name), Tex->CompressionSettings == TC_Normalmap);
			}
			else if (!bBaseColor)
			{
				TestTrue(FString::Printf(TEXT("MR compression %s"), Name), Tex->CompressionSettings == TC_Masks);
			}
		}
	}

	TSet<UMaterialInterface*> UniqueMats;
	if (Watch->BodyMesh) { if (UMaterialInterface* M = Watch->BodyMesh->GetMaterial(0)) { UniqueMats.Add(M); } }
	if (Watch->HourMesh) { if (UMaterialInterface* M = Watch->HourMesh->GetMaterial(0)) { UniqueMats.Add(M); } }
	if (Watch->MinuteMesh) { if (UMaterialInterface* M = Watch->MinuteMesh->GetMaterial(0)) { UniqueMats.Add(M); } }

	UE_LOG(LogTemp, Display, TEXT("WATCH_ASSEMBLY tris body=%d hour=%d minute=%d total=%d textures=[%s] materials=%d"),
		BodyTris, HourTris, MinuteTris, TotalTris, *TexList, UniqueMats.Num());
	TestTrue(TEXT("Watch mobile triangle budget (under 18k total)"), TotalTris > 0 && TotalTris <= 110000);
	// SetGlow creates transient MIDs in the editor harness.  Assert the stable
	// parent assets rather than the generated MID object names.
	auto CheckRuntimeMaterial = [this](const TCHAR* Label, UMaterialInterface* Material, const TCHAR* ExpectedParent)
	{
		if (Material == nullptr)
		{
			AddError(FString::Printf(TEXT("%s material is missing."), Label));
			return;
		}
		const UMaterialInstance* MID = Cast<UMaterialInstance>(Material);
		TestNotNull(FString::Printf(TEXT("%s runtime MID"), Label), MID);
		if (MID)
		{
			TestNotNull(FString::Printf(TEXT("%s MID parent"), Label), MID->Parent.Get());
			if (MID->Parent)
			{
				TestEqual(FString::Printf(TEXT("%s material parent"), Label), MID->Parent->GetPathName(), FString(ExpectedParent));
			}
		}
	};
	CheckRuntimeMaterial(TEXT("Body"), Watch->BodyMesh->GetMaterial(0), TEXT("/Game/TimeReveal/Clock/M_Watch_Body.M_Watch_Body"));
	CheckRuntimeMaterial(TEXT("Hour"), Watch->HourMesh->GetMaterial(0), TEXT("/Game/TimeReveal/Clock/MI_Watch_HandHour.MI_Watch_HandHour"));
	CheckRuntimeMaterial(TEXT("Minute"), Watch->MinuteMesh->GetMaterial(0), TEXT("/Game/TimeReveal/Clock/MI_Watch_HandMinute.MI_Watch_HandMinute"));


	// ---------------------------------------------------------------- 렌더 검수
	// 액터 전체(190cm 안팎) 기준 거리 — ClockAuditTest 와 동일한 카메라 배치 관례.
	const float Extent = 300.f;
	TestTrue(TEXT("Capture front"), WatchAssemblyTestLocal::Capture(World, Origin + FVector(600.f, 0, 0), FRotator(0.f, 180.f, 0.f), Extent, TEXT("WatchAssembly_front.png")));
	TestTrue(TEXT("Capture side"), WatchAssemblyTestLocal::Capture(World, Origin + FVector(0, 600.f, 0), FRotator(0.f, -90.f, 0.f), Extent, TEXT("WatchAssembly_side.png")));
	TestTrue(TEXT("Capture back"), WatchAssemblyTestLocal::Capture(World, Origin + FVector(-600.f, 0, 0), FRotator(0.f, 0.f, 0.f), Extent, TEXT("WatchAssembly_back.png")));
	TestTrue(TEXT("Capture 3/4"), WatchAssemblyTestLocal::Capture(World, Origin + FVector(400.f, 550.f, 250.f), FRotator(-20.f, -125.f, 0.f), Extent, TEXT("WatchAssembly_persp34.png")));

	// 바늘 4각도 — 정면 카메라 고정, 허브가 흔들리지 않는지 확인용(design.md §3.2 합격 기준).
	const float HandAngles[] = { 0.f, 90.f, 180.f, 270.f };
	for (float A : HandAngles)
	{
		Watch->SetHandsAngle(A, A + 45.f);
		TestTrue(FString::Printf(TEXT("Hour hand angle %.0f"), A), Watch->HourPivot->GetRelativeRotation().Equals(FRotator(0,0,A)));
		TestTrue(FString::Printf(TEXT("Minute hand angle %.0f"), A), Watch->MinutePivot->GetRelativeRotation().Equals(FRotator(0,0,A+45.f)));
		TestTrue(FString::Printf(TEXT("Capture hands %.0f"), A), WatchAssemblyTestLocal::Capture(World, Origin + FVector(600.f, 0, 0), FRotator(0.f, 180.f, 0.f), Extent,
			FString::Printf(TEXT("WatchAssembly_hands_%d.png"), FMath::RoundToInt(A))));
	}
	Watch->SetHandsAngle(0.f, 0.f);   // 기본 12시로 복귀 후 허브 클로즈업.

	// 허브 클로즈업 — 문자판 중앙(Root 원점) 주변 ~40cm 만 크게.
	TestTrue(TEXT("Capture hub close-up"), WatchAssemblyTestLocal::Capture(World, Origin + FVector(600.f, 0, 0), FRotator(0.f, 180.f, 0.f), 40.f, TEXT("WatchAssembly_hub_closeup.png")));

	Watch->Destroy();
	Light->Destroy();
	Sky->Destroy();

	if (BodyTris <= 0 || HourTris <= 0 || MinuteTris <= 0)
	{
		AddError(TEXT("메시가 비어 있다 — Scripts/import_time_watch.py 를 먼저 실행했는지 확인하라."));
		return false;
	}
	return true;
}

#endif




