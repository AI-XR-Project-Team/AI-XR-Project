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
	if (!FFileHelper::LoadFileToString(Json, *(Seeds / TEXT("map_outline.json"))))
	{ Widget->RemoveFromRoot(); AddError(TEXT("Map fixture file missing")); return false; }
	TSharedPtr<FJsonObject> Object;
	TSharedPtr<FJsonValue> Root;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid())
	{
		if (Root->Type == EJson::Object) { Object = Root->AsObject(); }
		else if (Root->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Root->AsArray())
			{
				if (Entry->Type != EJson::Object) { continue; }
				FString MapKey;
				if (Entry->AsObject()->TryGetStringField(TEXT("map_key"), MapKey) && MapKey == TEXT("museum_final"))
				{ Object = Entry->AsObject(); break; }
			}
		}
	}
	if (!Object.IsValid())
	{ Widget->RemoveFromRoot(); AddError(TEXT("Map fixture has no museum_final outline")); return false; }
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
		if (Cells.Num() < 7 || Cells[1] != TEXT("museum_final")) { continue; }
		FNavMapNode N; N.NodeId = Cells[0]; N.PosXCm = FCString::Atof(*Cells[2]);
		N.PosYCm = FCString::Atof(*Cells[3]); N.NodeType = Cells[5]; N.Label = Cells[6]; Graph.Nodes.Add(N);
	}
	Widget->ApplyState(Graph, {}, false, FVector2D::ZeroVector, 0, false, TEXT(""));
	TestNotNull(TEXT("Bound map preserved after skin layout"), Widget->GetMapView());
	TestTrue(TEXT("Original museum artwork is loaded"), Widget->UsesReferenceArtwork());
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
	// The illustrated picker has its own image-space hotspots; routing retains metric graph coordinates.
	UNavMinimapWidget* Map = Widget->GetMapView();
	TestTrue(TEXT("Graph survived layout"), Map && Map->HasGraph());
	TSet<FString> Hittable;
	for (int32 Y = 50; Y < 1210; Y += 8)
		for (int32 X = 0; X < 941; X += 8)
		{
			FString Id;
			if (Widget->FindReferenceDestinationAtLocal(FVector2D(X, Y), Id)) { Hittable.Add(Id); }
		}
	TArray<int32> DestinationOrder;
	FNavDestinations::BuildDestinationOrder(Graph.Nodes, DestinationOrder);
	TestEqual(TEXT("Museum has all thirteen confirmed destinations"), DestinationOrder.Num(), 13);
	const TCHAR* ExpectedLabels[] = { TEXT("자수정"), TEXT("입구"), TEXT("삼엽충"), TEXT("고사리잎"),
		TEXT("아르켈론"), TEXT("알로사우루스"), TEXT("화석"), TEXT("물고기화석"), TEXT("진주화석"),
		TEXT("포유류 화석"), TEXT("거북이 화석"), TEXT("출구"), TEXT("탄생석") };
	for (int32 Position = 0; Position < DestinationOrder.Num() && Position < UE_ARRAY_COUNT(ExpectedLabels); ++Position)
	{
		TestEqual(TEXT("Museum destination display order"), Graph.Nodes[DestinationOrder[Position]].Label, FString(ExpectedLabels[Position]));
	}
	for (int32 Index : DestinationOrder)
		TestTrue(TEXT("Destination remains touchable: ") + Graph.Nodes[Index].Label, Hittable.Contains(Graph.Nodes[Index].NodeId));
	FString EmptyHit;
	TestFalse(TEXT("Header is not a destination"), Widget->FindReferenceDestinationAtLocal(FVector2D(500, 300), EmptyHit));
	Widget->RefreshDestinations(FNavGraph(), FString());
	FString MissingHit;
	TestFalse(TEXT("Artwork cannot select a destination without live data"),
		Widget->FindReferenceDestinationAtLocal(FVector2D(158, 1107), MissingHit));
	Widget->RefreshDestinations(Graph, FString());
	FString RestoredHit;
	TestTrue(TEXT("Async graph arrival restores illustrated hotspot"),
		Widget->FindReferenceDestinationAtLocal(FVector2D(158, 1107), RestoredHit));
	TestEqual(TEXT("Illustration selects original metric graph node ID"), RestoredHit, FString(TEXT("mu-x-amseok")));
	Widget->RemoveFromRoot();
	return bSaved;
}
#endif
