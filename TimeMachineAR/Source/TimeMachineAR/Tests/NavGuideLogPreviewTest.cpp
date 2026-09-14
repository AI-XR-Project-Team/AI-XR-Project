#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../NavGuideLogWidget.h"
#include "../NavTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"
#include "RHI.h"   // GUsingNullRHI

/**
 * 렉시 말풍선(안내 로그)을 미니맵 없이 단독 생성해 4 단계를 PNG 로 남긴다. Nav.SkinPreview·
 * Docent.SkinPreview 와 같은 방식(nav-lexi-guide-design.md §6).
 *
 *   Saved/NavGuideLogPreview_Guiding.png  - 안내 중(멀리, 서버 원문 있음)
 *   Saved/NavGuideLogPreview_Near.png     - 안내 중(근접, "조금만 더 가면")
 *   Saved/NavGuideLogPreview_Arrived.png  - 도착
 *   Saved/NavGuideLogPreview_Ended.png    - 도착 후 자동 종료 마무리 문구(§4)
 *
 * -RenderOffscreen 으로 실행해야 한다. -nullrhi 면 렌더 타깃이 비어 있어 건너뛴다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavGuideLogPreviewTest, "TimeMachineAR.Nav.GuideLogPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	bool RenderGuideLogToPng(UWorld* World, ENavGuidePhase Phase, const FString& NodeType,
		const FString& Label, const FNavGuidance& Guidance, const FIntPoint& Size, const FString& FileName)
	{
		UNavGuideLogWidget* Widget = CreateWidget<UNavGuideLogWidget>(World, UNavGuideLogWidget::StaticClass());
		if (Widget == nullptr) { return false; }
		Widget->AddToRoot();
		TSharedRef<SWidget> Slate = Widget->TakeWidget();   // 강한 참조로 잡아 둔다(UWidget::MyWidget 은 약한 포인터).

		// BindToMinimap 없이 단독 렌더 — 테스트 전용 진입점으로 상태를 바로 밀어 넣는다.
		// (NativeConstruct 는 미니맵 표출 신호가 없으면 Collapsed 로 두므로, PreviewSetState 가
		// 대신 HitTestInvisible 로 켠다.)
		Widget->PreviewSetState(Phase, NodeType, Label, Guidance);

		FAssetCompilingManager::Get().FinishAllCompilation();
		FWidgetRenderer Renderer(false);
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
		Target->InitCustomFormat(Size.X, Size.Y, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate();
		// 첫 프레임은 레이아웃만 잡힌다. 두 번 그려 실제 배치가 반영된 것을 읽는다.
		Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.f);
		Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.016f);
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(true);
		const bool bRead = Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags);
		bool bSaved = false;
		if (bRead)
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
			bSaved = FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FileName));
		}
		Widget->RemoveFromRoot();
		return bSaved;
	}
}

bool FNavGuideLogPreviewTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("-nullrhi: 렌더 타깃이 비어 있어 GuideLogPreview 렌더를 건너뜁니다."));
		return true;
	}

	UWorld* World = nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::Editor) { World = Ctx.World(); break; }
	}
	if (!TestNotNull(TEXT("Editor world"), World)) { return false; }

	// 실기기 1440 폭을 DPI 1.333 배로 옮긴 값과 같은 기준(Docent/Nav SkinPreview 와 동일).
	// 말풍선은 화면 상단 한 줄이라 높이는 300 이면 아바타+3줄까지 충분하다.
	const FIntPoint Size(1080, 520);   // 하단 앵커라 아래 여백 48 + 말풍선 높이가 들어오게 넉넉히.
	const FString NodeType = TEXT("exhibit");
	const FString Label = TEXT("티라노사우루스 렉스");

	bool bAllOk = true;

	{
		FNavGuidance Guiding;
		Guiding.bValid = true;
		Guiding.RemainingCm = 2800.f;
		Guiding.Instruction = TEXT("앞으로 6m 직진하세요");
		bAllOk &= RenderGuideLogToPng(World, ENavGuidePhase::Guiding, NodeType, Label, Guiding,
			Size, TEXT("NavGuideLogPreview_Guiding.png"));
	}
	{
		// 서버 원문 없이 근접(≤500) — "조금만 더 가면" 1줄 + 발자국 안내 2줄을 함께 확인.
		FNavGuidance Near;
		Near.bValid = true;
		Near.RemainingCm = 300.f;
		bAllOk &= RenderGuideLogToPng(World, ENavGuidePhase::Guiding, NodeType, Label, Near,
			Size, TEXT("NavGuideLogPreview_Near.png"));
	}
	{
		FNavGuidance Arrived;
		Arrived.bValid = true;
		Arrived.bArrived = true;
		Arrived.RemainingCm = 0.f;
		bAllOk &= RenderGuideLogToPng(World, ENavGuidePhase::Arrived, NodeType, Label, Arrived,
			Size, TEXT("NavGuideLogPreview_Arrived.png"));
	}
	{
		FNavGuidance Ended;
		Ended.bValid = true;
		Ended.bArrived = true;
		bAllOk &= RenderGuideLogToPng(World, ENavGuidePhase::Ended, NodeType, Label, Ended,
			Size, TEXT("NavGuideLogPreview_Ended.png"));
	}

	TestTrue(TEXT("4장 모두 렌더됨"), bAllOk);
	return bAllOk;
}
#endif
