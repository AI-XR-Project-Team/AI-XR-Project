#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../DocentChatWidget.h"
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
#include "Components/PanelWidget.h"

/**
 * 도슨트 채팅 화면을 에디터에서 렌더해 PNG 로 남긴다. Nav.SkinPreview 와 같은 방식.
 *
 * 안드로이드 패키징 없이 스킨을 확인하기 위한 것이다. 두 장을 남긴다.
 *   Saved/DocentSkinPreview_Welcome.png  - 시작 화면(렉시 + 인사말 + 추천 질문)
 *   Saved/DocentSkinPreview_Chat.png     - 대화 화면(말풍선 3개)
 *
 * -RenderOffscreen 으로 실행해야 한다. -nullrhi 면 렌더 타깃이 비어 있다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocentSkinTest, "TimeMachineAR.Docent.SkinPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/**
	 * 도슨트 탭을 고른 상태를 만든다.
	 *
	 * 실제 앱에서는 BP 델리게이트가 채팅을 열 때 스캔·네비 패널을 접는데, 헤드리스
	 * 렌더에서는 그 경로가 돌지 않아 스캔 HUD 가 채팅 위에 남는다. 에디터에는
	 * 카메라가 없어 그 HUD 배경이 검게 채워져 채팅이 가려진다.
	 */
	void OpenDocentTab(UDocentChatWidget* Widget)
	{
		// 공룡을 스캔한 뒤의 상태. 추천 질문은 전시물 맥락이 있어야만 만들어진다.
		// OpenChat 은 BuildQuickQuestions 를 Client 검사보다 먼저 돌려 서버 없이도 칩이 생긴다.
		Widget->OpenChat(TEXT("trex_full_skeleton"));
		Widget->ShowChat();
		for (const TCHAR* Name : { TEXT("ScanPanel"), TEXT("NavPanel") })
		{
			if (UWidget* W = Widget->GetWidgetFromName(Name))
			{
				W->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	/**
	 * Slate 위젯을 호출부가 들고 있어야 한다. UWidget::MyWidget 은 TWeakPtr 라, TakeWidget()
	 * 반환값을 버리면 즉시 파괴되고 다음 TakeWidget() 이 위젯을 다시 만들면서
	 * NativeConstruct 가 재실행돼(bStartOpen=false) 채팅이 도로 닫힌다.
	 */
	bool RenderToPng(const TSharedRef<SWidget>& Slate, const FIntPoint& Size, const FString& FileName)
	{
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
		if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags)) { return false; }
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
		return FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FileName));
	}
}

bool FDocentSkinTest::RunTest(const FString& Parameters)
{
	// 에디터 월드에는 DocentClient 서브시스템이 없어 위젯이 Error 를 남긴다.
	// 렌더와 무관하므로 실패로 치지 않는다.
	AddExpectedError(TEXT("DocentClient"), EAutomationExpectedErrorFlags::Contains, 0);

	UWorld* World = nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::Editor) { World = Ctx.World(); break; }
	}
	if (!TestNotNull(TEXT("Editor world"), World)) { return false; }

	UClass* Class = LoadClass<UDocentChatWidget>(nullptr,
		TEXT("/Game/UI/Docent/WBP_DocentChat.WBP_DocentChat_C"));
	if (!TestNotNull(TEXT("Docent chat Blueprint"), Class)) { return false; }

	// 실기기 1440x3120 을 UMG 단위(DPI 1.333)로 옮긴 크기. ApplyChatSkin 의 기준과 같다.
	const FIntPoint Size(1080, 2340);

	// 1) 시작 화면
	{
		UDocentChatWidget* Widget = CreateWidget<UDocentChatWidget>(World, Class);
		if (!TestNotNull(TEXT("Widget (welcome)"), Widget)) { return false; }
		Widget->AddToRoot();
		// WBP 기본값 bStartOpen=false 라 NativeConstruct 가 채팅을 닫는다. 구성 뒤에 연다.
		TSharedRef<SWidget> Slate = Widget->TakeWidget();   // 강한 참조로 잡아 둔다
		OpenDocentTab(Widget);
		{
			auto Vis = [&](const TCHAR* N) -> FString {
				UWidget* W = Widget->GetWidgetFromName(N);
				return W ? FString::FromInt((int32)W->GetVisibility()) : TEXT("null");
			};
			UE_LOG(LogTemp, Display, TEXT("[DocentSkinTest] IsChatOpen=%d ChatPanel=%s ChatBackdrop=%s BottomBar=%s EmptyStateBox=%s IsDesignTime=%d"),
				Widget->IsChatOpen() ? 1 : 0, *Vis(TEXT("ChatPanel")), *Vis(TEXT("ChatBackdrop")),
				*Vis(TEXT("BottomBar")), *Vis(TEXT("EmptyStateBox")), Widget->IsDesignTime() ? 1 : 0);
		}
		const bool bOk = RenderToPng(Slate, Size, TEXT("DocentSkinPreview_Welcome.png"));
		Widget->RemoveFromRoot();
		TestTrue(TEXT("Rendered welcome preview"), bOk);
	}

	// 2) 대화 화면
	{
		UDocentChatWidget* Widget = CreateWidget<UDocentChatWidget>(World, Class);
		if (!TestNotNull(TEXT("Widget (chat)"), Widget)) { return false; }
		Widget->AddToRoot();
		TSharedRef<SWidget> Slate = Widget->TakeWidget();   // 강한 참조로 잡아 둔다
		OpenDocentTab(Widget);
		Widget->AddPreviewBubble(false, TEXT("안녕하세요!\n궁금한 공룡이 있다면 언제든지\n물어보세요!"));
		Widget->AddPreviewBubble(true,  TEXT("티라노사우루스에 대해 설명해줘"));
		Widget->AddPreviewBubble(false, TEXT("티라노사우루스는 약 6,600만 년 전\n백악기 후기에 살았던 대형 육식 공룡이에요.\n강력한 턱과 날카로운 이빨로 유명하며,\n당시 생태계의 최상위 포식자였습니다."));
		Widget->AddExhibitCard();
		Widget->AddPreviewBubble(true,  TEXT("크기는 얼마나 컸을까?"));
		const bool bOk = RenderToPng(Slate, Size, TEXT("DocentSkinPreview_Chat.png"));
		Widget->RemoveFromRoot();
		TestTrue(TEXT("Rendered chat preview"), bOk);
	}

	// 3) 주제 선택 메뉴
	{
		UDocentChatWidget* Widget = CreateWidget<UDocentChatWidget>(World, Class);
		if (!TestNotNull(TEXT("Widget (menu)"), Widget)) { return false; }
		Widget->AddToRoot();
		TSharedRef<SWidget> Slate = Widget->TakeWidget();
		OpenDocentTab(Widget);
		Widget->AddTopicMenu();
		const bool bOk = RenderToPng(Slate, Size, TEXT("DocentSkinPreview_Menu.png"));
		Widget->RemoveFromRoot();
		TestTrue(TEXT("Rendered menu preview"), bOk);
	}
	return true;
}
#endif
