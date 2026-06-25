#include "MechRaidMainMenu.h"
// 우리가 만든 버튼 클래스를 사용하기 위해 헤더 포함
#include "CommonUI/Public/CommonButtonBase.h" 

void UMechRaidMainMenu::NativeConstruct()
{
	Super::NativeConstruct();

	// 버튼이 블루프린트에서 정상적으로 연결되었다면(null이 아니라면)
	if (Btn_StartGame)
	{
		Btn_StartGame->OnClicked().AddUObject(this, &UMechRaidMainMenu::OnStartGameClicked);
	}
}

// 버튼이 실제로 클릭되었을 때 실행될 로직
void UMechRaidMainMenu::OnStartGameClicked()
{
	// 테스트용 로그 출력 (화면 좌측 상단과 출력 로그 창에 노란색으로 뜹니다)
	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, TEXT("게임 시작 버튼이 C++에서 눌렸습니다!"));
	
	// 나중에는 여기에 "서버 접속 로직" 이나 "레벨 이동 로직"을 넣게 됩니다.
}

void UMechRaidMainMenu::OnQuitGameClicked()
{
	// 게임 종료 로직
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}