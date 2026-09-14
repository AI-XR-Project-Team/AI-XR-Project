#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TimeRevealProfile.generated.h"

class ATimeWatchActor;
class UCurveFloat;
class UMaterialInterface;
class UNiagaraSystem;

UCLASS(BlueprintType)
class TIMEMACHINEAR_API UTimeRevealProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<ATimeWatchActor> WatchClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> OrbitFX;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> PortalFX;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UMaterialInterface> RevealMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UMaterialInterface> SpriteMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WatchScale = .07f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D ReadyScreenAnchor = FVector2D(.5f, .60f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReadyDistanceCm = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReadyFadeInSec = .3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReadyBobAmpCm = .6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReadyBobPeriodSec = 2.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SwipeMinNormalized = .12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SwipeMaxSec = .6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SwipeMaxSideRatio = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WatchHitPaddingPx = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ThrowSec = .7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ThrowArcHeightCm = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ThrowStopBeforeTargetCm = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UCurveFloat> ThrowEase;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float OrbitStart = .12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float OrbitEnd = 1.10f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PortalStart = .65f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PortalFullSize = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BlurPeakStart = 1.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BlurPeakEnd = 1.85f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BlurMaxStrength = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RevealStart = 1.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RevealEnd = 2.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FadeOutStart = 2.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FadeOutEnd = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UCurveFloat> PortalScaleCurve;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UCurveFloat> RevealCurve;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UCurveFloat> BlurCurve;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BodySpinDegPerSec = 540.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HourHandDegPerSec = 1440.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinuteHandDegPerSec = 2400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TrackingLostGraceSec = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PortalSizeCm = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor PrimaryColor = FLinearColor(.073f,.913f,1.f,1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor SecondaryColor = FLinearColor(.071f,.366f,1.f,1.f);
};
