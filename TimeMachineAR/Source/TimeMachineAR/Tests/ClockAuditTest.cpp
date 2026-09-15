#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "AssetCompilingManager.h"
#include "RenderingThread.h"
#include "RHI.h"

/**
 * 회중시계 메시(감사용 /Game/TimeReveal/Audit 또는 파생 /Game/TimeReveal/Clock)를 에디터 월드에 놓고
 * 6방향(+X -X +Y -Y +Z -Z)에서 찍는다. 문자판 방향, 바늘이 본체에 이미 그려졌는지, 바늘의 허브(회전점)가
 * 어느 끝인지 눈으로 확정하기 위한 것이다(설계 §4 — 모델을 보지 않은 피벗 수치를 넘기지 않기 위함).
 *
 *   Saved/ClockAudit_<Part>_<view>.png
 * -ClockAuditPrefix=/Game/TimeReveal/Clock/SM_Watch 로 파생본을 찍을 수 있다(기본 Audit/SM_Audit).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClockAuditTest, "TimeMachineAR.Reveal.ClockAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ClockAuditTestLocal
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

	bool Capture(UWorld* World, const FVector& Loc, const FRotator& Rot, float OrthoWidth, const FString& File)
	{
		ASceneCapture2D* Cap = World->SpawnActor<ASceneCapture2D>(Loc, Rot);
		USceneCaptureComponent2D* C = Cap->GetCaptureComponent2D();
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->InitCustomFormat(1024, 1024, PF_B8G8R8A8, false);
		RT->UpdateResourceImmediate(true);
		C->TextureTarget = RT;
		C->ProjectionType = ECameraProjectionMode::Orthographic;   // 치수 비교가 되게 직교.
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

	/**
	 * 정점을 직접 읽어 조립 수치를 뽑는다.
	 *  - Body : 법선이 +Y 인 정점 중 가장 많은 정점이 몰린 Y 평면 = 문자판. 그 XZ 중심·반지름.
	 *  - Hand : Z 최소 끝(허브 고리) 30cm 구간의 XZ 중심 = 회전점, Z 최대 = 바늘 끝.
	 */
	void AnalyzeGeometry(const FString& Part, UStaticMesh* Mesh)
	{
		const FStaticMeshRenderData* RD = Mesh->GetRenderData();
		if (RD == nullptr || RD->LODResources.Num() == 0) { return; }
		const FStaticMeshLODResources& LOD = RD->LODResources[0];
		const FPositionVertexBuffer& Pos = LOD.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& VB = LOD.VertexBuffers.StaticMeshVertexBuffer;
		const uint32 N = Pos.GetNumVertices();
		if (N == 0) { return; }
		const FBox Box = Mesh->GetBoundingBox();

		if (Part == TEXT("Body"))
		{
			// +Y 법선 정점의 Y 히스토그램(0.5cm 버킷).
			TMap<int32, int32> Hist;
			for (uint32 i = 0; i < N; ++i)
			{
				const FVector3f Nrm = VB.VertexTangentZ(i);
				// 앞쪽 케이스(Y>20)만 — 뒤쪽(Y<-25)은 180° 젖혀진 뚜껑의 안쪽 면이라 제외한다(감사 1차에서 오판).
				if (Nrm.Y > 0.97f && Pos.VertexPosition(i).Y > 20.f)
				{
					Hist.FindOrAdd(FMath::RoundToInt(Pos.VertexPosition(i).Y * 2.f))++;
				}
			}
			int32 BestKey = 0, BestCount = 0;
			for (const auto& KV : Hist) { if (KV.Value > BestCount) { BestCount = KV.Value; BestKey = KV.Key; } }
			const float DialY = BestKey * 0.5f;
			FVector Sum(0); int32 Cnt = 0; float MaxR = 0.f;
			TArray<FVector> Pts;
			for (uint32 i = 0; i < N; ++i)
			{
				const FVector3f P = Pos.VertexPosition(i);
				if (VB.VertexTangentZ(i).Y > 0.97f && FMath::Abs(P.Y - DialY) < 1.0f) { Sum += FVector(P); ++Cnt; Pts.Add(FVector(P)); }
			}
			const FVector C = Cnt > 0 ? Sum / Cnt : FVector::ZeroVector;
			for (const FVector& P : Pts) { MaxR = FMath::Max(MaxR, FVector2D(P.X - C.X, P.Z - C.Z).Size()); }
			// 문자판 앞쪽(더 큰 Y)에 무엇이 있는지: 문자판보다 앞의 정점 수(유리/베젤).
			int32 InFront = 0; float MaxY = Box.Min.Y;
			for (uint32 i = 0; i < N; ++i) { const float Y = Pos.VertexPosition(i).Y; if (Y > DialY + 0.5f) { ++InFront; } MaxY = FMath::Max(MaxY, Y); }
			{
				// 앞 케이스 원판(Y>20)의 XZ 중심·반지름(베젤 바깥) — 문자판 중심과 비교.
				FVector S2(0); int32 C2 = 0; float R2 = 0.f;
				for (uint32 i = 0; i < N; ++i) { const FVector3f P = Pos.VertexPosition(i); if (P.Y > 20.f) { S2 += FVector(P); ++C2; } }
				const FVector CC = C2 > 0 ? S2 / C2 : FVector::ZeroVector;
				for (uint32 i = 0; i < N; ++i) { const FVector3f P = Pos.VertexPosition(i); if (P.Y > 20.f) { R2 = FMath::Max(R2, FVector2D(P.X - CC.X, P.Z - CC.Z).Size()); } }
				UE_LOG(LogTemp, Display, TEXT("CLOCK_GEOM Body frontCase(Y>20) centroid=(%.2f %.2f %.2f) maxRadiusXZ=%.2f verts=%d"), CC.X, CC.Y, CC.Z, R2, C2);
			}
			UE_LOG(LogTemp, Display, TEXT("CLOCK_GEOM Body dialPlaneY=%.2f dialVerts=%d dialCentre=(%.2f %.2f %.2f) dialRadius=%.2f vertsInFrontOfDial=%d maxY=%.2f"),
				DialY, Cnt, C.X, DialY, C.Z, MaxR, InFront, MaxY);
			// Y 슬라이스(5cm)별 정점 수와 XZ 최대 반지름 — 뚜껑/케이스/베젤이 Y 어디에 있는지.
			{
				TMap<int32, TPair<int32, float>> Slices;
				for (uint32 i = 0; i < N; ++i)
				{
					const FVector3f P = Pos.VertexPosition(i);
					TPair<int32, float>& S = Slices.FindOrAdd(FMath::FloorToInt(P.Y / 5.f));
					S.Key++; S.Value = FMath::Max(S.Value, FVector2D(P.X - C.X, P.Z - C.Z).Size());
				}
				Slices.KeySort([](int32 A, int32 B) { return A < B; });
				FString Line;
				for (const auto& KV : Slices) { Line += FString::Printf(TEXT("[y%d:%d,r%.0f]"), KV.Key * 5, KV.Value.Key, KV.Value.Value); }
				UE_LOG(LogTemp, Display, TEXT("CLOCK_GEOM Body ySlices(verts,maxRadiusXZ)=%s"), *Line);
			}
			// Y 히스토그램 상위 5개(문자판·유리·뚜껑 판별용).
			Hist.ValueSort([](int32 A, int32 B) { return A > B; });
			int32 k = 0;
			for (const auto& KV : Hist) { if (k++ >= 6) break; UE_LOG(LogTemp, Display, TEXT("CLOCK_GEOM Body +Y plane y=%.1f verts=%d"), KV.Key * 0.5f, KV.Value); }
		}
		else
		{
			const float ZMin = Box.Min.Z, ZMax = Box.Max.Z;
			FVector Sum(0); int32 Cnt = 0; float HubMaxX = -1e9f, HubMinX = 1e9f;
			float YMin = 1e9f, YMax = -1e9f;
			for (uint32 i = 0; i < N; ++i)
			{
				const FVector3f P = Pos.VertexPosition(i);
				YMin = FMath::Min(YMin, P.Y); YMax = FMath::Max(YMax, P.Y);
				if (P.Z < ZMin + 30.f) { Sum += FVector(P); ++Cnt; HubMaxX = FMath::Max(HubMaxX, P.X); HubMinX = FMath::Min(HubMinX, P.X); }
			}
			const FVector Hub = Cnt > 0 ? Sum / Cnt : FVector::ZeroVector;
			// Z 슬라이스별 X 폭(10cm) — 어느 끝이 허브인지 확인.
			FString Widths;
			for (float Z = ZMin; Z < ZMax; Z += 19.f)
			{
				float Lo = 1e9f, Hi = -1e9f;
				for (uint32 i = 0; i < N; ++i) { const FVector3f P = Pos.VertexPosition(i); if (P.Z >= Z && P.Z < Z + 19.f) { Lo = FMath::Min(Lo, P.X); Hi = FMath::Max(Hi, P.X); } }
				Widths += FString::Printf(TEXT("[%.0f:%.1f]"), Z, Hi > Lo ? Hi - Lo : 0.f);
			}
			UE_LOG(LogTemp, Display, TEXT("CLOCK_GEOM %s hubCentre(bottom30cm)=(%.2f %.2f %.2f) hubWidthX=%.2f tipZ=%.2f length=%.2f thicknessY=%.2f widthsByZ=%s"),
				*Part, Hub.X, Hub.Y, Hub.Z, HubMaxX - HubMinX, ZMax, ZMax - ZMin, YMax - YMin, *Widths);
		}
	}

	struct FView { const TCHAR* Name; FVector Dir; FRotator Rot; };
	const FView Views[] = {
		{ TEXT("fromPlusX"),  FVector( 1, 0, 0), FRotator(0.f, 180.f, 0.f) },   // 카메라가 +X 쪽에서 -X 를 본다.
		{ TEXT("fromMinusX"), FVector(-1, 0, 0), FRotator(0.f, 0.f, 0.f) },
		{ TEXT("fromPlusY"),  FVector( 0, 1, 0), FRotator(0.f, -90.f, 0.f) },
		{ TEXT("fromMinusY"), FVector( 0,-1, 0), FRotator(0.f, 90.f, 0.f) },
		{ TEXT("fromTop"),    FVector( 0, 0, 1), FRotator(-90.f, 0.f, 0.f) },
		{ TEXT("fromBottom"), FVector( 0, 0,-1), FRotator(90.f, 0.f, 0.f) },
	};
} // namespace ClockAuditTestLocal
using namespace ClockAuditTestLocal;

