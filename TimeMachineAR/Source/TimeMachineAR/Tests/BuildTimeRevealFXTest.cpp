#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildTimeRevealFXTest,"TimeMachineAR.Reveal.BuildFX",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBuildTimeRevealFXTest::RunTest(const FString&)
{
 auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/TimeReveal/FX/M_TimeFX_Sprite.M_TimeFX_Sprite"));
 if (!TestNotNull(TEXT("Generated sprite material"),Mat)) return false;
 const TCHAR* Sources[]={TEXT("/Game/FreeNiagaraPack/Effects/NS_ActiveAtom.NS_ActiveAtom"),TEXT("/Game/FreeNiagaraPack/Effects/NS_Worm-Hole.NS_Worm-Hole")};
 const TCHAR* SystemNames[]={TEXT("NS_WatchOrbit"),TEXT("NS_TimePortal")};
 for (int32 I=0;I<2;++I) {
  auto* Src=LoadObject<UNiagaraSystem>(nullptr,Sources[I]);
  if (!TestNotNull(TEXT("Source effect"),Src)) return false;
  const FString PackageName=FString(TEXT("/Game/TimeReveal/FX/"))+SystemNames[I];
  UPackage* Package=CreatePackage(*PackageName);
  Package->FullyLoad();
  auto* Sys=Cast<UNiagaraSystem>(StaticDuplicateObject(Src,Package,FName(SystemNames[I])));
  if (!TestNotNull(TEXT("Duplicate effect"),Sys)) return false;
  Sys->SetFlags(RF_Public|RF_Standalone);
  Sys->bFixedBounds=true;
  Sys->SetFixedBounds(FBox(FVector(-350),FVector(350)));
  FNiagaraVariable MaterialVar(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()),TEXT("User.SpriteMaterial"));
  Sys->GetExposedParameters().AddParameter(MaterialVar);
  Sys->GetExposedParameters().SetUObject(Mat,MaterialVar);
  for (FNiagaraEmitterHandle& Handle:Sys->GetEmitterHandles()) {
   auto Emitter=Handle.GetInstance();
   auto* Data=Emitter.GetEmitterData();
   if (!Data) continue;
   Data->SimTarget=ENiagaraSimTarget::CPUSim;
   Data->bLocalSpace=true;
   Data->FixedBounds=FBox(FVector(-350),FVector(350));
   const bool Point=Handle.GetName().ToString().Contains(TEXT("Singularity"));
   TArray<UNiagaraScript*> Scripts; Data->GetScripts(Scripts,false);
   for (auto* Script:Scripts) {
    if (!Script) continue;
    auto& Store=Script->RapidIterationParameters;
    TArray<FNiagaraVariable> Vars; Store.GetParameters(Vars);
    for (const auto& V:Vars) {
     const FString N=V.GetName().ToString();
     if (V.GetType()==FNiagaraTypeDefinition::GetFloatDef()) {
      const float Old=Store.GetParameterValue<float>(V);
      float Value=Old;
      if (N.EndsWith(TEXT("SpawnRate.SpawnRate"))) Value=I==0 ? (Point?12.f:240.f) : 280.f;
      else if (N.Contains(TEXT("InitializeParticle.Lifetime"))) Value=N.EndsWith(TEXT("Max"))?1.4f:0.8f;
      else if (I==0 && N.EndsWith(TEXT("Large Radius"))) Value=78.f;
      else if (I==0 && N.EndsWith(TEXT("Sphere Radius"))) Value=6.f;
      else if (N.Contains(TEXT("Uniform Sprite Size"))) Value=N.EndsWith(TEXT("Max")) ? (I==0?4.f:7.f) : 1.8f;
      if (Old!=Value) Store.SetParameterValue(Value,V);
     } else if (V.GetType()==FNiagaraTypeDefinition::GetIntDef() && N.Contains(TEXT("SpawnParticlesInGrid")) && N.EndsWith(TEXT("Count"))) {
      Store.SetParameterValue(4,V);
     }
    }
   }
   for (auto* R:Data->GetRenderers()) if (auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(R)) {
    Sprite->Material=Mat;
    Sprite->MaterialUserParamBinding.Parameter=MaterialVar;
   }
   Emitter.Emitter->MarkPackageDirty();
  }
  Sys->RequestCompile(true);
  Sys->WaitForCompilationComplete(true,false);
  TestTrue(TEXT("CPU effect compiled"),Sys->IsReadyToRun());
  FAssetRegistryModule::AssetCreated(Sys);
  Package->MarkPackageDirty();
  FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
  const FString File=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
  TestTrue(TEXT("Saved effect"),UPackage::SavePackage(Package,Sys,*File,Args));
  AddInfo(FString::Printf(TEXT("FX_BUILT %s emitters=%d"),Names[I],Sys->GetEmitterHandles().Num()));
 }
 return !HasAnyErrors();
}
#endif
