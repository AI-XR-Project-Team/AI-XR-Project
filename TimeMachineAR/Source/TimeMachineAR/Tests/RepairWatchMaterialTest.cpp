#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepairWatchMaterialTest,"TimeMachineAR.Reveal.RepairWatchMaterials",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRepairWatchMaterialTest::RunTest(const FString&)
{
 for (const TCHAR* Name:{TEXT("M_Watch_Body"),TEXT("M_Watch_Hand")}) {
  const FString PackageName=FString(TEXT("/Game/TimeReveal/Clock/"))+Name;
  UMaterial* Mat=LoadObject<UMaterial>(nullptr,*(PackageName+TEXT(".")+Name));
  if (!TestNotNull(TEXT("Watch material"),Mat)) return false;
  int32 Count=0;
  for (UMaterialExpression* E:Mat->GetExpressionCollection().Expressions) {
   if (auto* Sample=Cast<UMaterialExpressionTextureSampleParameter2D>(E)) if (Sample->ParameterName==TEXT("MRTex")) { Sample->SamplerType=SAMPLERTYPE_Masks; ++Count; }
  }
  TestEqual(TEXT("One mask sample"),Count,1);
  Mat->PostEditChange(); Mat->MarkPackageDirty();
  FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
  TestTrue(TEXT("Saved repaired material"),UPackage::SavePackage(Mat->GetOutermost(),Mat,*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args));
 }
 return !HasAnyErrors();
}
#endif
