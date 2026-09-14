#include "TimeRevealComponent.h"
#include "ARPin.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DinoOverlayActor.h"
#include "DinoInfoData.h"
#include "DocentChatWidget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CoreDelegates.h"
#include "NavDestinations.h"   // FNavDestinations::SubjectParticle (조사 "이/가" 공용 헬퍼)
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimeRevealMath.h"
#include "TimeRevealProfile.h"
#include "TimeWatchActor.h"
#include "Widgets/Layout/SBackgroundBlur.h"
DEFINE_LOG_CATEGORY_STATIC(LogTimeReveal, Log, All);
UTimeRevealComponent::UTimeRevealComponent() { PrimaryComponentTick.bCanEverTick = true; }
UTimeRevealComponent* UTimeRevealComponent::AttachTo(ADinoOverlayActor* Actor, UTimeRevealProfile* Settings, UARPin* Pin)
{
 if (!IsValid(Actor) || !IsValid(Settings) || !Settings->bEnabled) return nullptr;
 if (auto* Existing = Actor->FindComponentByClass<UTimeRevealComponent>()) return Existing;
 auto* Result = NewObject<UTimeRevealComponent>(Actor, TEXT("TimeRevealComponent"));
 Result->Overlay=Actor; Result->Profile=Settings; Result->TrackingPin=Pin;
 Actor->AddInstanceComponent(Result);
 Result->RegisterComponent();
 // RegisterComponent invokes BeginPlay for a running actor. Editor preview has no BeginPlay.
 if (!Result->bInitialized) Result->Initialize();
 return Result;
}
void UTimeRevealComponent::BeginPlay() { Super::BeginPlay(); Initialize(); }
void UTimeRevealComponent::Initialize()
{
 if (bInitialized) return;
 bInitialized=true;
 if (!Overlay || !Profile) { SetComponentTickEnabled(false); return; }
 PC=UGameplayStatics::GetPlayerController(this,0);
 bOriginalClickable=Overlay->bClickable;
 Overlay->bRevealLocked=true;
 Overlay->bClickable=false;
 if (Overlay->BoneMesh) {
  bOriginalBoneVisible=Overlay->BoneMesh->IsVisible(); OriginalBoneCollision=Overlay->BoneMesh->GetCollisionEnabled();
  Overlay->BoneMesh->SetVisibility(false); Overlay->BoneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 }
 if (Overlay->FleshMesh) {
  bOriginalFleshVisible=Overlay->FleshMesh->IsVisible(); OriginalFleshCollision=Overlay->FleshMesh->GetCollisionEnabled();
  OriginalMaterial=Overlay->FleshMesh->GetMaterial(0);
  Overlay->FleshMesh->SetVisibility(false); Overlay->FleshMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 }
 const TSubclassOf<ATimeWatchActor> Class = Profile->WatchClass ? Profile->WatchClass.Get() : ATimeWatchActor::StaticClass();
 Watch=GetWorld()->SpawnActor<ATimeWatchActor>(Class);
 if (!Watch || !Watch->BodyMesh->GetStaticMesh() || !Watch->HourMesh->GetStaticMesh() || !Watch->MinuteMesh->GetStaticMesh()) { Cancel(true); return; }
 Watch->SetHandsAngle(60.f,-60.f);
 Watch->SetGlow(.18f);
 Watch->SetActorScale3D(FVector(.001f));
 if (PC.IsValid()) {
  GestureInput=NewObject<UInputComponent>(this,TEXT("TimeRevealInput"));
  GestureInput->RegisterComponent(); GestureInput->bBlockInput=false; GestureInput->Priority=5;
  GestureInput->BindTouch(IE_Pressed,this,&UTimeRevealComponent::TouchPressed).bConsumeInput=false;
  GestureInput->BindTouch(IE_Released,this,&UTimeRevealComponent::TouchReleased).bConsumeInput=false;
  GestureInput->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&UTimeRevealComponent::MousePressed).bConsumeInput=false;
  GestureInput->BindKey(EKeys::LeftMouseButton,IE_Released,this,&UTimeRevealComponent::MouseReleased).bConsumeInput=false;
  PC->PushInputComponent(GestureInput);
 }
 TArray<UUserWidget*> Widgets;
 UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,UDocentChatWidget::StaticClass(),true);
 if (Widgets.Num()) HUD=Cast<UDocentChatWidget>(Widgets[0]);
 BackgroundHandle=FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this,&UTimeRevealComponent::Backgrounded);
 EnterState(ETimeRevealState::Ready);
 UpdateReady();
}
void UTimeRevealComponent::EnterState(ETimeRevealState Next)
{
 if (State==Next) return;
 State=Next; StateElapsed=0;
 UE_LOG(LogTimeReveal,Log,TEXT("state=%d"),int32(State));
 ApplyHudHint();
 OnStateChanged.Broadcast(State);
}
void UTimeRevealComponent::ApplyHudHint()
{
	if (!HUD.IsValid()) return;
	const FString Name = (Overlay && Overlay->DinoInfo && !Overlay->DinoInfo->NameKo.IsEmpty())
		? Overlay->DinoInfo->NameKo.ToString() : TEXT("공룡");
	// 조사 규칙은 FNavDestinations 로 옮겼다(내비 안내 로그와 공용, nav-lexi-guide-design.md §2).
	const FString Ga = FNavDestinations::SubjectParticle(Name);
	FString Text;
	switch (State)
	{
	case ETimeRevealState::Ready:
		Text = FString::Printf(TEXT("%s 마커를 찾았어요!\n회중시계를 위로 휙 던져 시간을 되돌려 보세요."), *Name);
		break;
	case ETimeRevealState::Throwing:
		Text = TEXT("회중시계가 날아가요!\n시간의 문이 열릴 거예요.");
		break;
	case ETimeRevealState::Impact:
	case ETimeRevealState::Portal:
		Text = FString::Printf(TEXT("시간의 문이 열리고 있어요!\n%s%s 복원되고 있어요…"), *Name, *Ga);
		break;
	case ETimeRevealState::Revealing:
		Text = FString::Printf(TEXT("%s%s 시간을 거슬러 돌아오고 있어요…\n잠시만 기다려 주세요."), *Name, *Ga);
		break;
	case ETimeRevealState::Complete:
		Text = FString::Printf(TEXT("%s%s 등장했어요!\n공룡을 터치하면 자세한 정보를 볼 수 있어요."), *Name, *Ga);
		break;
	default:
		break;   // Idle/Cancelled: 기본 문구로 복귀.
	}
	HUD->SetRevealHint(Text.IsEmpty() ? FText::GetEmpty() : FText::FromString(Text));
}

