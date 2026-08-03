#include "DocentChatWidget.h"

#include "DocentChatBubble.h"
#include "DocentQuickChip.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogDocentChat, Log, All);

void UDocentChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (DocentNameText != nullptr)
	{
		DocentNameText->SetText(DocentName);
	}

	if (SendButton != nullptr)
	{
		SendButton->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleSendClicked);
	}
	if (InputBox != nullptr)
	{
		// 모바일 가상 키보드의 "완료" 로도 보낼 수 있어야 한다. 화면 아래쪽
		// 전송 버튼이 키보드에 가리는 경우가 있다.
		InputBox->OnTextCommitted.AddUniqueDynamic(this, &UDocentChatWidget::HandleTextCommitted);
	}

	BuildQuickQuestions();

	// 인사말은 서버를 거치지 않는다. 창을 열자마자 보여야 하는데 LLM 왕복을
	// 기다리면 빈 화면이 남는다.
	if (!GreetingText.IsEmpty())
	{
		AddBubble(/*bIsUser=*/false, GreetingText.ToString());
	}

	UGameInstance* GameInstance = GetGameInstance();
	Client = GameInstance != nullptr ? GameInstance->GetSubsystem<UDocentClient>() : nullptr;
	if (Client == nullptr)
	{
		UE_LOG(LogDocentChat, Error, TEXT("DocentClient 서브시스템을 찾지 못했습니다."));
		SetInputEnabled(false);
		return;
	}

	Client->OnChatSessionReady.AddUniqueDynamic(this, &UDocentChatWidget::HandleSessionReady);
	Client->OnChatDelta.AddUniqueDynamic(this, &UDocentChatWidget::HandleDelta);
	Client->OnChatCompleted.AddUniqueDynamic(this, &UDocentChatWidget::HandleCompleted);
	Client->OnChatFailed.AddUniqueDynamic(this, &UDocentChatWidget::HandleFailed);

	// 세션이 열리기 전에는 보낼 수 없다.
	SetInputEnabled(false);

	if (!Client->GetChatSessionId().IsEmpty())
	{
		// 창을 닫았다 다시 연 경우. 세션을 새로 만들면 앞선 맥락을 잃는다.
		HandleSessionReady(Client->GetChatSessionId());
	}
	else
	{
		Client->StartChatSession(ExhibitId);
	}
}

void UDocentChatWidget::NativeDestruct()
{
	// 서브시스템은 위젯보다 오래 산다. 언바인드하지 않으면 죽은 위젯으로
	// 브로드캐스트가 계속 날아간다.
	if (Client != nullptr)
	{
		Client->OnChatSessionReady.RemoveDynamic(this, &UDocentChatWidget::HandleSessionReady);
		Client->OnChatDelta.RemoveDynamic(this, &UDocentChatWidget::HandleDelta);
		Client->OnChatCompleted.RemoveDynamic(this, &UDocentChatWidget::HandleCompleted);
		Client->OnChatFailed.RemoveDynamic(this, &UDocentChatWidget::HandleFailed);
	}

	if (SendButton != nullptr)
	{
		SendButton->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleSendClicked);
	}
	if (InputBox != nullptr)
	{
		InputBox->OnTextCommitted.RemoveDynamic(this, &UDocentChatWidget::HandleTextCommitted);
	}

	Super::NativeDestruct();
}

void UDocentChatWidget::OpenChat(const FString& InExhibitId)
{
	ExhibitId = InExhibitId;

	if (Client == nullptr)
	{
		return;
	}

	SetInputEnabled(false);
	Client->StartChatSession(ExhibitId);
}

