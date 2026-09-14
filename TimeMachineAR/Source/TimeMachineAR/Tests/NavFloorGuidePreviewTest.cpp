#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../NavFloorGuideActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"
#include "RenderingThread.h"
#include "RHI.h"

/**
 * 바닥 황금 발자국 액터를 에디터 월드에서 오프스크린 렌더해 PNG 로 남긴다.
 *
 * 안드로이드 패키징·실기기 없이 (1) 텍스처 위(발가락)가 진행 방향을 향하는지(+X/+Y/-X/-Y),
 * (2) 좌우 교대와 가로 오프셋, (3) 머티리얼 알파가 밝은/어두운 바닥에서 맞는지,
 * (4) 도착 링+대표 발자국 배치를 눈으로 확인하기 위한 것이다. 측위 없이 맵=월드(항등)라
 * 실제 앱의 MapToWorld 경로는 검증하지 않는다(그건 실기기 몫).
 *
 *   Saved/NavFloorGuidePreview_Top.png      - ㄱ자 경로(+X → +Y) 위에서 내려다본 것
 *   Saved/NavFloorGuidePreview_Walk.png     - 사용자 시점(눈높이 140cm, 아래로 30°)
 *   Saved/NavFloorGuidePreview_Dirs.png     - +X/+Y/-X/-Y 직선 네 방향(위에서)
 *   Saved/NavFloorGuidePreview_Arrival.png  - 도착 링 + 대표 발자국(사용자 시점)
 *   Saved/NavFloorGuidePreview_Dark.png     - 어두운 바닥에서 Walk
 *
 * -RenderOffscreen 으로 실행해야 한다. -nullrhi 면 렌더 타깃이 비어 있다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavFloorGuidePreviewTest, "TimeMachineAR.Nav.FloorGuidePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	UWorld* FindEditorWorld()
	{
		if (GEngine == nullptr) { return nullptr; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::Editor && Ctx.World() != nullptr)
			{
				return Ctx.World();
			}
		}
		return nullptr;
	}

	/** 테스트 장면 원점. 열려 있는 맵의 물체와 겹치지 않게 멀리 둔다. */
	const FVector SceneOrigin(20000.f, 20000.f, 0.f);

	AStaticMeshActor* SpawnFloor(UWorld* World, const FLinearColor& Color)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(SceneOrigin, FRotator::ZeroRotator);
		Floor->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(Plane);
		Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(40.f, 40.f, 1.f));   // 40m×40m
		if (Mat != nullptr)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mat, Floor);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			Floor->GetStaticMeshComponent()->SetMaterial(0, MID);
		}
		return Floor;
	}

	bool Capture(UWorld* World, const FVector& Loc, const FRotator& Rot, float Fov, const FString& File, FString& OutError)
	{
		ASceneCapture2D* Cap = World->SpawnActor<ASceneCapture2D>(Loc, Rot);
		USceneCaptureComponent2D* C = Cap->GetCaptureComponent2D();
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->InitCustomFormat(1024, 1024, PF_B8G8R8A8, false);
		RT->UpdateResourceImmediate(true);
		C->TextureTarget = RT;
		C->FOVAngle = Fov;
		C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		C->bCaptureEveryFrame = false;
		C->bCaptureOnMovement = false;
		C->bAlwaysPersistRenderingState = true;
		// 자동 노출을 고정한다 — 밝은/어두운 바닥이 자동 노출로 같은 회색이 되면 알파 검증이 무의미하다.
		C->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
		C->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
		C->PostProcessSettings.AutoExposureMinBrightness = 1.f;
		C->PostProcessSettings.AutoExposureMaxBrightness = 1.f;

		FAssetCompilingManager::Get().FinishAllCompilation();
		// 셰이더·텍스처 스트리밍이 첫 프레임에 안 들어올 수 있어 두 번 찍고 두 번째를 쓴다.
		C->CaptureScene();
		FlushRenderingCommands();
		C->CaptureScene();
		FlushRenderingCommands();

		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(false);
		FRenderTarget* Res = RT->GameThread_GetRenderTargetResource();
		if (Res == nullptr || !Res->ReadPixels(Pixels, Flags))
		{
			OutError = TEXT("ReadPixels failed: ") + File;
			Cap->Destroy();
			return false;
		}
		for (FColor& P : Pixels) { P.A = 255; }
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(1024, 1024, Pixels, Png);
		const FString Path = FPaths::ProjectSavedDir() / File;
		Cap->Destroy();
		if (!FFileHelper::SaveArrayToFile(Png, *Path))
		{
			OutError = TEXT("Save failed: ") + Path;
			return false;
		}
		return true;
	}

	TArray<FVector2D> Offset(std::initializer_list<FVector2D> Pts)
	{
		TArray<FVector2D> Out;
		for (const FVector2D& P : Pts) { Out.Add(P + FVector2D(SceneOrigin.X, SceneOrigin.Y)); }
		return Out;
	}
}

bool FNavFloorGuidePreviewTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindEditorWorld();
	if (World == nullptr)
	{
		AddError(TEXT("에디터 월드가 없다."));
		return false;
	}
	if (GUsingNullRHI)
	{
		AddWarning(TEXT("-nullrhi 에서는 렌더할 수 없다. -RenderOffscreen 으로 다시 실행하라(건너뜀)."));
		return true;
	}

	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(SceneOrigin + FVector(0, 0, 500), FRotator(-60.f, 30.f, 0.f));
	Light->SetMobility(EComponentMobility::Movable);
	Light->GetComponent()->SetIntensity(3.f);

	AStaticMeshActor* Floor = SpawnFloor(World, FLinearColor(0.55f, 0.53f, 0.50f));   // 밝은 대리석 느낌.
	ANavFloorGuideActor* Guide = World->SpawnActor<ANavFloorGuideActor>(SceneOrigin, FRotator::ZeroRotator);

	const FString Ex = TEXT("exhibit");
	const FString Label = TEXT("티라노사우르스 렉스");
	FString Err;
	bool bOk = true;

	// (1) ㄱ자: (0,0)→(500,0)→(500,600). 사용자는 시작점.
	Guide->UpdateGuidePreview(Offset({ FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 600) }),
		FVector2D(SceneOrigin.X, SceneOrigin.Y), Ex, Label);
	// 첫 캡처는 머티리얼 셰이더가 아직 안 올라와 발자국이 빠질 수 있다 — 버리는 캡처 한 장.
	Capture(World, SceneOrigin + FVector(300, 250, 900), FRotator(-90.f, 0.f, 0.f), 60.f, TEXT("NavFloorGuidePreview_Warmup.png"), Err);
	bOk &= Capture(World, SceneOrigin + FVector(300, 250, 900), FRotator(-90.f, 0.f, 0.f), 60.f, TEXT("NavFloorGuidePreview_Top.png"), Err);
	bOk &= Capture(World, SceneOrigin + FVector(-60, 0, 140), FRotator(-30.f, 0.f, 0.f), 70.f, TEXT("NavFloorGuidePreview_Walk.png"), Err);

	// (2) 네 방향 직선 — 한 액터에 하나씩만 낼 수 있으니 액터 4개.
	{
		TArray<ANavFloorGuideActor*> Dirs;
		const FVector2D Ends[] = { FVector2D(400, 0), FVector2D(0, 400), FVector2D(-400, 0), FVector2D(0, -400) };
		for (const FVector2D& E : Ends)
		{
			ANavFloorGuideActor* A = World->SpawnActor<ANavFloorGuideActor>(SceneOrigin, FRotator::ZeroRotator);
			A->UpdateGuidePreview(Offset({ FVector2D(0, 0), E }), FVector2D(SceneOrigin.X, SceneOrigin.Y), Ex, Label);
			Dirs.Add(A);
		}
		Guide->HideGuide();
		bOk &= Capture(World, SceneOrigin + FVector(0, 0, 1000), FRotator(-90.f, 0.f, 0.f), 60.f, TEXT("NavFloorGuidePreview_Dirs.png"), Err);
		for (ANavFloorGuideActor* A : Dirs) { A->Destroy(); }
	}

	// (3) 도착: 목적지 (500,600) 앞 40cm.
	{
		FNavFloorArrival Arr;
		Arr.bArrived = true;
		Arr.bHasDestination = true;
		Arr.DestMapXY = FVector2D(SceneOrigin.X + 500, SceneOrigin.Y + 600);
		Guide->UpdateGuidePreview(Offset({ FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 600) }),
			FVector2D(SceneOrigin.X + 500, SceneOrigin.Y + 560), Ex, Label, Arr);
		bOk &= Capture(World, SceneOrigin + FVector(500, 380, 140), FRotator(-35.f, 90.f, 0.f), 70.f, TEXT("NavFloorGuidePreview_Arrival.png"), Err);
	}

	// (4) 어두운 바닥에서 Walk.
	{
		Floor->Destroy();
		Floor = SpawnFloor(World, FLinearColor(0.06f, 0.06f, 0.07f));
		Guide->UpdateGuidePreview(Offset({ FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 600) }),
			FVector2D(SceneOrigin.X, SceneOrigin.Y), Ex, Label);
		bOk &= Capture(World, SceneOrigin + FVector(-60, 0, 140), FRotator(-30.f, 0.f, 0.f), 70.f, TEXT("NavFloorGuidePreview_Dark.png"), Err);
	}

	Guide->Destroy();
	Floor->Destroy();
	Light->Destroy();

	if (!bOk)
	{
		AddError(Err);
	}
	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
