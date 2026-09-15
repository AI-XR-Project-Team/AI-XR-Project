#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "../DinoOverlayActor.h"
#include "../TimeRevealComponent.h"
#include "../TimeRevealMath.h"
#include "../TimeRevealProfile.h"
#include "../TimeWatchActor.h"

namespace
{
	UWorld* FindRevealEditorWorld()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor && Context.World()) return Context.World();
		}
		return nullptr;
	}

	UTimeRevealProfile* MakeFastProfile()
	{
		UTimeRevealProfile* Profile = NewObject<UTimeRevealProfile>(GetTransientPackage());
		Profile->WatchClass = ATimeWatchActor::StaticClass();
		Profile->ThrowSec = .02f;
		Profile->OrbitStart = .005f;
		Profile->PortalStart = .01f;
		Profile->PortalFullSize = .02f;
		Profile->RevealStart = .025f;
		Profile->RevealEnd = .04f;
		Profile->FadeOutStart = .045f;
		Profile->FadeOutEnd = .06f;
		Profile->OrbitFX = nullptr;
		Profile->PortalFX = nullptr;
		Profile->RevealMaterial = nullptr;
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeRevealMathTest, "TimeMachineAR.Reveal.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTimeRevealMathTest::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(1080.f, 1920.f);
	TestTrue(TEXT("Quick upward swipe is accepted"),
		FTimeRevealMath::ClassifySwipe(FVector2D(540,1500), FVector2D(560,1100), .3f, Viewport, .12f, .6f, 1.2f));
	TestFalse(TEXT("Short gesture is rejected"),
		FTimeRevealMath::ClassifySwipe(FVector2D(540,1500), FVector2D(540,1400), .3f, Viewport, .12f, .6f, 1.2f));
	TestFalse(TEXT("Downward gesture is rejected"),
		FTimeRevealMath::ClassifySwipe(FVector2D(540,1100), FVector2D(540,1500), .3f, Viewport, .12f, .6f, 1.2f));
	TestFalse(TEXT("Slow gesture is rejected"),
		FTimeRevealMath::ClassifySwipe(FVector2D(540,1500), FVector2D(540,1000), .7f, Viewport, .12f, .6f, 1.2f));
	TestFalse(TEXT("Mostly sideways gesture is rejected"),
		FTimeRevealMath::ClassifySwipe(FVector2D(200,1500), FVector2D(900,1100), .3f, Viewport, .12f, .6f, 1.2f));
	TestFalse(TEXT("Invalid viewport is safe"),
		FTimeRevealMath::ClassifySwipe(FVector2D::ZeroVector, FVector2D::ZeroVector, .1f, FVector2D::ZeroVector, .1f, .6f, 1.f));

	const FVector A(0,0,0), B(100,0,0), Up(0,0,1);
	TestTrue(TEXT("Throw starts exactly at A"), FTimeRevealMath::ThrowPoint(A,B,Up,25,0).Equals(A));
	TestTrue(TEXT("Throw ends exactly at B"), FTimeRevealMath::ThrowPoint(A,B,Up,25,1).Equals(B));
	const FVector Mid = FTimeRevealMath::ThrowPoint(A,B,Up,25,.5f);
	TestTrue(TEXT("Throw midpoint has a positive arc"), Mid.Z > 24.9f);
	TestTrue(TEXT("Default throw ease advances toward the target"), Mid.X > 50.f && Mid.X < 100.f);
	TestEqual(TEXT("Phase clamps before start"), FTimeRevealMath::PhaseAlpha(-1,0,1), 0.f);
	TestEqual(TEXT("Phase clamps after end"), FTimeRevealMath::PhaseAlpha(2,0,1), 1.f);
	TestTrue(TEXT("Zero-length phase is deterministic"),
		FTimeRevealMath::PhaseAlpha(1,1,1) == 1.f && FTimeRevealMath::PhaseAlpha(0,1,1) == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimeRevealStateTest, "TimeMachineAR.Reveal.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTimeRevealStateTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindRevealEditorWorld();
	if (!TestNotNull(TEXT("Editor world"), World)) return false;

	// The runtime deliberately treats missing watch meshes as a safe fallback, so this
	// regression also catches an incomplete clock import before packaging.
	ATimeWatchActor* AssetProbe = World->SpawnActor<ATimeWatchActor>();
	const bool bClockReady = AssetProbe && AssetProbe->BodyMesh->GetStaticMesh()
		&& AssetProbe->HourMesh->GetStaticMesh() && AssetProbe->MinuteMesh->GetStaticMesh();
	if (AssetProbe) AssetProbe->Destroy();
	if (!TestTrue(TEXT("Clock body and both hands are imported"), bClockReady)) return false;

	ADinoOverlayActor* Overlay = World->SpawnActor<ADinoOverlayActor>();
	if (!TestNotNull(TEXT("Overlay"), Overlay)) return false;
	Overlay->bClickable = true;
	Overlay->BoneMesh->SetVisibility(true);
	Overlay->FleshMesh->SetVisibility(false);
	Overlay->BoneMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Overlay->FleshMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	UTimeRevealProfile* Profile = MakeFastProfile();

	UTimeRevealComponent* Reveal = UTimeRevealComponent::AttachTo(Overlay, Profile, nullptr);
	if (!TestNotNull(TEXT("Reveal component"), Reveal)) { Overlay->Destroy(); return false; }
	TestEqual(TEXT("Attach enters Ready"), Reveal->GetState(), ETimeRevealState::Ready);
	TestTrue(TEXT("Overlay is locked while ready"), Overlay->bRevealLocked);
	TestFalse(TEXT("Overlay cannot open its card while ready"), Overlay->bClickable);
	TestFalse(TEXT("Bone is hidden while ready"), Overlay->BoneMesh->IsVisible());
	TestFalse(TEXT("Flesh is hidden while ready"), Overlay->FleshMesh->IsVisible());

	UTimeRevealComponent* Duplicate = UTimeRevealComponent::AttachTo(Overlay, Profile, nullptr);
	TestTrue(TEXT("Duplicate attach returns the same component"), Duplicate == Reveal);
	TestTrue(TEXT("Valid target can begin throw"), Reveal->TryThrow());
	TestEqual(TEXT("Throw begins in Throwing state"), Reveal->GetState(), ETimeRevealState::Throwing);
	TestFalse(TEXT("Second throw is rejected"), Reveal->TryThrow());

	// Editor worlds do not advance actor ticks on their own during a synchronous test.
	// Executing the registered tick function exercises the same component entry point.
	FGraphEventRef CompletionEvent;
	for (int32 I=0; I<12 && Reveal->GetState()!=ETimeRevealState::Complete; ++I)
	{
		Reveal->PrimaryComponentTick.ExecuteTick(.01f, LEVELTICK_All, ENamedThreads::GameThread, CompletionEvent);
	}
	TestEqual(TEXT("Sequence reaches Complete"), Reveal->GetState(), ETimeRevealState::Complete);
	TestFalse(TEXT("Successful completion unlocks overlay"), Overlay->bRevealLocked);
	TestTrue(TEXT("Successful completion restores click"), Overlay->bClickable);
	TestTrue(TEXT("Successful completion shows flesh"), Overlay->FleshMesh->IsVisible());
	TestEqual(TEXT("Successful completion restores flesh collision"), Overlay->FleshMesh->GetCollisionEnabled(), ECollisionEnabled::PhysicsOnly);
	Overlay->Destroy();

	ADinoOverlayActor* CancelOverlay = World->SpawnActor<ADinoOverlayActor>();
	CancelOverlay->bClickable = false;
	CancelOverlay->BoneMesh->SetVisibility(true);
	CancelOverlay->FleshMesh->SetVisibility(false);
	CancelOverlay->BoneMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CancelOverlay->FleshMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UTimeRevealComponent* CancelReveal = UTimeRevealComponent::AttachTo(CancelOverlay, MakeFastProfile(), nullptr);
	if (!TestNotNull(TEXT("Cancelable component"), CancelReveal)) { CancelOverlay->Destroy(); return false; }
	CancelReveal->Cancel(false);
	TestEqual(TEXT("Cancel enters Cancelled"), CancelReveal->GetState(), ETimeRevealState::Cancelled);
	TestFalse(TEXT("Cancel restores original click state"), CancelOverlay->bClickable);
	TestFalse(TEXT("Cancel(false) keeps bone hidden"), CancelOverlay->BoneMesh->IsVisible());
	TestFalse(TEXT("Cancel(false) keeps flesh hidden"), CancelOverlay->FleshMesh->IsVisible());
	TestEqual(TEXT("Cancel(false) leaves collisions disabled"), CancelOverlay->BoneMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	CancelReveal->Cancel(true);
	TestEqual(TEXT("Repeated cancel is idempotent"), CancelReveal->GetState(), ETimeRevealState::Cancelled);
	CancelOverlay->Destroy();

	// Cancelling after the dissolve material has been installed must never leave the
	// temporary MID on the model. This is the common path when the user changes mode
	// while the portal is open.
	UMaterialInterface* Original = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* RevealMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (TestNotNull(TEXT("Original test material"), Original)
		&& TestNotNull(TEXT("Reveal test material"), RevealMaterial))
	{
		ADinoOverlayActor* MidRevealOverlay = World->SpawnActor<ADinoOverlayActor>();
		MidRevealOverlay->FleshMesh->SetMaterial(0, Original);
		MidRevealOverlay->FleshMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		UTimeRevealProfile* MidRevealProfile = MakeFastProfile();
		MidRevealProfile->RevealMaterial = RevealMaterial;
		UTimeRevealComponent* MidReveal = UTimeRevealComponent::AttachTo(MidRevealOverlay, MidRevealProfile, nullptr);
		if (TestNotNull(TEXT("Mid-reveal component"), MidReveal) && MidReveal->TryThrow())
		{
			FGraphEventRef MidRevealCompletion;
			for (int32 I=0; I<8 && MidReveal->GetState()!=ETimeRevealState::Revealing; ++I)
				MidReveal->PrimaryComponentTick.ExecuteTick(.01f, LEVELTICK_All, ENamedThreads::GameThread, MidRevealCompletion);
			TestEqual(TEXT("Test reaches Revealing before cancellation"), MidReveal->GetState(), ETimeRevealState::Revealing);
			TestTrue(TEXT("Reveal temporarily replaces material"), MidRevealOverlay->FleshMesh->GetMaterial(0) != Original);
			MidReveal->Cancel(true);
			TestEqual(TEXT("Mid-reveal cancel enters Cancelled"), MidReveal->GetState(), ETimeRevealState::Cancelled);
			TestTrue(TEXT("Mid-reveal cancel restores original material"), MidRevealOverlay->FleshMesh->GetMaterial(0) == Original);
			TestEqual(TEXT("Mid-reveal cancel restores collision"), MidRevealOverlay->FleshMesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
			TestTrue(TEXT("Mid-reveal cancel leaves model usable"), MidRevealOverlay->bClickable && MidRevealOverlay->FleshMesh->IsVisible());
		}
		MidRevealOverlay->Destroy();
	}
	return true;
}

#endif
