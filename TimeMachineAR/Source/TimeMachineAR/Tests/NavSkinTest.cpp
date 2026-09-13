#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../NavFullMapWidget.h"
#include "../NavMinimapWidget.h"
#include "../NavDestinations.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferArchive.h"
#include "AssetCompilingManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavSkinTest, "TimeMachineAR.Nav.SkinPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavSkinTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Editor) { World = Context.World(); break; }
	if (!TestNotNull(TEXT("Editor world"), World)) { return false; }
	UClass* Class = LoadClass<UNavFullMapWidget>(nullptr, TEXT("/Game/UI/Nav/WBP_NavMinimapFull.WBP_NavMinimapFull_C"));
	if (!TestNotNull(TEXT("Full map Blueprint"), Class)) { return false; }
	UNavFullMapWidget* Widget = CreateWidget<UNavFullMapWidget>(World, Class);
	if (!TestNotNull(TEXT("Full map instance"), Widget)) { return false; }
	Widget->AddToRoot();
	FNavGraph Graph;
	const FString Seeds = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../backend_server/seeds/nav/"));
	FString Json;
	FFileHelper::LoadFileToString(Json, *(Seeds / TEXT("map_outline.json")));
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object))
	{ Widget->RemoveFromRoot(); AddError(TEXT("Map fixture missing")); return false; }
	for (const TSharedPtr<FJsonValue>& V : Object->GetArrayField(TEXT("outline")))
		Graph.Outline.Emplace(V->AsArray()[0]->AsNumber(), V->AsArray()[1]->AsNumber());
	for (const TSharedPtr<FJsonValue>& V : Object->GetArrayField(TEXT("obstacles")))
	{
		FNavObstacle O; const auto J = V->AsObject();
		O.X0 = J->GetNumberField(TEXT("x0")); O.Y0 = J->GetNumberField(TEXT("y0"));
		O.X1 = J->GetNumberField(TEXT("x1")); O.Y1 = J->GetNumberField(TEXT("y1")); Graph.Obstacles.Add(O);
	}
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *(Seeds / TEXT("nav_nodes.csv")));
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells; Line.ParseIntoArray(Cells, TEXT(","), false);
		if (Cells.Num() < 7 || Cells[1] != TEXT("neuti4f")) { continue; }
		FNavMapNode N; N.NodeId = Cells[0]; N.PosXCm = FCString::Atof(*Cells[2]);
		N.PosYCm = FCString::Atof(*Cells[3]); N.NodeType = Cells[5]; N.Label = Cells[6]; Graph.Nodes.Add(N);
	}
	Widget->ApplyState(Graph, {}, false, FVector2D::ZeroVector, 0, false, TEXT(""));
	TestNotNull(TEXT("Bound map preserved after skin layout"), Widget->GetMapView());
	FAssetCompilingManager::Get().FinishAllCompilation();
	FWidgetRenderer Renderer(false);
	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
	Target->InitCustomFormat(940, 1672, PF_FloatRGBA, true);
	Target->UpdateResourceImmediate();
	Renderer.DrawWidget(Target, SlateWidget, FVector2D(940, 1672), 0.f);
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(true);
	bool bSaved = Target && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags);
	TArray64<uint8> Png;
	if (bSaved) { FImageUtils::PNGCompressImageArray(940, 1672, Pixels, Png); }
	if (bSaved) { bSaved = FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("NavSkinPreview.png"))); }
	TestTrue(TEXT("Rendered full map preview"), bSaved);
	// Drawing and hit testing must use the same transformed position for every destination.
	UNavMinimapWidget* Map = Widget->GetMapView();
	TestTrue(TEXT("Graph survived layout"), Map && Map->HasGraph());
	TSet<FString> Hittable;
	for (int32 Y = 0; Y < 990; Y += 8)
		for (int32 X = 0; X < 880; X += 8)
		{
			FString Id;
			if (Map->FindDestinationNodeAtLocal(FVector2D(X, Y), Map->NodeHitRadiusPx, Id)) { Hittable.Add(Id); }
		}
	TArray<int32> DestinationOrder;
	FNavDestinations::BuildDestinationOrder(Graph.Nodes, DestinationOrder);
	for (int32 Index : DestinationOrder)
		TestTrue(TEXT("Destination remains touchable: ") + Graph.Nodes[Index].Label, Hittable.Contains(Graph.Nodes[Index].NodeId));
	Widget->RemoveFromRoot();
	return bSaved;
}
#endif
