#include "DocentChatWidget.h"

#include "DocentChatBubble.h"
#include "DocentQuickChip.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "TimerManager.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDocentChat, Log, All);

namespace
{
#if PLATFORM_ANDROID
/**
 * 가상 키보드가 화면 아래에서 차지하는 비율(0~1). 알 수 없으면 -1.
 *
 * GameActivity 에 UPL 로 심어 둔 메서드를 부른다. 안드로이드 11 의
 * WindowInsets.Type.ime() 를 읽으므로 창이 리사이즈되지 않는 몰입 모드에서도
 * 정확하다. 자세한 배경은 TimeMachineAR_UPL.xml 주석에 있다.
 */
float QueryImeInsetRatio()
{
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (Env == nullptr)
	{
		return -1.0f;
	}

	// UPL 주입이 빠진 빌드에서도 죽지 않게 선택 조회로 찾는다.
	static jmethodID Method = FJavaWrapper::FindMethod(
		Env, FJavaWrapper::GameActivityClassID,
		"AndroidThunkJava_GetImeInsetPermyriad", "()I", /*bIsOptional=*/true);
	if (Method == nullptr)
	{
		return -1.0f;
	}

	const int32 Permyriad = FJavaWrapper::CallIntMethod(Env, FJavaWrapper::GameActivityThis, Method);
	return (Permyriad < 0) ? -1.0f : (Permyriad / 10000.0f);
}
#endif
}

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
	if (CloseButton != nullptr)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleCloseClicked);
	}
	if (OpenButton != nullptr)
	{
		OpenButton->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleOpenClicked);
	}

	// 안드로이드 가상 키보드. Slate 는 이 이벤트를 아무도 받지 않아서, 받아 두지
	// 않으면 키보드가 화면 아래 절반을 덮은 채 입력창을 가린다. 안드로이드에서만
	// 브로드캐스트되고 다른 플랫폼에서는 조용히 아무 일도 일어나지 않는다.
	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<GenericApplication> PlatformApp = FSlateApplication::Get().GetPlatformApplication())
		{
			TWeakObjectPtr<UDocentChatWidget> WeakThis(this);

			KeyboardShownHandle = PlatformApp->OnVirtualKeyboardShown().AddLambda(
				[WeakThis](FPlatformRect KeyboardRect)
				{
					if (UDocentChatWidget* Self = WeakThis.Get())
					{
						Self->ReportedKeyboardPixels = static_cast<float>(KeyboardRect.Bottom - KeyboardRect.Top);
						UE_LOG(LogDocentChat, Log, TEXT("[키보드] 플랫폼 보고: %.0fpx"),
							Self->ReportedKeyboardPixels);
						if (Self->bInputFocused)
						{
							Self->ApplyKeyboardInset(Self->ResolveKeyboardPixels());
						}
					}
				});

			KeyboardHiddenHandle = PlatformApp->OnVirtualKeyboardHidden().AddLambda(
				[WeakThis]()
				{
					if (UDocentChatWidget* Self = WeakThis.Get())
					{
						Self->ReportedKeyboardPixels = 0.0f;
						Self->ApplyKeyboardInset(0.0f);
					}
				});
		}
	}

	// 포커스 감시. 0.1 초면 키보드가 올라오는 동안(대략 250ms)에 따라붙는다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			InputFocusTimer, this, &UDocentChatWidget::PollInputFocus, 0.1f, /*bLoop=*/true);
	}

	// 여는 버튼이 없으면 닫힌 채로 시작할 수 없다. 다시 열 방법이 사라진다.
	const bool bCanReopen = (OpenButton != nullptr);
	if (!bStartOpen && !bCanReopen)
	{
		UE_LOG(LogDocentChat, Warning,
			TEXT("OpenButton 이 없어 닫힌 채로 시작할 수 없습니다. 열린 상태로 진행합니다."));
	}
	ApplyOpenState(bStartOpen || !bCanReopen);

	BuildQuickQuestions();

	// 인사말은 서버를 거치지 않는다. 창을 열자마자 보여야 하는데 LLM 왕복을
	// 기다리면 빈 화면이 남는다.
	if (GreetingLabel != nullptr)
	{
		GreetingLabel->SetText(GreetingText);
	}
	if (EmptyStateBox == nullptr && !GreetingText.IsEmpty())
	{
		// 빈 상태 블록이 없는 구성. 인사말을 보여 줄 자리가 말풍선뿐이다.
		// 블록이 있는데도 여기서 붙이면 같은 문장이 화면에 두 번 나온다.
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
	if (CloseButton != nullptr)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleCloseClicked);
	}
	if (OpenButton != nullptr)
	{
		OpenButton->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleOpenClicked);
	}

	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<GenericApplication> PlatformApp = FSlateApplication::Get().GetPlatformApplication())
		{
			PlatformApp->OnVirtualKeyboardShown().Remove(KeyboardShownHandle);
			PlatformApp->OnVirtualKeyboardHidden().Remove(KeyboardHiddenHandle);
		}
	}
	KeyboardShownHandle.Reset();
	KeyboardHiddenHandle.Reset();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InputFocusTimer);
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

		// 빈 상태(아바타 + 인사말)는 첫 질문과 함께 걷어낸다. 대화가 시작된
		// 뒤에도 화면 절반을 차지하면 말풍선이 들어갈 자리가 없다.
		if (EmptyStateBox != nullptr)
		{
			EmptyStateBox->SetVisibility(ESlateVisibility::Collapsed);
		}
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

void UDocentChatWidget::ShowChat()
{
	ApplyOpenState(true);
}

void UDocentChatWidget::HideChat()
{
	ApplyOpenState(false);
}