FVector UTimeRevealComponent::TargetPoint() const
{
 return Overlay && Overlay->FleshMesh ? Overlay->FleshMesh->Bounds.Origin : FVector::ZeroVector;
}
bool UTimeRevealComponent::TargetIsAvailable() const
{
 if (TrackingPin && TrackingPin->GetTrackingState()!=EARTrackingState::Tracking) return false;
 if (!PC.IsValid()) return GetWorld() && !GetWorld()->IsGameWorld(); // editor regression harness
 int32 W,H; PC->GetViewportSize(W,H);
 FVector2D P;
 return W>0 && H>0 && PC->ProjectWorldLocationToScreen(TargetPoint(),P,false) && P.X>=0 && P.X<=W && P.Y>=0 && P.Y<=H;
}
void UTimeRevealComponent::UpdateReady()
{
 if (!Watch || !PC.IsValid()) return;
 int32 W,H; PC->GetViewportSize(W,H);
 FVector O,D;
 if (W<=0 || H<=0 || !PC->DeprojectScreenPositionToWorld(W*Profile->ReadyScreenAnchor.X,H*Profile->ReadyScreenAnchor.Y,O,D)) return;
 FVector CameraLoc; FRotator CameraRot; PC->GetPlayerViewPoint(CameraLoc,CameraRot);
 const float Bob=FMath::Sin(StateElapsed*2*PI/FMath::Max(.1f,Profile->ReadyBobPeriodSec))*Profile->ReadyBobAmpCm;
 Watch->SetActorLocation(O+D*Profile->ReadyDistanceCm+CameraRot.RotateVector(FVector::UpVector)*Bob);
 // 문자판(+X)은 카메라를, 보우(+Z)는 카메라의 위쪽을 향한다. Rotation() 만 쓰면 카메라가
 // 아래를 내려다볼 때 롤이 뒤집혀 시계가 거꾸로 떠 보인다(실기기 캡처로 확인).
 Watch->SetActorRotation(FRotationMatrix::MakeFromXZ(CameraLoc-Watch->GetActorLocation(),CameraRot.RotateVector(FVector::UpVector)).Rotator());
 Watch->SetActorScale3D(FVector(Profile->WatchScale*FMath::Max(.001f,FTimeRevealMath::PhaseAlpha(StateElapsed,0,Profile->ReadyFadeInSec))));
}
void UTimeRevealComponent::TouchPressed(ETouchIndex::Type Finger,FVector Location)
{
 if (Finger!=ETouchIndex::Touch1) { bOwnsTouch=false; return; }
 if (State!=ETimeRevealState::Ready || !PC.IsValid() || !Watch || !TargetIsAvailable()) return;
 float X,Y; bool SecondDown=false; PC->GetInputTouchState(ETouchIndex::Touch2,X,Y,SecondDown);
 if (SecondDown) return;
 int32 W,H; PC->GetViewportSize(W,H);
 FVector2D Center;
 if (!PC->ProjectWorldLocationToScreen(Watch->GetActorLocation(),Center,false)) return;
 const float Radius=FMath::Max(24.f,W*.12f)+Profile->WatchHitPaddingPx*W/1080.f;
 bOwnsTouch=FVector2D::Distance(FVector2D(Location.X,Location.Y),Center)<=Radius;
 if (bOwnsTouch) { PressPosition=FVector2D(Location.X,Location.Y); PressTime=GetWorld()->GetRealTimeSeconds(); }
}
void UTimeRevealComponent::TouchReleased(ETouchIndex::Type Finger,FVector Location)
{
 if (Finger!=ETouchIndex::Touch1 || !bOwnsTouch || !PC.IsValid()) return;
 bOwnsTouch=false;
 int32 W,H; PC->GetViewportSize(W,H);
 if (FTimeRevealMath::ClassifySwipe(PressPosition,FVector2D(Location.X,Location.Y),GetWorld()->GetRealTimeSeconds()-PressTime,
 FVector2D(W,H),Profile->SwipeMinNormalized,Profile->SwipeMaxSec,Profile->SwipeMaxSideRatio)) TryThrow();
}
void UTimeRevealComponent::MousePressed() { float X,Y; if (PC.IsValid() && PC->GetMousePosition(X,Y)) TouchPressed(ETouchIndex::Touch1,FVector(X,Y,0)); }
void UTimeRevealComponent::MouseReleased() { float X,Y; if (PC.IsValid() && PC->GetMousePosition(X,Y)) TouchReleased(ETouchIndex::Touch1,FVector(X,Y,0)); }
bool UTimeRevealComponent::TryThrow()
{
 if (State!=ETimeRevealState::Ready || !Watch || !TargetIsAvailable()) return false;
 ThrowStart=Watch->GetActorLocation();
 FVector Cam=ThrowStart; FRotator Rot;
 if (PC.IsValid()) PC->GetPlayerViewPoint(Cam,Rot);
 const FVector Target=TargetPoint();
 ThrowEnd=Target+(Cam-Target).GetSafeNormal()*Profile->ThrowStopBeforeTargetCm;
 ThrowUp=Rot.RotateVector(FVector::UpVector);
 Watch->SetActorScale3D(FVector(Profile->WatchScale));
 EnterState(ETimeRevealState::Throwing);
 return true;
}
void UTimeRevealComponent::BeginImpact()
{
 EnterState(ETimeRevealState::Impact); SequenceElapsed=0;
 if (Watch) { ImpactRotation=Watch->GetActorQuat(); Watch->SetHandSpeeds(Profile->HourHandDegPerSec,Profile->MinuteHandDegPerSec); }
}
void UTimeRevealComponent::SetBlur(float Strength)
{
 if (!Blur.IsValid() && Strength>.01f && GetWorld()->GetGameViewport()) {
  SAssignNew(Blur,SBackgroundBlur).BlurStrength(0.f).Visibility(EVisibility::HitTestInvisible);
  GetWorld()->GetGameViewport()->AddViewportWidgetContent(Blur.ToSharedRef(),5);
 }
 if (Blur.IsValid()) Blur->SetBlurStrength(Strength);
}
void UTimeRevealComponent::SetRevealAlpha(float A) { if (RevealMID) RevealMID->SetScalarParameterValue(TEXT("Dissolve"),FMath::Clamp(A,0.f,1.f)); }
void UTimeRevealComponent::UpdateSequence(float Dt)
{
 SequenceElapsed+=Dt;
 const float T=SequenceElapsed;
 if (Watch) {
  Watch->SetActorRotation(ImpactRotation*FQuat(FVector::UpVector,FMath::DegreesToRadians(T*Profile->BodySpinDegPerSec)));
  Watch->SetGlow(.18f+.75f*(1-FTimeRevealMath::PhaseAlpha(T,0,.18f)));
 }
 if (!Orbit && T>=Profile->OrbitStart && Profile->OrbitFX && Watch) {
  Orbit=UNiagaraFunctionLibrary::SpawnSystemAttached(Profile->OrbitFX,Watch->GetRootComponent(),NAME_None,FVector::ZeroVector,FRotator::ZeroRotator,EAttachLocation::SnapToTarget,false,false);
  if (Orbit && Profile->SpriteMaterial) {
   OrbitMID=UMaterialInstanceDynamic::Create(Profile->SpriteMaterial,this);
   OrbitMID->SetVectorParameterValue(TEXT("PrimaryColor"),Profile->PrimaryColor);
   OrbitMID->SetVectorParameterValue(TEXT("SecondaryColor"),Profile->SecondaryColor);
   OrbitMID->SetScalarParameterValue(TEXT("SecondaryMix"),.12f);
   Orbit->SetVariableMaterial(TEXT("User.SpriteMaterial"),OrbitMID);
  }
  if (Orbit) { Orbit->SetRelativeScale3D(FVector(1.1f)); Orbit->Activate(true); }
 }
 if (!Portal && T>=Profile->PortalStart && Profile->PortalFX) {
  FRotator Rot=FRotator::ZeroRotator;
  // Original worm-hole axis is Z. Orient that axis toward the camera once.
  if (PC.IsValid()) { FVector Cam; FRotator Unused; PC->GetPlayerViewPoint(Cam,Unused); Rot=FRotationMatrix::MakeFromZ(Cam-TargetPoint()).Rotator(); }
  Portal=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,Profile->PortalFX,TargetPoint(),Rot,FVector(.001f),false,false);
  if (Portal && Profile->SpriteMaterial) {
   PortalMID=UMaterialInstanceDynamic::Create(Profile->SpriteMaterial,this);
   PortalMID->SetVectorParameterValue(TEXT("PrimaryColor"),Profile->PrimaryColor);
   PortalMID->SetVectorParameterValue(TEXT("SecondaryColor"),Profile->SecondaryColor);
   PortalMID->SetScalarParameterValue(TEXT("SecondaryMix"),.07f);
   Portal->SetVariableMaterial(TEXT("User.SpriteMaterial"),PortalMID);
  }
  if (Portal) Portal->Activate(true);
 }
 if (T>=Profile->PortalStart && State==ETimeRevealState::Impact) EnterState(ETimeRevealState::Portal);
 const float Fade=1-FTimeRevealMath::PhaseAlpha(T,Profile->FadeOutStart,Profile->FadeOutEnd);
 if (OrbitMID) OrbitMID->SetScalarParameterValue(TEXT("Fade"),1-FTimeRevealMath::PhaseAlpha(T,Profile->OrbitEnd,Profile->RevealEnd));
 if (PortalMID) PortalMID->SetScalarParameterValue(TEXT("Fade"),Fade);
 if (Portal) {
  const float Size=Profile->PortalSizeCm>0 ? Profile->PortalSizeCm : FMath::Clamp(Overlay->FleshMesh->Bounds.SphereRadius*1.6f,30.f,320.f);
  const float Scale=FMath::Lerp(.05f,1.f,FTimeRevealMath::PhaseAlpha(T,Profile->PortalStart,Profile->PortalFullSize,Profile->PortalScaleCurve));
  Portal->SetWorldLocation(TargetPoint()); Portal->SetWorldScale3D(FVector(Size/200.f*Scale));
 }
 SetBlur(Profile->BlurMaxStrength*FMath::Min(FTimeRevealMath::PhaseAlpha(T,Profile->PortalStart,Profile->BlurPeakStart,Profile->BlurCurve),1-FTimeRevealMath::PhaseAlpha(T,Profile->BlurPeakEnd,Profile->RevealEnd,Profile->BlurCurve)));
 if (T>=Profile->RevealStart) {
  if (State!=ETimeRevealState::Revealing) {
   EnterState(ETimeRevealState::Revealing);
   if (Overlay->FleshMesh && Profile->RevealMaterial) {
    RevealMID=UMaterialInstanceDynamic::Create(Profile->RevealMaterial,this);
    SetRevealAlpha(0); Overlay->FleshMesh->SetMaterial(0,RevealMID);
   }
   if (Overlay->FleshMesh) Overlay->FleshMesh->SetVisibility(true);
  }
  const float A=FTimeRevealMath::PhaseAlpha(T,Profile->RevealStart,Profile->RevealEnd,Profile->RevealCurve);
  SetRevealAlpha(A);
  if (Watch) { Watch->SetActorLocation(FMath::Lerp(ThrowEnd,TargetPoint(),A)); Watch->SetActorScale3D(FVector(FMath::Max(.001f,Profile->WatchScale*(1-A)))); Watch->SetActorHiddenInGame(A>=1); }
 }
 if (T>=Profile->FadeOutEnd) { Cleanup(true); EnterState(ETimeRevealState::Complete); }
}
void UTimeRevealComponent::Cleanup(bool ShowModel)
{
 bOwnsTouch=false;
 if (GestureInput) { if (PC.IsValid()) PC->PopInputComponent(GestureInput); GestureInput->DestroyComponent(); GestureInput=nullptr; }
 FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle); BackgroundHandle.Reset();
 if (Orbit) { Orbit->DestroyComponent(); Orbit=nullptr; }
 if (Portal) { Portal->DestroyComponent(); Portal=nullptr; }
 if (Watch) { Watch->Destroy(); Watch=nullptr; }
 if (Blur.IsValid()) { if (GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Blur.ToSharedRef()); Blur.Reset(); }
 if (IsValid(Overlay)) {
  Overlay->bRevealLocked=false; Overlay->bClickable=bOriginalClickable;
  if (Overlay->BoneMesh) { Overlay->BoneMesh->SetVisibility(ShowModel && bOriginalBoneVisible); Overlay->BoneMesh->SetCollisionEnabled(ShowModel?OriginalBoneCollision:ECollisionEnabled::NoCollision); }
  if (Overlay->FleshMesh) {
   if (OriginalMaterial) Overlay->FleshMesh->SetMaterial(0,OriginalMaterial);
   Overlay->FleshMesh->SetVisibility(ShowModel);
   Overlay->FleshMesh->SetCollisionEnabled(ShowModel?OriginalFleshCollision:ECollisionEnabled::NoCollision);
  }
 }
 RevealMID=nullptr; SetComponentTickEnabled(false);
 if (HUD.IsValid()) HUD->SetRevealHint(FText::GetEmpty());   // Complete 는 뒤따르는 EnterState 가 다시 채운다.
}
void UTimeRevealComponent::Cancel(bool ShowModel)
{
 if (State==ETimeRevealState::Complete || State==ETimeRevealState::Cancelled) return;
 Cleanup(ShowModel); EnterState(ETimeRevealState::Cancelled);
}
void UTimeRevealComponent::Backgrounded() { Cancel(true); }
void UTimeRevealComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
 Super::TickComponent(DeltaTime,TickType,Tick);
 if (!Profile || State==ETimeRevealState::Complete || State==ETimeRevealState::Cancelled) return;
 if (HUD.IsValid()) {
  UWidget* Nav=HUD->GetWidgetFromName(TEXT("NavPanel"));
  if (HUD->IsChatOpen() || (Nav && Nav->IsVisible())) { Cancel(true); return; }
 }
 const float Dt=FMath::Max(0.f,DeltaTime);
 if (TrackingPin && TrackingPin->GetTrackingState()!=EARTrackingState::Tracking) {
  bOwnsTouch=false; LostTrackingTime+=Dt;
  if (Watch) Watch->SetHandSpeeds(0,0);
  if (Orbit) Orbit->SetPaused(true); if (Portal) Portal->SetPaused(true);
  if (LostTrackingTime>Profile->TrackingLostGraceSec) Cancel(true);
  return;
 }
 if (LostTrackingTime>0) {
  if (Watch && State!=ETimeRevealState::Ready && State!=ETimeRevealState::Throwing) Watch->SetHandSpeeds(Profile->HourHandDegPerSec,Profile->MinuteHandDegPerSec);
  if (Orbit) Orbit->SetPaused(false); if (Portal) Portal->SetPaused(false);
 }
 LostTrackingTime=0; StateElapsed+=Dt;
 if (State==ETimeRevealState::Ready) UpdateReady();
 else if (State==ETimeRevealState::Throwing) {
  const float A=StateElapsed/FMath::Max(.01f,Profile->ThrowSec);
  if (Watch) Watch->SetActorLocation(FTimeRevealMath::ThrowPoint(ThrowStart,ThrowEnd,ThrowUp,Profile->ThrowArcHeightCm,A,Profile->ThrowEase));
  if (A>=1) { const float Overage=StateElapsed-Profile->ThrowSec; BeginImpact(); if (Overage>0) UpdateSequence(Overage); }
 } else UpdateSequence(Dt);
}
void UTimeRevealComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 Cleanup(false); Super::EndPlay(Reason);
}

