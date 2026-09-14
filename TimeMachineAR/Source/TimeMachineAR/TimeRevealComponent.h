#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimeRevealComponent.generated.h"
class ADinoOverlayActor;
class ATimeWatchActor;
class UARPin;
class UTimeRevealProfile;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UInputComponent;
class APlayerController;
class UDocentChatWidget;
class SBackgroundBlur;
UENUM(BlueprintType)
enum class ETimeRevealState : uint8 { Idle, Ready, Throwing, Impact, Portal, Revealing, Complete, Cancelled };
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimeRevealStateChanged, ETimeRevealState, State);
UCLASS(ClassGroup=(TimeReveal), meta=(BlueprintSpawnableComponent))
class TIMEMACHINEAR_API UTimeRevealComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UTimeRevealComponent();
 static UTimeRevealComponent* AttachTo(ADinoOverlayActor* Overlay, UTimeRevealProfile* Profile, UARPin* Pin);
 UFUNCTION(BlueprintCallable) void Cancel(bool bRevealModel = true);
 UFUNCTION(BlueprintPure) ETimeRevealState GetState() const { return State; }
 UPROPERTY(BlueprintAssignable) FOnTimeRevealStateChanged OnStateChanged;
 // A single transition entry point also used by the deterministic editor regression test.
 bool TryThrow();
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;
private:
 void Initialize();
 void EnterState(ETimeRevealState NewState);
 void UpdateReady();
 void BeginImpact();
 void UpdateSequence(float DeltaTime);
 void Cleanup(bool bRevealModel);
 void SetRevealAlpha(float Alpha);
 void TouchPressed(ETouchIndex::Type Finger, FVector Location);
 void TouchReleased(ETouchIndex::Type Finger, FVector Location);
 void MousePressed();
 void MouseReleased();
 void Backgrounded();
 FVector TargetPoint() const;
 bool TargetIsAvailable() const;
 void SetBlur(float Strength);
 /** 상태별 렉시 말풍선(스캔 HUD ScanHintText) — 인식→던지기 안내→복원 중→등장. 비우면 기본 문구. */
 void ApplyHudHint();
 UPROPERTY(Transient) TObjectPtr<ADinoOverlayActor> Overlay;
 UPROPERTY(Transient) TObjectPtr<UTimeRevealProfile> Profile;
 UPROPERTY(Transient) TObjectPtr<UARPin> TrackingPin;
 UPROPERTY(Transient) TObjectPtr<ATimeWatchActor> Watch;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Orbit;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Portal;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OrbitMID;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PortalMID;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RevealMID;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> OriginalMaterial;
 UPROPERTY(Transient) TObjectPtr<UInputComponent> GestureInput;
 TWeakObjectPtr<APlayerController> PC;
 TWeakObjectPtr<UDocentChatWidget> HUD;
 TSharedPtr<SBackgroundBlur> Blur;
 FDelegateHandle BackgroundHandle;
 ETimeRevealState State = ETimeRevealState::Idle;
 FVector ThrowStart = FVector::ZeroVector, ThrowEnd = FVector::ZeroVector, ThrowUp = FVector::UpVector;
 FVector2D PressPosition = FVector2D::ZeroVector;
 FQuat ImpactRotation = FQuat::Identity;
 float StateElapsed = 0.f, SequenceElapsed = 0.f, PressTime = 0.f, LostTrackingTime = 0.f;
 bool bOwnsTouch = false, bInitialized = false, bOriginalClickable = true;
 bool bOriginalBoneVisible = true, bOriginalFleshVisible = true;
 ECollisionEnabled::Type OriginalBoneCollision = ECollisionEnabled::NoCollision;
 ECollisionEnabled::Type OriginalFleshCollision = ECollisionEnabled::NoCollision;
};