void UDocentChatWidget::ToggleChat()
{
	ApplyOpenState(!bIsOpen);
}

void UDocentChatWidget::ApplyOpenState(bool bOpen)
{
	bIsOpen = bOpen;

	if (ChatPanel != nullptr)
	{
		// SelfHitTestInvisible 이 아니라 Visible 이다. 패널 안의 버튼·입력창이
		// 터치를 받아야 한다.
		ChatPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	else
	{
		// 패널을 따로 두지 않은 구성. 이 경우 OpenButton 도 같이 숨겨지므로
		// 다시 열려면 바깥에서 ShowChat 을 불러야 한다.
		SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 배경은 안전영역 밖이라 ChatPanel 에 딸려 가지 않는다. 따로 접어 주지 않으면
	// 채팅을 닫아도 배경만 남아 AR 카메라가 보이지 않는다.
	if (ChatBackdrop != nullptr)
	{
		ChatBackdrop->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (OpenButton != nullptr)
	{
		OpenButton->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	// 하단 바는 AR 화면의 것이다. 채팅이 덮고 있는 동안에는 눌러 봐야 보이지도
	// 않는 화면을 조작하게 되므로 같이 접는다.
	if (BottomBar != nullptr)
	{
		BottomBar->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (!bOpen)
	{
		// 키보드가 내려가므로 밀어 둔 만큼도 같이 되돌린다. OnVirtualKeyboardHidden
		// 이 오기는 하지만, 그 사이 한 프레임 동안 빈 칸이 남는다.
		ApplyKeyboardInset(0.0f);

		if (FSlateApplication::IsInitialized())
		{
			// 포커스를 놓지 않으면 창을 닫아도 안드로이드 가상 키보드가 남는다.
			FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
		}
	}

	OnChatOpenChanged(bOpen);
}

void UDocentChatWidget::PollInputFocus()
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	if (InputBox == nullptr || KeyboardSpacer == nullptr)
	{
		return;
	}

	bInputFocused = bIsOpen && InputBox->HasKeyboardFocus();

	// 포커스 변화만 보면 안 된다. 키보드가 올라오는 동안 IME 인셋이 0 에서부터
	// 자라기 때문에, 포커스를 얻은 그 순간에는 아직 0 으로 잡힌다.
	const float Target = bInputFocused ? ResolveKeyboardPixels() : 0.0f;
	if (FMath::IsNearlyEqual(Target, AppliedKeyboardPixels, 1.0f))
	{
		return;
	}

	AppliedKeyboardPixels = Target;
	ApplyKeyboardInset(Target);
#endif
}

float UDocentChatWidget::ResolveKeyboardPixels() const
{
	const float ViewportHeight = UWidgetLayoutLibrary::GetViewportSize(const_cast<UDocentChatWidget*>(this)).Y;
	if (ViewportHeight <= 0.0f)
	{
		return 0.0f;
	}

#if PLATFORM_ANDROID
	// 1순위. 안드로이드가 알려 주는 실제 IME 높이라 기기·키보드앱과 무관하게 맞다.
	// 0 은 "키보드가 아직 안 올라왔다" 는 정상값이므로 그대로 쓴다. 올라오는
	// 동안 값이 자라고, 폴링이 그걸 따라간다. -1 일 때만 알 수 없는 경우다.
	const float ImeRatio = QueryImeInsetRatio();
	if (ImeRatio >= 0.0f)
	{
		return ViewportHeight * ImeRatio;
	}
#endif

	// 2순위. UE 가 자체적으로 잰 값. 몰입 모드에서는 0 이나 음수로 나오므로
	// 화면의 15~75% 라는 그럴듯한 범위 안일 때만 믿는다.
	if (ReportedKeyboardPixels > ViewportHeight * 0.15f &&
		ReportedKeyboardPixels < ViewportHeight * 0.75f)
	{
		return ReportedKeyboardPixels;
	}

	// 3순위. 둘 다 실패했을 때의 어림값. 기기마다 어긋난다.
	UE_LOG(LogDocentChat, Warning,
		TEXT("[키보드] 실제 높이를 알 수 없어 비율 %.2f 로 대신합니다."), KeyboardHeightRatio);
	return ViewportHeight * KeyboardHeightRatio;
}

void UDocentChatWidget::ApplyKeyboardInset(float KeyboardPixels)
{
	if (KeyboardSpacer == nullptr)
	{
		return;
	}

	// 픽셀을 위젯 좌표로 바꾼다. 그대로 넣으면 고해상도 기기에서 키보드보다
	// 훨씬 크게 밀린다. S25+ 는 스케일이 1.33 이라 33% 더 밀린다.
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	const float SlateUnits = (Scale > 0.0f) ? (KeyboardPixels / Scale) : KeyboardPixels;

	// 안전영역이 이미 하단 제스처 바만큼 여백을 주고 있어서 그만큼 겹친다.
	// 몇십 단위라 눈에 띄지 않으므로 빼지 않는다.
	KeyboardSpacer->SetSize(FVector2D(0.0f, FMath::Max(0.0f, SlateUnits)));

	UE_LOG(LogDocentChat, Log, TEXT("[키보드] 여백 %.0fpx -> %.0f단위 (스케일 %.2f, 보고 %.0fpx)"),
		KeyboardPixels, SlateUnits, Scale, ReportedKeyboardPixels);

	// 밀린 만큼 마지막 말풍선이 가려지므로 다시 맨 아래로 보낸다.
	ScrollToLatest();
}

void UDocentChatWidget::HandleCloseClicked()
{
	HideChat();
}

void UDocentChatWidget::HandleOpenClicked()
{
	ShowChat();
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
