// 13-4 재측위 오버레이 미리보기 — 폰(S21 1080×2400)의 절반 크기로 오프스크린 렌더해 PNG 로 남긴다(에디터 전용).
// NavSkinTest 와 같은 FWidgetRenderer 방식이다. 실기기 T0 전에 배치·글씨 가독성을 먼저 본다.
//   UnrealEditor-Cmd <proj> -ExecCmds="Automation RunTests TimeMachineAR.Nav.RelocOverlayPreview; Quit" -unattended -nop4 -nosplash
//   → Saved/NavRelocPreview_*.png
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../NavRelocalizeOverlay.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** 위젯을 Size 로 그려 PNG 로 저장한다. Background = AR 카메라 대신 깔 단색. Advance(초)만큼 틱해 애니메이션을 진행시킨다. */
	bool RenderRelocPreview(UNavRelocalizeOverlay* Widget, const FIntPoint& Size, const FLinearColor& Background,
		float Advance, const FString& FileName)
	{
		FWidgetRenderer Renderer(false);
		TSharedRef<SWidget> Slate = Widget->TakeWidget();
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
		Target->ClearColor = Background;
		Target->InitCustomFormat(Size.X, Size.Y, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.f);
		if (Advance > 0.f)
		{
			Renderer.DrawWidget(Target, Slate, FVector2D(Size), Advance);
		}
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(true);
		if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags))
		{
			return false;
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;   // 반투명 어둡기를 배경색 위에 합성한 결과만 본다
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
		return FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FileName));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavRelocOverlayPreviewTest, "TimeMachineAR.Nav.RelocOverlayPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNavRelocOverlayPreviewTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor)
		{
			World = Context.World();
			break;
		}
	}
	if (!TestNotNull(TEXT("Editor world"), World))
	{
		return false;
	}

	UNavRelocalizeOverlay* Widget = CreateWidget<UNavRelocalizeOverlay>(World, UNavRelocalizeOverlay::StaticClass());
	if (!TestNotNull(TEXT("Overlay instance"), Widget))
	{
		return false;
	}
	Widget->AddToRoot();
	Widget->TakeWidget();   // 트리를 세운다(RebuildWidget)
	Widget->SetAlphas(0.20f, 0.62f);

	const FIntPoint Size(1080, 2400);                             // S21 UMG 뷰포트 그대로(DPI 배율 1.0) — 폰과 같은 배율 3.375
	const FLinearColor Dark(0.035f, 0.030f, 0.025f);              // 어두운 전시실
	const FLinearColor Bright(0.55f, 0.55f, 0.52f);               // 유리 반사·흰 벽

	bool bOk = true;
	Widget->ShowScanning(TEXT("shake"));
	bOk &= RenderRelocPreview(Widget, Size, Dark, 1.45f, TEXT("NavRelocPreview_1_scan_dark.png"));
	Widget->ShowScanning(TEXT("occluded"));
	bOk &= RenderRelocPreview(Widget, Size, Bright, 3.10f, TEXT("NavRelocPreview_2_return_bright.png"));
	Widget->ShowScanning(TEXT("jump"));
	Widget->SetStageText(2);
	bOk &= RenderRelocPreview(Widget, Size, Bright, 2.20f, TEXT("NavRelocPreview_3_stage90_bright.png"));
	Widget->ShowRecovered();
	bOk &= RenderRelocPreview(Widget, Size, Dark, 0.60f, TEXT("NavRelocPreview_4_recovered_dark.png"));

	TestTrue(TEXT("오버레이 미리보기 PNG 4장 저장"), bOk);
	Widget->RemoveFromRoot();
	return bOk;
}
#endif
