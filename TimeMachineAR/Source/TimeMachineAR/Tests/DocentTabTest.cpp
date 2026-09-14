#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../DocentChatWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"

/**
 * ScanPanel/NavPanel/ChatPanel/BottomBar 이 서로 겹치지 않는지 확인한다.
 *
 * PROBLEM: 도슨트를 열면 스캔 패널(틀·조준원·위치 칩·힌트·셔터)이 채팅 위에 그대로
 * 남던 문제. 이제 이 위젯(UDocentChatWidget)이 SetHudTab/ApplyHudTab 으로 세 패널의
 * 배타 표시를 혼자 강제한다. DocentSkinTest 와 같은 방식으로 WBP_DocentChat 을
 * 에디터 월드에 만들어 확인한다(서버 없이도 SetHudTab 자체는 순수 표시 로직이라 돈다).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocentTabTest, "TimeMachineAR.Docent.TabExclusive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDocentTabTest::RunTest(const FString& Parameters)
{
	// 에디터 월드에는 DocentClient 서브시스템이 없어 위젯이 Error 를 남긴다.
	// 렌더/표시 로직과 무관하므로 실패로 치지 않는다.
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

	UDocentChatWidget* Widget = CreateWidget<UDocentChatWidget>(World, Class);
	if (!TestNotNull(TEXT("Widget"), Widget)) { return false; }
	Widget->AddToRoot();
	// Slate 를 만들어 둬야 NativeConstruct 가 돈다. 반환값을 버리면 위젯이 바로
	// 파괴되고, 다음 TakeWidget() 이 다시 만들면서 NativeConstruct 가 재실행된다
	// (DocentSkinTest 의 같은 주석 참고).
	TSharedRef<SWidget> Slate = Widget->TakeWidget();

	// 위젯 이름이 없을 수 있는 맨 C++ 구성에서는 크래시 대신 경고로 건너뛴다.
	auto GetVis = [&](const TCHAR* Name) -> TOptional<ESlateVisibility>
	{
		UWidget* W = Widget->GetWidgetFromName(Name);
		if (W == nullptr)
		{
			AddWarning(FString::Printf(TEXT("위젯을 찾지 못해 건너뜀: %s"), Name));
			return TOptional<ESlateVisibility>();
		}
		return W->GetVisibility();
	};

	auto CheckVis = [&](const TCHAR* Name, ESlateVisibility Expected, const TCHAR* Context)
	{
		TOptional<ESlateVisibility> Vis = GetVis(Name);
		if (!Vis.IsSet())
		{
			return;
		}
		TestEqual(FString::Printf(TEXT("%s: %s visibility"), Context, Name),
			static_cast<uint8>(*Vis), static_cast<uint8>(Expected));
	};

	// ChatPanel 은 BindWidgetOptional 로 직접 접근 가능하다. 이름으로도 동일하게 찾을 수 있다.
	auto CheckChatPanel = [&](ESlateVisibility Expected, const TCHAR* Context)
	{
		CheckVis(TEXT("ChatPanel"), Expected, Context);
	};

	// ---- Scan 탭
	Widget->SetHudTab(EDocentHudTab::Scan);
	TestEqual(TEXT("GetHudTab() == Scan"), (uint8)Widget->GetHudTab(), (uint8)EDocentHudTab::Scan);
	CheckVis(TEXT("ScanPanel"), ESlateVisibility::SelfHitTestInvisible, TEXT("Scan"));
	CheckVis(TEXT("NavPanel"), ESlateVisibility::Collapsed, TEXT("Scan"));
	CheckChatPanel(ESlateVisibility::Collapsed, TEXT("Scan"));
	CheckVis(TEXT("BottomBar"), ESlateVisibility::Visible, TEXT("Scan"));

	// ---- Nav 탭
	Widget->SetHudTab(EDocentHudTab::Nav);
	TestEqual(TEXT("GetHudTab() == Nav"), (uint8)Widget->GetHudTab(), (uint8)EDocentHudTab::Nav);
	CheckVis(TEXT("NavPanel"), ESlateVisibility::Visible, TEXT("Nav"));
	CheckVis(TEXT("ScanPanel"), ESlateVisibility::Collapsed, TEXT("Nav"));
	CheckChatPanel(ESlateVisibility::Collapsed, TEXT("Nav"));
	CheckVis(TEXT("BottomBar"), ESlateVisibility::Collapsed, TEXT("Nav"));

	// ---- Docent 탭
	Widget->SetHudTab(EDocentHudTab::Docent);
	TestEqual(TEXT("GetHudTab() == Docent"), (uint8)Widget->GetHudTab(), (uint8)EDocentHudTab::Docent);
	CheckChatPanel(ESlateVisibility::Visible, TEXT("Docent"));
	CheckVis(TEXT("ScanPanel"), ESlateVisibility::Collapsed, TEXT("Docent"));
	CheckVis(TEXT("NavPanel"), ESlateVisibility::Collapsed, TEXT("Docent"));

	// ---- 전환 순서: Scan -> Docent -> Scan -> Nav -> Scan. 마지막에 활성 탭 외에는
	// 아무 패널도 보이지 않아야 한다(스캔 패널이 도슨트 위에 남던 원래 버그의 회귀 확인).
	Widget->SetHudTab(EDocentHudTab::Scan);
	Widget->SetHudTab(EDocentHudTab::Docent);
	Widget->SetHudTab(EDocentHudTab::Scan);
	Widget->SetHudTab(EDocentHudTab::Nav);
	Widget->SetHudTab(EDocentHudTab::Scan);

	TestEqual(TEXT("최종 탭 == Scan"), (uint8)Widget->GetHudTab(), (uint8)EDocentHudTab::Scan);
	CheckVis(TEXT("ScanPanel"), ESlateVisibility::SelfHitTestInvisible, TEXT("Final"));
	CheckVis(TEXT("NavPanel"), ESlateVisibility::Collapsed, TEXT("Final"));
	CheckChatPanel(ESlateVisibility::Collapsed, TEXT("Final"));

	Widget->RemoveFromRoot();
	return true;
}
#endif