bool FClockAuditTest::RunTest(const FString& Parameters)
{
	UWorld* World = ClockAuditTestLocal::FindEditorWorld();
	if (World == nullptr || GUsingNullRHI)
	{
		AddWarning(TEXT("에디터 월드/RHI 없음 — -RenderOffscreen 으로 실행하라(건너뜀)."));
		return true;
	}
	FString Prefix = TEXT("/Game/TimeReveal/Audit/SM_Audit_");
	FParse::Value(FCommandLine::Get(), TEXT("-ClockAuditPrefix="), Prefix);
	const FVector Origin(40000.f, 40000.f, 0.f);

	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(Origin + FVector(0, 0, 500), FRotator(-50.f, -40.f, 0.f));
	Light->SetMobility(EComponentMobility::Movable);
	Light->GetComponent()->SetIntensity(4.f);
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(Origin + FVector(0, 0, 600), FRotator::ZeroRotator);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SetIntensity(1.5f);
	Sky->GetLightComponent()->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;

	const TCHAR* Parts[] = { TEXT("Body"), TEXT("HandHour"), TEXT("HandMinute") };
	TArray<AStaticMeshActor*> Spawned;
	for (const TCHAR* Part : Parts)
	{
		const FString Path = Prefix + Part + TEXT(".SM_Audit_") + Part;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (Mesh == nullptr)
		{
			// 파생본 이름 규칙(SM_Watch_Body)도 시도.
			const FString Alt = Prefix + Part + TEXT(".") + FPaths::GetBaseFilename(Prefix + Part);
			Mesh = LoadObject<UStaticMesh>(nullptr, *Alt);
		}
		if (Mesh == nullptr)
		{
			AddWarning(FString::Printf(TEXT("메시 없음: %s"), *Path));
			continue;
		}
		const FBox Box = Mesh->GetBoundingBox();
		const float Extent = Box.GetSize().GetMax() * 1.15f;
		UE_LOG(LogTemp, Display, TEXT("CLOCK_VIEW %s tris=%d bounds=%s"), Part, Mesh->GetNumTriangles(0), *Box.ToString());
		AnalyzeGeometry(Part, Mesh);

		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Origin, FRotator::ZeroRotator);
		A->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		for (const FView& V : Views)
		{
			ClockAuditTestLocal::Capture(World, Origin + V.Dir * 600.f, V.Rot, Extent, FString::Printf(TEXT("ClockAudit_%s_%s.png"), Part, V.Name));
		}
		if (FString(Part) == TEXT("Body"))
		{
			// 케이스(Y<-25)와 앞 원판(Y>20) 사이에 카메라를 두고 각각만 본다 + 3/4 원근.
			ClockAuditTestLocal::Capture(World, Origin + FVector(0, 5.f, 0), FRotator(0.f, -90.f, 0.f), Extent, TEXT("ClockAudit_Body_dialOnly_camY5_lookMinusY.png"));
			ClockAuditTestLocal::Capture(World, Origin + FVector(0, 5.f, 0), FRotator(0.f, 90.f, 0.f), Extent, TEXT("ClockAudit_Body_frontDiscInner_camY5_lookPlusY.png"));
			ClockAuditTestLocal::Capture(World, Origin + FVector(250.f, 350.f, 150.f), FRotator(-20.f, -125.f, 0.f), Extent, TEXT("ClockAudit_Body_persp34.png"));
		}
		A->Destroy();
	}
	Light->Destroy();
	Sky->Destroy();
	return true;
}

#endif
