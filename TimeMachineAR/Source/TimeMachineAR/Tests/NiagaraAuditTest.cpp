#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraComponent.h"
#include "NiagaraActor.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"
#include "RenderingThread.h"
#include "RHI.h"

/**
 * 회중시계 연출용 Niagara 원본/파생 시스템을 **에셋에서 직접** 읽어 로그로 남기고, 에디터 월드에서
 * 시뮬레이션을 돌려 PNG 로 찍는다(시간의 문 설계 §6 — 파일명·uasset 문자열로 단정하지 않기 위함).
 *
 *   NIAGARA_AUDIT system <path> exposed=[User.X(type) ...]
 *   NIAGARA_AUDIT   emitter <name> enabled sim=CPU|GPU local=… bounds=… persistentIDs=… maxParticlesEst=…
 *   NIAGARA_AUDIT     renderer <class> material=<path>
 *   NIAGARA_AUDIT     ri <rapid-iteration param>=<float>   (SpawnRate 등 모듈 입력값)
 *
 * 인자(-Params 대신 정적 목록): 기본은 FreeNiagaraPack 원본 2개 + /Game/TimeReveal/FX 파생 2개(있으면).
 * 렌더는 -RenderOffscreen 에서만. Saved/NiagaraAudit_<name>_<t>.png (t = 0.5s, 1.5s, 3.0s).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNiagaraAuditTest, "TimeMachineAR.Reveal.NiagaraAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	const TCHAR* Systems[] = {
		TEXT("/Game/FreeNiagaraPack/Effects/NS_ActiveAtom.NS_ActiveAtom"),
		TEXT("/Game/FreeNiagaraPack/Effects/NS_Worm-Hole.NS_Worm-Hole"),
		TEXT("/Game/TimeReveal/FX/NS_WatchOrbit.NS_WatchOrbit"),
		TEXT("/Game/TimeReveal/FX/NS_TimePortal.NS_TimePortal"),
	};

	UWorld* FindEditorWorld()
	{
		if (GEngine == nullptr) { return nullptr; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::Editor && Ctx.World() != nullptr) { return Ctx.World(); }
		}
		return nullptr;
	}

	void DumpSystem(UNiagaraSystem* Sys, const FString& Path)
	{
		TArray<FNiagaraVariable> Exposed;
		Sys->GetExposedParameters().GetParameters(Exposed);
		FString ExposedStr;
		for (const FNiagaraVariable& V : Exposed)
		{
			ExposedStr += FString::Printf(TEXT("%s(%s) "), *V.GetName().ToString(), *V.GetType().GetName());
		}
		UE_LOG(LogTemp, Display, TEXT("NIAGARA_AUDIT system %s fixedBounds=%s warmup=%.2f exposed=[%s]"),
			*Path, Sys->bFixedBounds ? *Sys->GetFixedBounds().ToString() : TEXT("dynamic"), Sys->GetWarmupTime(), *ExposedStr);

		for (const FNiagaraEmitterHandle& H : Sys->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = H.GetEmitterData();
			if (Data == nullptr)
			{
				UE_LOG(LogTemp, Display, TEXT("NIAGARA_AUDIT   emitter %s (no data)"), *H.GetName().ToString());
				continue;
			}
			UE_LOG(LogTemp, Display, TEXT("NIAGARA_AUDIT   emitter %s enabled=%d sim=%s local=%d boundsMode=%d fixed=%s persistentIDs=%d interpSpawn=%d maxParticlesEst=%d"),
				*H.GetName().ToString(), H.GetIsEnabled() ? 1 : 0,
				Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"),
				Data->bLocalSpace ? 1 : 0, (int32)Data->CalculateBoundsMode, *Data->FixedBounds.ToString(),
				Data->bRequiresPersistentIDs ? 1 : 0, Data->bInterpolatedSpawning ? 1 : 0, Data->GetMaxParticleCountEstimate());
			for (UNiagaraRendererProperties* R : Data->GetRenderers())
			{
				if (R == nullptr) { continue; }
				FString Mat = TEXT("-");
				if (const UNiagaraSpriteRendererProperties* S = Cast<UNiagaraSpriteRendererProperties>(R))
				{
					Mat = S->Material ? S->Material->GetPathName() : TEXT("null");
					Mat += FString::Printf(TEXT(" align=%d facing=%d sortMode=%d"), (int32)S->Alignment, (int32)S->FacingMode, (int32)S->SortMode);
				}
				else if (const UNiagaraMeshRendererProperties* M = Cast<UNiagaraMeshRendererProperties>(R))
				{
					Mat = FString::Printf(TEXT("meshes=%d"), M->Meshes.Num());
					for (const FNiagaraMeshRendererMeshProperties& MP : M->Meshes)
					{
						Mat += TEXT(" ") + (MP.Mesh ? MP.Mesh->GetPathName() : TEXT("null"));
					}
				}
				else if (const UNiagaraRibbonRendererProperties* Rb = Cast<UNiagaraRibbonRendererProperties>(R))
				{
					Mat = Rb->Material ? Rb->Material->GetPathName() : TEXT("null");
				}
				UE_LOG(LogTemp, Display, TEXT("NIAGARA_AUDIT     renderer %s enabled=%d %s"), *R->GetClass()->GetName(), R->GetIsEnabled() ? 1 : 0, *Mat);
			}
			// 모듈 입력값(SpawnRate 등)은 스크립트의 RapidIterationParameters 에 있다.
			TArray<UNiagaraScript*> Scripts;
			Data->GetScripts(Scripts, false);
			for (UNiagaraScript* Sc : Scripts)
			{
				if (Sc == nullptr) { continue; }
				for (const FNiagaraVariableWithOffset& VO : Sc->RapidIterationParameters.ReadParameterVariables())
				{
					const FNiagaraTypeDefinition& T = VO.GetType();
					FString Val;
					const uint8* P = Sc->RapidIterationParameters.GetParameterData(VO.Offset);
					if (P == nullptr) { Val = TEXT("?"); }
					else if (T == FNiagaraTypeDefinition::GetFloatDef()) { Val = FString::SanitizeFloat(*(const float*)P); }
					else if (T == FNiagaraTypeDefinition::GetIntDef()) { Val = FString::FromInt(*(const int32*)P); }
					else if (T == FNiagaraTypeDefinition::GetBoolDef()) { Val = (*(const int32*)P) ? TEXT("true") : TEXT("false"); }
					else if (T == FNiagaraTypeDefinition::GetVec3Def()) { Val = ((const FVector3f*)P)->ToString(); }
					else if (T == FNiagaraTypeDefinition::GetColorDef()) { Val = ((const FLinearColor*)P)->ToString(); }
					else { Val = TEXT("(") + T.GetName() + TEXT(")"); }
					UE_LOG(LogTemp, Display, TEXT("NIAGARA_AUDIT     ri %s = %s"), *VO.GetName().ToString(), *Val);
				}
			}
		}
	}

	bool Capture(UWorld* World, const FVector& Loc, const FRotator& Rot, const FString& File)
	{
		ASceneCapture2D* Cap = World->SpawnActor<ASceneCapture2D>(Loc, Rot);
		USceneCaptureComponent2D* C = Cap->GetCaptureComponent2D();
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->InitCustomFormat(768, 768, PF_B8G8R8A8, false);
		RT->UpdateResourceImmediate(true);
		C->TextureTarget = RT;
		C->FOVAngle = 60.f;
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
		FImageUtils::PNGCompressImageArray(768, 768, Pixels, Png);
		return FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / File));
	}
}

bool FNiagaraAuditTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindEditorWorld();
	const bool bCanRender = World != nullptr && !GUsingNullRHI;
	const FVector Origin(30000.f, 30000.f, 0.f);

	AStaticMeshActor* Floor = nullptr;
	if (bCanRender)
	{
		// 어두운 바닥 — 카메라 배경(박물관 어두운 홀)에 가깝게.
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		Floor = World->SpawnActor<AStaticMeshActor>(Origin + FVector(0, 0, -60), FRotator::ZeroRotator);
		Floor->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(Plane);
		Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(20.f, 20.f, 1.f));
		if (Mat)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mat, Floor);
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.05f, 0.06f));
			Floor->GetStaticMeshComponent()->SetMaterial(0, MID);
		}
	}

	for (const TCHAR* Path : Systems)
	{
		UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, Path);
		if (Sys == nullptr)
		{
			UE_LOG(LogTemp, Display, TEXT("NIAGARA_AUDIT system %s MISSING"), Path);
			continue;
		}
		DumpSystem(Sys, Path);

		if (!bCanRender) { continue; }
		const FString Short = FPaths::GetBaseFilename(Path);
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sys, Origin, FRotator::ZeroRotator,
			FVector(1.f), /*bAutoDestroy*/ false, /*bAutoActivate*/ true, ENCPoolMethod::None, /*bPreCullCheck*/ false);
		if (Comp == nullptr)
		{
			AddWarning(FString::Printf(TEXT("%s 스폰 실패"), *Short));
			continue;
		}
		Comp->SetForceSolo(true);
		float Simulated = 0.f;
		for (float T : { 0.5f, 1.5f, 3.0f })
		{
			Comp->AdvanceSimulationByTime(T - Simulated, 1.f / 30.f);
			Simulated = T;
			Capture(World, Origin + FVector(-320.f, 0.f, 40.f), FRotator(-5.f, 0.f, 0.f),
				FString::Printf(TEXT("NiagaraAudit_%s_%.1fs.png"), *Short, T));
		}
		Comp->DestroyComponent();
	}
	if (Floor) { Floor->Destroy(); }
	return true;
}

#endif