void UDocentChatWidget::SendQuestion(const FString& Question)
{
	const FString Trimmed = Question.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || Client == nullptr)
	{
		return;
	}

	// 여기서 먼저 막는다. 서브시스템도 같은 검사를 하지만 거기서 걸리면
	// OnChatFailed 가 떠서, 사용자 눈에는 이유 없는 오류 말풍선이 생긴다.
	if (Client->GetChatSessionId().IsEmpty() || Client->IsChatStreaming())
	{
		return;
	}

	AddBubble(/*bIsUser=*/true, Trimmed);

	// 응답이 들어올 빈 말풍선을 미리 만들어 두고 델타로 채운다.
	StreamingBubble = AddBubble(/*bIsUser=*/false, FString());

	Client->SendChatMessage(Trimmed, FocusedPoiId);

	if (InputBox != nullptr)
	{
		InputBox->SetText(FText::GetEmpty());
	}

	if (!bHasAskedOnce)
	{
		bHasAskedOnce = true;
		if (bHideChipsAfterFirstQuestion && QuickQuestionBox != nullptr)
		{
			QuickQuestionBox->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	SetInputEnabled(false);
	OnStreamingChanged(true);
	ScrollToLatest();
}

void UDocentChatWidget::ClearMessages()
{
	if (ChatScroll != nullptr)
	{
		ChatScroll->ClearChildren();
	}
	StreamingBubble = nullptr;
}

void UDocentChatWidget::HandleSessionReady(const FString& SessionId)
{
	SetInputEnabled(true);
	OnChatReady();
}

void UDocentChatWidget::HandleDelta(const FString& Text)
{
	if (StreamingBubble == nullptr)
	{
		// 창을 새로 연 직후 이전 요청의 잔여 조각이 올 수 있다. 버리지 않고
		// 새 말풍선에 담는다.
		StreamingBubble = AddBubble(/*bIsUser=*/false, FString());
	}

	StreamingBubble->AppendText(Text);
	ScrollToLatest();
}

void UDocentChatWidget::HandleCompleted(const FDocentChatResult& Result)
{
	StreamingBubble = nullptr;
	SetInputEnabled(true);
	OnStreamingChanged(false);
	ScrollToLatest();
}

void UDocentChatWidget::HandleFailed(const FString& Reason, bool bPartial)
{
	UE_LOG(LogDocentChat, Warning, TEXT("대화 실패: %s (부분응답=%s)"), *Reason,
		bPartial ? TEXT("있음") : TEXT("없음"));

	if (StreamingBubble != nullptr)
	{
		if (bPartial)
		{
			// 이미 받은 조각은 유효하다. 지우면 관람객이 읽던 문장이 사라진다.
			StreamingBubble->AppendText(TEXT("\n\n(답변이 중간에 끊겼어요)"));
		}
		else
		{
			StreamingBubble->SetText(TEXT("답변을 가져오지 못했어요. 잠시 후 다시 시도해 주세요."));
		}
		StreamingBubble = nullptr;
	}
	else
	{
		// 세션 생성 실패처럼 말풍선이 없는 시점의 오류.
		AddBubble(/*bIsUser=*/false, TEXT("도슨트에 연결하지 못했어요. 네트워크를 확인해 주세요."));
	}

	SetInputEnabled(true);
	OnStreamingChanged(false);
	ScrollToLatest();
}

void UDocentChatWidget::HandleSendClicked()
{
	if (InputBox != nullptr)
	{
		SendQuestion(InputBox->GetText().ToString());
	}
}

void UDocentChatWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 포커스가 빠지는 것만으로 보내면 안 된다.
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SendQuestion(Text.ToString());
	}
}

void UDocentChatWidget::HandleQuickChipClicked(const FString& Question)
{
	SendQuestion(Question);
}

UDocentChatBubble* UDocentChatWidget::AddBubble(bool bIsUser, const FString& Text)
{
	if (BubbleClass == nullptr || ChatScroll == nullptr)
	{
		UE_LOG(LogDocentChat, Error,
			TEXT("BubbleClass 또는 ChatScroll 이 없습니다. WBP 클래스 기본값을 확인하세요."));
		return nullptr;
	}

	UDocentChatBubble* Bubble = CreateWidget<UDocentChatBubble>(this, BubbleClass);
	if (Bubble == nullptr)
	{
		return nullptr;
	}

	// Setup 이 OnRoleApplied 를 부르므로 화면에 붙이기 전에 스타일이 정해진다.
	Bubble->Setup(bIsUser, Text);
	ChatScroll->AddChild(Bubble);
	return Bubble;
}

void UDocentChatWidget::BuildQuickQuestions()
{
	if (QuickQuestionBox == nullptr)
	{
		return;
	}

	QuickQuestionBox->ClearChildren();

	if (ChipClass == nullptr)
	{
		if (QuickQuestions.Num() > 0)
		{
			UE_LOG(LogDocentChat, Warning,
				TEXT("ChipClass 가 없어 빠른 질문을 만들지 못했습니다."));
		}
		return;
	}

	for (const FString& Question : QuickQuestions)
	{
		UDocentQuickChip* Chip = CreateWidget<UDocentQuickChip>(this, ChipClass);
		if (Chip == nullptr)
		{
			continue;
		}

		Chip->SetQuestion(Question);
		Chip->OnClicked.BindUObject(this, &UDocentChatWidget::HandleQuickChipClicked);
		QuickQuestionBox->AddChild(Chip);
	}
}

void UDocentChatWidget::SetInputEnabled(bool bEnabled)
{
	if (SendButton != nullptr)
	{
		SendButton->SetIsEnabled(bEnabled);
	}
	if (InputBox != nullptr)
	{
		InputBox->SetIsEnabled(bEnabled);
	}
	if (QuickQuestionBox != nullptr && !bHasAskedOnce)
	{
		QuickQuestionBox->SetIsEnabled(bEnabled);
	}
}

void UDocentChatWidget::ScrollToLatest()
{
	if (ChatScroll != nullptr)
	{
		ChatScroll->ScrollToEnd();
	}
}
