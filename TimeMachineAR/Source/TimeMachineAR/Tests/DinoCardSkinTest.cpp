#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../DinoInfoCardWidget.h"
#include "../DinoInfoData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/SWidget.h"

/**
 * 공룡 정보 카드를 에디터에서 렌더해 PNG 로 남긴다. Docent.SkinPreview 와 같은 방식.
 *
 * 아르켈론(바다 스킨) 네 탭을 두 화면 크기로, 그리고 티라노(기존 WBP 스킨)를 한 장
 * 남겨 다른 종에 회귀가 없는지 같이 본다.
 *   Saved/DinoCardOcean_<tab>_<h>.png   - 아르켈론 소개/특징/서식/발견, 높이 2340·1920
 *   Saved/DinoCardDefault_TRex.png      - 티라노 기존 카드
 *   Saved/DinoCardSwitch_TRexAfterOcean.png - 바다 스킨을 거친 뒤 티라노로 되돌린 카드
 *
 * -RenderOffscreen 으로 실행해야 한다. -nullrhi 면 렌더 타깃이 비어 있다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoCardSkinTest, "TimeMachineAR.DinoCard.SkinPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	bool RenderCardToPng(const TSharedRef<SWidget>& Slate, const FIntPoint& Size, const FString& FileName)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		FWidgetRenderer Renderer(false);
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
		Target->InitCustomFormat(Size.X, Size.Y, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate();
		// 첫 프레임은 레이아웃만 잡힌다. 세 번 그려 스크롤·랩 텍스트까지 안정된 것을 읽는다.
		Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.f);
		Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.016f);
		Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.016f);
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(true);
		if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags)) { return false; }
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
		return FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FileName));
	}
}

bool FDinoCardSkinTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::Editor) { World = Ctx.World(); break; }
	}
	if (!TestNotNull(TEXT("Editor world"), World)) { return false; }

	UClass* Class = LoadClass<UDinoInfoCardWidget>(nullptr,
		TEXT("/Game/UI/DinoCard/WBP_DinoInfoCard.WBP_DinoInfoCard_C"));
	if (!TestNotNull(TEXT("Info card Blueprint"), Class)) { return false; }

	UDinoInfoData* Archelon = LoadObject<UDinoInfoData>(nullptr, TEXT("/Game/UI/DinoCard/DA_Dino_Archelon.DA_Dino_Archelon"));
	UDinoInfoData* TRex = LoadObject<UDinoInfoData>(nullptr, TEXT("/Game/UI/DinoCard/DA_Dino_TRex.DA_Dino_TRex"));
	if (!TestNotNull(TEXT("DA_Dino_Archelon"), Archelon) || !TestNotNull(TEXT("DA_Dino_TRex"), TRex)) { return false; }
	TestEqual(TEXT("Archelon uses the ocean skin"), Archelon->CardSkin, EDinoCardSkin::Ocean);
	TestEqual(TEXT("Archelon has four tabs"), Archelon->Tabs.Num(), 4);

	// 실기기 1440x3120 → 1080x2340 단위, 720x1280 / 1080x1920 → 1080x1920 단위.
	const TCHAR* TabNames[] = { TEXT("intro"), TEXT("feature"), TEXT("habitat"), TEXT("discovery") };
	for (const int32 Height : { 2340, 1920 })
	{
		UDinoInfoCardWidget* Card = CreateWidget<UDinoInfoCardWidget>(World, Class);
		if (!TestNotNull(TEXT("Card widget"), Card)) { return false; }
		Card->AddToRoot();
		Card->ShowFor(Archelon);                       // TakeWidget 전에 불러 바다 스킨 루트로 만들어지게 한다
		TSharedRef<SWidget> Slate = Card->TakeWidget();   // 강한 참조로 잡아 둔다
		TestTrue(TEXT("Card open"), Card->IsCardOpen());
		for (int32 Tab = 0; Tab < 4; ++Tab)
		{
			Card->SelectTab(Tab);
			TestEqual(TEXT("Selected tab"), Card->GetSelectedTab(), Tab);
			const FString File = FString::Printf(TEXT("DinoCardOcean_%d_%s_%d.png"), Tab, TabNames[Tab], Height);
			TestTrue(*File, RenderCardToPng(Slate, FIntPoint(1080, Height), File));
		}
		Card->RemoveFromRoot();
	}

	// 티라노: 기존 WBP 스킨 그대로.
	{
		UDinoInfoCardWidget* Card = CreateWidget<UDinoInfoCardWidget>(World, Class);
		Card->AddToRoot();
		Card->ShowFor(TRex);
		TSharedRef<SWidget> Slate = Card->TakeWidget();
		TestTrue(TEXT("TRex default render"), RenderCardToPng(Slate, FIntPoint(1080, 2340), TEXT("DinoCardDefault_TRex.png")));
		Card->RemoveFromRoot();
	}

	// 바다 → 기본 스킨 전환: 같은 위젯에서 루트를 되돌리고 슬레이트를 다시 만든다.
	{
		UDinoInfoCardWidget* Card = CreateWidget<UDinoInfoCardWidget>(World, Class);
		Card->AddToRoot();
		Card->ShowFor(Archelon);
		{
			TSharedRef<SWidget> Slate = Card->TakeWidget();
			RenderCardToPng(Slate, FIntPoint(1080, 2340), TEXT("DinoCardSwitch_OceanFirst.png"));
		}
		Card->ShowFor(TRex);
		TSharedRef<SWidget> Slate = Card->TakeWidget();   // 이전 슬레이트가 죽었으므로 WBP 루트로 다시 만든다
		TestTrue(TEXT("TRex after ocean render"), RenderCardToPng(Slate, FIntPoint(1080, 2340), TEXT("DinoCardSwitch_TRexAfterOcean.png")));
		Card->RemoveFromRoot();
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoCardLiveSkinTest, "TimeMachineAR.DinoCard.LiveSkinSwitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDinoCardLiveSkinTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor) { World = Context.World(); break; }
	}
	UClass* CardClass = LoadClass<UDinoInfoCardWidget>(nullptr,
		TEXT("/Game/UI/DinoCard/WBP_DinoInfoCard.WBP_DinoInfoCard_C"));
	UDinoInfoData* Info = LoadObject<UDinoInfoData>(nullptr,
		TEXT("/Game/UI/DinoCard/DA_Dino_Archelon.DA_Dino_Archelon"));
	if (!TestNotNull(TEXT("World"), World) || !TestNotNull(TEXT("Class"), CardClass)
		|| !TestNotNull(TEXT("Archelon"), Info)) { return false; }
	UDinoInfoCardWidget* Card = CreateWidget<UDinoInfoCardWidget>(World, CardClass);
	Card->AddToRoot();
	// The viewport/input path keeps Slate alive before the first dinosaur tap.
	// Preview tests that call ShowFor before TakeWidget miss this lifecycle.
	TSharedRef<SWidget> LiveSlate = Card->TakeWidget();
	Card->HideCard();
	Card->ShowFor(Info);
	TFunction<bool(const TSharedRef<SWidget>&, const TSharedRef<SWidget>&)> Contains;
	Contains = [&Contains](const TSharedRef<SWidget>& Parent, const TSharedRef<SWidget>& Target)
	{
		if (Parent == Target) { return true; }
		FChildren* Children = Parent->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (Contains(Children->GetChildAt(Index), Target)) { return true; }
		}
		return false;
	};
	TestTrue(TEXT("Already-mounted Slate displays the new ocean root"),
		Contains(LiveSlate, Card->WidgetTree->RootWidget->TakeWidget()));
	Card->SelectTab(2);
	TestEqual(TEXT("Live tab selection"), Card->GetSelectedTab(), 2);
	Card->HideCard();
	TestFalse(TEXT("Closed ocean root cannot block AR input"), Card->WidgetTree->RootWidget->IsVisible());
	Card->ShowFor(Info);
	TestTrue(TEXT("Reopened root remains in the live Slate tree"),
		Contains(LiveSlate, Card->WidgetTree->RootWidget->TakeWidget()));
	Card->RemoveFromRoot();
	return true;
}
#endif
