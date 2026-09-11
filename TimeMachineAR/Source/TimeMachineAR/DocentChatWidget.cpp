#include "DocentChatWidget.h"

#include "DocentChatBubble.h"
#include "DocentQuickChip.h"
#include "ARTrackingManager.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "TimerManager.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "DinoRegistry.h"
#include "DinoInfoData.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"

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
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("ReferenceUIPreview")))
	{
		FTimerHandle PreviewTimer;
		GetWorld()->GetTimerManager().SetTimer(PreviewTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/ReferenceUIPreview.png"), true, false);
		}), 8.f, false);
	}
#endif
	ApplyScanSkin();
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("RefCaptureButton")))) B->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleReferenceCapture);
	// 목업의 ↻ 는 카메라 전환이다. 다시 스캔은 셔터(스캔 중이 아닐 때)와 탭바가 맡는다.
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("RefRescanButton")))) B->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleCameraFlipClicked);
	for (const FName N : {FName(TEXT("ScanCloseButton")), FName(TEXT("NabButton"))})
		if (UButton* B = Cast<UButton>(GetWidgetFromName(N))) B->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleReferenceExit);

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
	ApplyChatSkin();
	{
		OpenButton->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleOpenClicked);
	}
	if (Btn_CloseAR != nullptr)
	{
		Btn_CloseAR->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleCloseARClicked);
		// 처음에는 숨김
		Btn_CloseAR->SetVisibility(ESlateVisibility::Hidden);
	}

	if (AARTrackingManager* TrackingMgr = AARTrackingManager::GetARTrackingManager(this))
	{
		TrackingMgr->OnMarkerFound.AddUniqueDynamic(this, &UDocentChatWidget::HandleMarkerFound);
		TrackingMgr->OnScanStateChanged.AddUniqueDynamic(this, &UDocentChatWidget::HandleScanStateChanged);
		TrackingMgr->OnCameraFacingChanged.AddUniqueDynamic(this, &UDocentChatWidget::HandleCameraFacingChanged);
		bFrontCamera = TrackingMgr->IsFrontCamera();
	}

	// 안드로이드 가상 키보드. Slate 는 이 이벤트를 아무도 받지 않아서, 받아 두지
	// 않으면 키보드가 화면 아래 절반을 덮은 채 입력창을 가린다. 안드로이드에서만
	// 브로드캐스트되고 다른 플랫폼에서는 조용히 아무 일도 일어나지 않는다.
	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<GenericApplication> PlatformApp = FSlateApplication::Get().GetPlatformApplication())
	if (UButton* Menu = Cast<UButton>(GetWidgetFromName(TEXT("MenuButton"))))
	{
		Menu->OnClicked.AddUniqueDynamic(this, &UDocentChatWidget::HandleMenuClicked);
	}
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

	// 마커로 공룡을 고르기 전에는 특정 전시물을 상정하지 않는다. 클래스
	// 기본값의 ExhibitKey 는 마커가 없던 시절의 시험용이라 여기서 버린다.
	// OpenChat 이 먼저 불려 전시물이 정해진 경우에만 그대로 둔다.
	if (!bExhibitContextSet)
	{
		ExhibitKey.Reset();
	}

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
		Client->StartChatSession(ExhibitKey);
	}
}

void UDocentChatWidget::NativeDestruct()
{
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("RefCaptureButton")))) B->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleReferenceCapture);
	if (UButton* B = Cast<UButton>(GetWidgetFromName(TEXT("RefRescanButton")))) B->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleCameraFlipClicked);
	for (const FName N : {FName(TEXT("ScanCloseButton")), FName(TEXT("NabButton"))})
		if (UButton* B = Cast<UButton>(GetWidgetFromName(N))) B->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleReferenceExit);
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
	if (Btn_CloseAR != nullptr)
	{
		Btn_CloseAR->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleCloseARClicked);
	}

	if (AARTrackingManager* TrackingMgr = AARTrackingManager::GetARTrackingManager(this))
	{
		TrackingMgr->OnMarkerFound.RemoveDynamic(this, &UDocentChatWidget::HandleMarkerFound);
		TrackingMgr->OnScanStateChanged.RemoveDynamic(this, &UDocentChatWidget::HandleScanStateChanged);
		TrackingMgr->OnCameraFacingChanged.RemoveDynamic(this, &UDocentChatWidget::HandleCameraFacingChanged);
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
	if (UButton* Menu = Cast<UButton>(GetWidgetFromName(TEXT("MenuButton"))))
	{
		Menu->OnClicked.RemoveDynamic(this, &UDocentChatWidget::HandleMenuClicked);
	}

	Super::NativeDestruct();
}

void UDocentChatWidget::OpenChat(const FString& InExhibitId)
{
	// 파라미터 이름은 BP 핀 호환을 위해 유지한다. 값은 안정 키(model_asset_key)다.
	ExhibitKey = InExhibitId;
	bExhibitContextSet = !InExhibitId.IsEmpty();

	// 공룡이 새로 정해졌으면 그 공룡용 칩을 다시 띄운다. 앞선 대화에서 이미
	// 질문을 했더라도, 방금 고른 공룡의 선택지는 보여 줘야 한다.
	if (bExhibitContextSet)
	{
		bHasAskedOnce = false;
	}
	BuildQuickQuestions();

	if (Client == nullptr)
	{
		return;
	}

	SetInputEnabled(false);
	Client->StartChatSession(ExhibitKey);
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
		ApplyQuickQuestionVisibility();
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
	RefreshReferenceUI();
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

void UDocentChatWidget::HandleCloseARClicked()
{
	bReferenceRecognized = false;
	if (AARTrackingManager* TrackingManager = AARTrackingManager::GetARTrackingManager(this))
	{
		TrackingManager->ClearOverlay();
	}
	if (Btn_CloseAR != nullptr)
	{
		Btn_CloseAR->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UDocentChatWidget::HandleMarkerFound(UARPin* Pin, const FTransform& MarkerPose, const FString& MarkerCode)
{
	bReferenceRecognized = true;
	if (AARTrackingManager* M = AARTrackingManager::GetARTrackingManager(this))
		if (M->DinoRegistry)
			if (UDinoInfoData* Info = M->DinoRegistry->FindByMarker(MarkerCode))
				SetLocationChip(Info->NameKo.ToString() + TEXT(" 전시존"), TEXT("현재 위치"), FLinearColor(0.23f, 0.9f, 0.44f, 1.f));
	// Existing Blueprint delegates hide the scan panel on recognition. Restore only
	// its nonblocking presentation after that broadcast has finished.
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWidget* Nav = GetWidgetFromName(TEXT("NavPanel"));
			if (bReferenceRecognized && !bIsOpen && (!Nav || Nav->GetVisibility() == ESlateVisibility::Collapsed))
				if (UWidget* Scan = GetWidgetFromName(TEXT("ScanPanel"))) Scan->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			RefreshReferenceUI();
		}));
	if (Btn_CloseAR != nullptr)
	{
		Btn_CloseAR->SetVisibility(ESlateVisibility::Visible);
	}
}

void UDocentChatWidget::HandleScanStateChanged(bool bIsScanning)
{
	if (bIsScanning) bReferenceRecognized = false;
	RefreshReferenceUI();
	if (Btn_CloseAR != nullptr && bIsScanning)
	{
		Btn_CloseAR->SetVisibility(ESlateVisibility::Hidden);
	}
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
	// 목업처럼 첫 답변 아래에 전시물 카드가 따라온다. AddExhibitCard 가 중복을 거른다.
	AddExhibitCard();
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

	// 공룡을 고르지 않은 일반 챗에서는 칩을 만들지 않는다. "이 공룡에 대해
	// 설명해줘" 같은 문구가 가리킬 대상이 없어서, 눌러도 맥락 없는 질문이 된다.
	if (!HasExhibitContext())
	{
		ApplyQuickQuestionVisibility();
		return;
	}

	if (ChipClass == nullptr)
	{
		if (QuickQuestions.Num() > 0)
		{
			UE_LOG(LogDocentChat, Warning,
				TEXT("ChipClass 가 없어 빠른 질문을 만들지 못했습니다."));
		}
		ApplyQuickQuestionVisibility();
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

	ApplyQuickQuestionVisibility();
}

void UDocentChatWidget::ApplyQuickQuestionVisibility()
{
	if (QuickQuestionBox == nullptr)
	{
		return;
	}

	const bool bShow = HasExhibitContext()
		&& QuickQuestionBox->GetChildrenCount() > 0
		&& !(bHideChipsAfterFirstQuestion && bHasAskedOnce);

	QuickQuestionBox->SetVisibility(
		bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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


void UDocentChatWidget::HandleReferenceRescan()
{
	bReferenceRecognized = false;
	if (!bFrontCamera)
	{
		SetLocationChip(TEXT("전시존 탐색 중"), TEXT("현재 위치 확인 중"), FLinearColor(0.6f, 0.6f, 0.65f, 1.f));
	}
	HideChat();
	if (UWidget* Nav = GetWidgetFromName(TEXT("NavPanel"))) Nav->SetVisibility(ESlateVisibility::Collapsed);
	if (UWidget* Scan = GetWidgetFromName(TEXT("ScanPanel"))) Scan->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (AARTrackingManager* Manager = AARTrackingManager::GetARTrackingManager(this)) Manager->StartScan();
	RefreshReferenceUI();
}

void UDocentChatWidget::HandleReferenceCapture()
{
	AARTrackingManager* Manager = AARTrackingManager::GetARTrackingManager(this);
	// 셀카 모드에서는 마커를 볼 수 없으므로 셔터는 늘 촬영이다.
	if (Manager && !Manager->IsScanning() && !bReferenceRecognized && !bFrontCamera)
	{
		HandleReferenceRescan();
		return;
	}
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/ARCapture.png"), true, true);
}

void UDocentChatWidget::HandleCameraFlipClicked()
{
	AARTrackingManager* Manager = AARTrackingManager::GetARTrackingManager(this);
	if (Manager == nullptr)
	{
		return;
	}
	// 후면으로 돌아왔을 때 사용자가 다시 스캔 버튼을 찾지 않아도 되게, 전환 전
	// 스캔 상태를 기억해 둔다. ToggleCameraFacing 이 스캔을 끄기 전에 읽어야 한다.
	bResumeScanAfterFlip = !Manager->IsFrontCamera() && Manager->IsScanning();
	bReferenceRecognized = false;
	Manager->ToggleCameraFacing();
}

void UDocentChatWidget::HandleCameraFacingChanged(bool bFront)
{
	bFrontCamera = bFront;
	if (!bFront && bResumeScanAfterFlip)
	{
		if (AARTrackingManager* Manager = AARTrackingManager::GetARTrackingManager(this))
		{
			Manager->StartScan();
		}
	}
	bResumeScanAfterFlip = false;
	if (bFront)
	{
		SetLocationChip(TEXT("셀카 모드"), TEXT("전면 카메라"), FLinearColor(0.55f, 0.5f, 1.f, 1.f));
	}
	else
	{
		SetLocationChip(TEXT("전시존 탐색 중"), TEXT("현재 위치 확인 중"), FLinearColor(0.6f, 0.6f, 0.65f, 1.f));
	}
	RefreshReferenceUI();
}

void UDocentChatWidget::HandleReferenceExit()
{
	bReferenceRecognized = false;
}

void UDocentChatWidget::RefreshReferenceUI()
{
	const AARTrackingManager* Manager = AARTrackingManager::GetARTrackingManager(this);
	const bool bScanning = Manager && Manager->IsScanning();
	const FLinearColor Accent = bFrontCamera ? FLinearColor(0.55f, 0.5f, 1.f, 1.f) :
		bReferenceRecognized ? FLinearColor(0.23f,0.9f,0.44f,1.f) :
		(bScanning ? FLinearColor(0.08f,0.43f,1.f,1.f) : FLinearColor::White);
	if (UImage* Frame = Cast<UImage>(GetWidgetFromName(TEXT("ScanFramImage"))))
	{
		Frame->SetColorAndOpacity(Accent);
		// 전면 카메라는 마커를 못 보므로 조준 틀을 걷는다. 인식된 뒤에는 조준원만 거둔다.
		Frame->SetVisibility(bFrontCamera ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (UImage* Reticle = Cast<UImage>(GetWidgetFromName(TEXT("RefReticleImage"))))
	{
		Reticle->SetVisibility((bFrontCamera || bReferenceRecognized) ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (UImage* Flip = Cast<UImage>(GetWidgetFromName(TEXT("RefRefreshIcon"))))
	{
		Flip->SetColorAndOpacity(bFrontCamera ? Accent : FLinearColor::White);
	}
	if (UTextBlock* Hint = Cast<UTextBlock>(GetWidgetFromName(TEXT("ScanHintText"))))
	{
		const FText Message = FText::FromString(bFrontCamera ?
			TEXT("셀카 모드예요!\n전환 버튼을 다시 누르면 후면 카메라로 돌아가요.") :
			bReferenceRecognized ?
			TEXT("대상을 인식했어요!\n공룡을 터치하면 정보를 볼 수 있어요.") :
			(bScanning ? TEXT("공룡 마커를 화면 중앙에 맞춰주세요\n더 선명하게 인식할 수 있어요!") :
			TEXT("AR 스캔을 눌러 시작해주세요\n전시물의 마커를 비춰주세요.")));
		if (!Hint->GetText().EqualTo(Message)) Hint->SetText(Message);
	}
	if (UBorder* Hint = Cast<UBorder>(GetWidgetFromName(TEXT("ScanHintBG"))))
	{
		FSlateBrush Brush = Hint->Background;
		Brush.OutlineSettings.Color = FSlateColor(Accent.CopyWithNewOpacity(0.6f));
		Hint->SetBrush(Brush);
	}
	UWidget* Scan = GetWidgetFromName(TEXT("ScanPanel"));
	const bool bScanVisible = !bIsOpen && Scan && Scan->GetVisibility() != ESlateVisibility::Collapsed;
	if (UImage* Icon = Cast<UImage>(GetWidgetFromName(TEXT("ScanIcon"))))
		Icon->SetColorAndOpacity(bScanVisible ? (bReferenceRecognized ? Accent : FLinearColor(0.08f,0.43f,1.f,1.f)) : FLinearColor::White);
	if (UTextBlock* Label = Cast<UTextBlock>(GetWidgetFromName(TEXT("ScanLabel"))))
		Label->SetColorAndOpacity(FSlateColor(bScanVisible ? (bReferenceRecognized ? Accent : FLinearColor(0.08f,0.43f,1.f,1.f)) : FLinearColor::White));
}

void UDocentChatWidget::ApplyScanSkin()
{
	if (WidgetTree == nullptr)
	{
		return;
	}
	// UMG 단위. 실기기(1440 폭)는 DPI 1.333 배로 그려진다.
	constexpr float TopInset = 48.f;
	constexpr float TopHeight = 116.f;
	const FLinearColor Panel = FLinearColor::FromSRGBColor(FColor(18, 22, 30, 215));
	const FLinearColor PanelOutline = FLinearColor(1.f, 1.f, 1.f, 0.14f);

	// 뒤로가기: 텍스트 글리프는 베이스라인 때문에 원 아래로 처진다. 아이콘으로 바꾼다.
	if (UButton* Back = Cast<UButton>(GetWidgetFromName(TEXT("ScanCloseButton"))))
	{
		FButtonStyle Style = Back->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(20, 22, 28, 180)), TopHeight * 0.5f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(40, 44, 54, 200)), TopHeight * 0.5f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(40, 44, 54, 200)), TopHeight * 0.5f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Back->SetStyle(Style);
		if (UTexture2D* Arrow = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Docent/arrow_back.arrow_back")))
		{
			UImage* Icon = WidgetTree->ConstructWidget<UImage>();
			Icon->SetBrushFromTexture(Arrow);
			Icon->SetDesiredSizeOverride(FVector2D(56.f, 56.f));
			Icon->SetColorAndOpacity(FLinearColor::White);
			Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
			Back->SetContent(Icon);
			if (UButtonSlot* IconSlot = Cast<UButtonSlot>(Icon->Slot))
			{
				IconSlot->SetPadding(FMargin(0.f));
				IconSlot->SetHorizontalAlignment(HAlign_Center);
				IconSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
	}
	if (USizeBox* BackSize = Cast<USizeBox>(GetWidgetFromName(TEXT("RefBackSize"))))
	{
		BackSize->SetWidthOverride(TopHeight);
		BackSize->SetHeightOverride(TopHeight);
		if (UOverlaySlot* S = Cast<UOverlaySlot>(BackSize->Slot))
		{
			S->SetPadding(FMargin(38.f, TopInset, 0.f, 0.f));
		}
	}

	// 위치 칩: 두 줄 텍스트 상자 → 제목 + "현재 위치 ●" 캡슐. 뒤로가기와 같은 높이로 맞춘다.
	UTextBlock* Title = Cast<UTextBlock>(GetWidgetFromName(TEXT("RefLocationText")));
	UBorder* Chip = Cast<UBorder>(GetWidgetFromName(TEXT("RefLocationPanel")));
	if (Title != nullptr && Chip != nullptr)
	{
		if (USizeBox* ChipSize = Cast<USizeBox>(GetWidgetFromName(TEXT("RefLocationSize"))))
		{
			ChipSize->ClearWidthOverride();
			ChipSize->SetMinDesiredWidth(360.f);
			ChipSize->SetHeightOverride(TopHeight);
			if (UOverlaySlot* S = Cast<UOverlaySlot>(ChipSize->Slot))
			{
				S->SetPadding(FMargin(38.f + TopHeight + 16.f, TopInset, 0.f, 0.f));
			}
		}
		Chip->SetBrush(FSlateRoundedBoxBrush(Panel, TopHeight * 0.5f, PanelOutline, 1.5f));
		Chip->SetBrushColor(FLinearColor::White);
		Chip->SetPadding(FMargin(32.f, 0.f, 36.f, 0.f));
		Chip->SetVerticalAlignment(VAlign_Center);
		Chip->SetHorizontalAlignment(HAlign_Left);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Title->RemoveFromParent();
		FSlateFontInfo TitleFont = Title->GetFont();
		TitleFont.Size = 30;
		Title->SetFont(TitleFont);
		Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Title->SetAutoWrapText(false);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Title))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		LocationSubText = WidgetTree->ConstructWidget<UTextBlock>();
		FSlateFontInfo SubFont = TitleFont;
		SubFont.Size = 23;
		LocationSubText->SetFont(SubFont);
		LocationSubText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.84f, 1.f)));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(LocationSubText))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		LocationDot = WidgetTree->ConstructWidget<UImage>();
		LocationDot->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 8.f));
		LocationDot->SetDesiredSizeOverride(FVector2D(16.f, 16.f));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(LocationDot))
		{
			S->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
			S->SetVerticalAlignment(VAlign_Center);
		}
		Column->AddChildToVerticalBox(Row);
		Chip->SetContent(Column);

		// WBP 기본 문구가 두 줄이라 제목·부제로 나눈다.
		SetLocationChip(TEXT("전시존 탐색 중"), TEXT("현재 위치 확인 중"), FLinearColor(0.6f, 0.6f, 0.65f, 1.f));
	}

	// 셔터: 버튼 여백이 [12,2,12,2] 라 안쪽 원이 146x166 타원으로 그려졌다.
	// 목업은 흰 링 + 틈 + 흰 원이다. 링은 버튼 외곽선, 원은 안쪽 이미지로 그린다.
	if (UButton* Shutter = Cast<UButton>(GetWidgetFromName(TEXT("RefCaptureButton"))))
	{
		constexpr float Size = 170.f;
		constexpr float Gap = 14.f;
		FButtonStyle Style = Shutter->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.25f), Size * 0.5f, FLinearColor::White, 4.f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.25f), Size * 0.5f, FLinearColor::White, 4.f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.35f), Size * 0.5f, FLinearColor::White, 4.f));
		Style.SetNormalPadding(FMargin(Gap));
		Style.SetPressedPadding(FMargin(Gap));
		Shutter->SetStyle(Style);
		if (UImage* Inner = Cast<UImage>(GetWidgetFromName(TEXT("RefShutterInner"))))
		{
			Inner->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, (Size - Gap * 2.f) * 0.5f));
			Inner->SetColorAndOpacity(FLinearColor::White);
			if (UButtonSlot* S = Cast<UButtonSlot>(Inner->Slot))
			{
				S->SetPadding(FMargin(0.f));
				S->SetHorizontalAlignment(HAlign_Fill);
				S->SetVerticalAlignment(VAlign_Fill);
			}
		}
	}

	// 전환 버튼: 아이콘이 정중앙에 오도록 비대칭 여백을 없앤다.
	if (UButton* Flip = Cast<UButton>(GetWidgetFromName(TEXT("RefRescanButton"))))
	{
		FButtonStyle Style = Flip->GetStyle();
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Flip->SetStyle(Style);
		if (UImage* Icon = Cast<UImage>(GetWidgetFromName(TEXT("RefRefreshIcon"))))
		{
			if (UButtonSlot* S = Cast<UButtonSlot>(Icon->Slot))
			{
				S->SetHorizontalAlignment(HAlign_Center);
				S->SetVerticalAlignment(VAlign_Center);
			}
		}
	}
}

void UDocentChatWidget::SetLocationChip(const FString& Title, const FString& Subtitle, const FLinearColor& DotColor)
{
	if (UTextBlock* TitleText = Cast<UTextBlock>(GetWidgetFromName(TEXT("RefLocationText"))))
	{
		// 칩을 못 꾸민 구성(부제 위젯 없음)에서는 예전처럼 두 줄로 넣는다.
		TitleText->SetText(FText::FromString(LocationSubText ? Title : Title + TEXT("\n") + Subtitle));
	}
	if (LocationSubText != nullptr)
	{
		LocationSubText->SetText(FText::FromString(Subtitle));
	}
	if (LocationDot != nullptr)
	{
		LocationDot->SetColorAndOpacity(DotColor);
	}
}

// ------------------------------------------------------------------ 채팅 스킨

namespace
{
	/** /Game/UI/Docent/Skin 의 텍스처. 없으면 nullptr — 호출부가 그냥 건너뛴다. */
	UTexture2D* DocentSkinTexture(const TCHAR* Name)
	{
		return LoadObject<UTexture2D>(nullptr,
			*FString::Printf(TEXT("/Game/UI/Docent/Skin/%s.%s"), Name, Name));
	}

	/**
	 * 둥근 유리 원 + 흰 아이콘 버튼. ApplyScanSkin 의 뒤로가기와 같은 만듦새다.
	 *
	 * 시트의 원형 버튼 스프라이트는 거의 검정이라 어두운 배경에서 안 보인다.
	 * 원은 브러시로 그리고 아이콘은 프로젝트의 흰 Material 아이콘을 쓴다.
	 */
	void MakeRoundIconButton(UButton* Button, const TCHAR* IconPath, float Diameter, float IconSize)
	{
		if (Button == nullptr)
		{
			return;
		}
		const FLinearColor Glass   = FLinearColor::FromSRGBColor(FColor(22, 27, 36, 215));
		const FLinearColor GlassHi = FLinearColor::FromSRGBColor(FColor(38, 45, 58, 235));
		const FLinearColor Outline = FLinearColor(1.f, 1.f, 1.f, 0.10f);

		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal (FSlateRoundedBoxBrush(Glass,   Diameter * 0.5f, Outline, 2.f));
		Style.SetHovered(FSlateRoundedBoxBrush(GlassHi, Diameter * 0.5f, Outline, 2.f));
		Style.SetPressed(FSlateRoundedBoxBrush(GlassHi, Diameter * 0.5f, Outline, 2.f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Button->SetStyle(Style);

		UWidgetTree* Tree = Cast<UWidgetTree>(Button->GetOuter());
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, IconPath);
		if (Tree == nullptr || Tex == nullptr)
		{
			return;
		}
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(Diameter);
		Box->SetHeightOverride(Diameter);
		UImage* Icon = Tree->ConstructWidget<UImage>();
		Icon->SetBrushFromTexture(Tex);
		Icon->SetDesiredSizeOverride(FVector2D(IconSize, IconSize));
		Icon->SetColorAndOpacity(FLinearColor::White);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		Box->SetContent(Icon);
		if (USizeBoxSlot* IS = Cast<USizeBoxSlot>(Icon->Slot))
		{
			IS->SetHorizontalAlignment(HAlign_Center);
			IS->SetVerticalAlignment(VAlign_Center);
		}
		Button->SetContent(Box);
	}
}

void UDocentChatWidget::ApplyChatSkin()
{
	if (WidgetTree == nullptr)
	{
		return;
	}

	// 레퍼런스 목업은 패널 폭 512px 이다. UMG 단위(실기기 1440 폭이 DPI 1.333 으로
	// 1080)로 옮기면 약 2.1 배다. 아래 수치는 그 비율로 잰 값이다.
	const FLinearColor Glass    = FLinearColor::FromSRGBColor(FColor(22, 27, 36, 215));
	const FLinearColor GlassHi  = FLinearColor::FromSRGBColor(FColor(38, 45, 58, 235));
	const FLinearColor Outline  = FLinearColor(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor TextMain = FLinearColor::FromSRGBColor(FColor(240, 244, 250));
	const FLinearColor TextDim  = FLinearColor::FromSRGBColor(FColor(160, 170, 185));

	// ---- 배경: 사진 위 어두운 반투명 막. 목업의 유리 느낌은 이 한 겹이 만든다.
	if (UImage* Backdrop = Cast<UImage>(ChatBackdrop))
	{
		Backdrop->SetColorAndOpacity(FLinearColor::FromSRGBColor(FColor(10, 13, 20, 200)));
	}

	// ---- 상단 바: 원형 뒤로가기 / 제목 / 원형 더보기
	MakeRoundIconButton(CloseButton, TEXT("/Game/UI/Docent/arrow_back.arrow_back"), 92.f, 48.f);
	MakeRoundIconButton(Cast<UButton>(GetWidgetFromName(TEXT("MenuButton"))),
		TEXT("/Game/UI/Docent/more_vert.more_vert"), 92.f, 48.f);
	if (DocentNameText != nullptr)
	{
		FSlateFontInfo Font = DocentNameText->GetFont();
		Font.Size = 40;
		DocentNameText->SetFont(Font);
		DocentNameText->SetColorAndOpacity(FSlateColor(TextMain));
		DocentNameText->SetJustification(ETextJustify::Center);
	}

	// ---- 시작 화면: 링 안의 렉시 + 인사말
	if (UImage* Hero = Cast<UImage>(GetWidgetFromName(TEXT("Avatar"))))
	{
		if (UTexture2D* Halo = DocentSkinTexture(TEXT("lexi_halo")))
		{
			Hero->SetBrushFromTexture(Halo);
			Hero->SetDesiredSizeOverride(FVector2D(420.f, 450.f));
			Hero->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	// 시작 화면 덩어리(렉시 + 인사말)를 추천 질문 바로 위에 붙인다. 가운데 정렬로 두면
	// 남는 공간이 인사말과 추천 질문 사이에 끼어 목업과 달리 둘이 멀어진다.
	if (EmptyStateBox != nullptr)
	{
		if (UOverlaySlot* S = Cast<UOverlaySlot>(EmptyStateBox->Slot))
		{
			S->SetVerticalAlignment(VAlign_Bottom);
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 36.f));
		}
	}
	if (GreetingLabel != nullptr)
	{
		FSlateFontInfo Font = GreetingLabel->GetFont();
		Font.Size = 32;
		GreetingLabel->SetFont(Font);
		GreetingLabel->SetColorAndOpacity(FSlateColor(TextMain));
		GreetingLabel->SetJustification(ETextJustify::Center);
		GreetingLabel->SetLineHeightPercentage(1.35f);
	}

	// ---- 입력창: 유리 캡슐 + 원형 보내기
	if (InputBox != nullptr)
	{
		FEditableTextBoxStyle Style = InputBox->WidgetStyle;
		Style.SetBackgroundImageNormal (FSlateRoundedBoxBrush(Glass,   28.f, Outline, 2.f));
		Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Glass,   28.f, Outline, 2.f));
		Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(GlassHi, 28.f, Outline, 2.f));
		Style.SetPadding(FMargin(34.f, 26.f));
		Style.SetForegroundColor(FSlateColor(TextMain));
		FSlateFontInfo Font = Style.TextStyle.Font;
		Font.Size = 30;
		Style.TextStyle.SetFont(Font);
		InputBox->WidgetStyle = Style;
		InputBox->SetHintText(FText::FromString(TEXT("메시지를 입력하세요...")));
		InputBox->SynchronizeProperties();
	}
	if (SendButton != nullptr)
	{
		FButtonStyle Style = SendButton->GetStyle();
		Style.SetNormal (FSlateRoundedBoxBrush(Glass,   50.f, Outline, 2.f));
		Style.SetHovered(FSlateRoundedBoxBrush(GlassHi, 50.f, Outline, 2.f));
		Style.SetPressed(FSlateRoundedBoxBrush(GlassHi, 50.f, Outline, 2.f));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		SendButton->SetStyle(Style);
	}
	if (UImage* Send = Cast<UImage>(GetWidgetFromName(TEXT("SendIcon"))))
	{
		if (UTexture2D* Tex = DocentSkinTexture(TEXT("icon_send")))
		{
			Send->SetBrushFromTexture(Tex);
			Send->SetDesiredSizeOverride(FVector2D(48.f, 48.f));
			Send->SetColorAndOpacity(TextMain);
		}
	}
}


void UDocentChatWidget::HandleMenuClicked()
{
	AddTopicMenu();
}

namespace
{
	/**
	 * 말풍선 열과 같은 왼쪽 선(아바타 폭 + 간격)에 맞춘 스크롤 항목 여백.
	 * ChatScroll 자체가 좌우 36 을 더 안으로 들이므로, 그만큼 뺀 값이다.
	 */
	const FMargin ChatInsertMargin(82.f, 10.f, 14.f, 10.f);
	/** 위 여백을 뺀 항목 폭. 1080 - 36*2 - 82 - 14. */
	const float ChatInsertWidth = 912.f;

	/** 유리 캡슐 태그 하나. 카드 아래 "육식 / 백악기 후기" 줄에 쓴다. */
	UWidget* MakeTagPill(UWidgetTree* Tree, const FText& Label)
	{
		const FLinearColor Glass    = FLinearColor::FromSRGBColor(FColor(22, 27, 36, 215));
		const FLinearColor Outline  = FLinearColor(1.f, 1.f, 1.f, 0.14f);
		const FLinearColor TextMain = FLinearColor::FromSRGBColor(FColor(240, 244, 250));

		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(Label);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = 24;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(TextMain));

		UBorder* Pill = Tree->ConstructWidget<UBorder>();
		Pill->SetBrush(FSlateRoundedBoxBrush(Glass, 22.f, Outline, 2.f));
		Pill->SetPadding(FMargin(24.f, 10.f, 24.f, 10.f));
		Pill->SetContent(Text);
		return Pill;
	}
}

void UDocentChatWidget::AddExhibitCard()
{
	if (WidgetTree == nullptr || ChatScroll == nullptr || !HasExhibitContext())
	{
		return;
	}
	if (ExhibitCardShownFor == ExhibitKey)
	{
		return;
	}

	// 카드에 담을 공룡. 레지스트리의 ExhibitKey 와 대화 키가 같은 종을 찾는다.
	UDinoInfoData* Info = nullptr;
	if (UDinoRegistry* Registry = Cast<UDinoRegistry>(StaticLoadObject(
		UDinoRegistry::StaticClass(), nullptr, TEXT("/Game/UI/DinoCard/DA_DinoRegistry.DA_DinoRegistry"))))
	{
		for (UDinoInfoData* Species : Registry->Species)
		{
			if (Species != nullptr && Species->ExhibitKey.Equals(ExhibitKey, ESearchCase::IgnoreCase))
			{
				Info = Species;
				break;
			}
		}
	}
	if (Info == nullptr || Info->HeroImage == nullptr)
	{
		UE_LOG(LogDocentChat, Verbose, TEXT("전시물 카드 생략: %s 에 맞는 종 데이터가 없습니다."), *ExhibitKey);
		return;
	}
	ExhibitCardShownFor = ExhibitKey;

	const FLinearColor Glass    = FLinearColor::FromSRGBColor(FColor(22, 27, 36, 225));
	const FLinearColor Outline  = FLinearColor(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor TextMain = FLinearColor::FromSRGBColor(FColor(240, 244, 250));
	const FLinearColor TextDim  = FLinearColor::FromSRGBColor(FColor(160, 170, 185));
	const float CardWidth = ChatInsertWidth;
	const float Radius = 28.f;

	UVerticalBox* Card = WidgetTree->ConstructWidget<UVerticalBox>();

	// 대표 사진. 위 두 모서리만 둥글게 - 카드 테두리와 맞물린다.
	{
		UImage* Hero = WidgetTree->ConstructWidget<UImage>();
		FSlateBrush Brush;
		Brush.SetResourceObject(Info->HeroImage);
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, 0.f, 0.f);
		Brush.TintColor = FSlateColor(FLinearColor::White);
		const float Aspect = FMath::Clamp(
			static_cast<float>(Info->HeroImage->GetSizeY()) / FMath::Max(1.f, static_cast<float>(Info->HeroImage->GetSizeX())),
			0.5f, 0.72f);
		Brush.ImageSize = FVector2D(CardWidth, FMath::RoundToFloat(CardWidth * Aspect));
		Hero->SetBrush(Brush);
		Hero->SetVisibility(ESlateVisibility::HitTestInvisible);
		Card->AddChild(Hero);
	}

	// 이름 줄: 한글 이름 / 학명, 오른쪽에 펼치기 아이콘.
	{
		UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		UVerticalBox* Names = WidgetTree->ConstructWidget<UVerticalBox>();

		UTextBlock* Ko = WidgetTree->ConstructWidget<UTextBlock>();
		Ko->SetText(Info->NameKo);
		FSlateFontInfo KoFont = Ko->GetFont();
		KoFont.Size = 30;
		Ko->SetFont(KoFont);
		Ko->SetColorAndOpacity(FSlateColor(TextMain));
		Names->AddChild(Ko);

		UTextBlock* Sci = WidgetTree->ConstructWidget<UTextBlock>();
		Sci->SetText(Info->NameSci);
		FSlateFontInfo SciFont = Sci->GetFont();
		SciFont.Size = 24;
		Sci->SetFont(SciFont);
		Sci->SetColorAndOpacity(FSlateColor(TextDim));
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Names->AddChild(Sci)))
		{
			S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(NameRow->AddChild(Names)))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetVerticalAlignment(VAlign_Center);
		}

		if (UTexture2D* Expand = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/DinoCard/Icons/fullscreen.fullscreen")))
		{
			UImage* Icon = WidgetTree->ConstructWidget<UImage>();
			Icon->SetBrushFromTexture(Expand);
			Icon->SetDesiredSizeOverride(FVector2D(40.f, 40.f));
			Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.75f));
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(NameRow->AddChild(Icon)))
			{
				S->SetVerticalAlignment(VAlign_Center);
			}
		}

		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Card->AddChild(NameRow)))
		{
			S->SetPadding(FMargin(26.f, 20.f, 26.f, 22.f));
		}
	}

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetBrush(FSlateRoundedBoxBrush(Glass, Radius, Outline, 2.f));
	Frame->SetPadding(FMargin(0.f));
	Frame->SetContent(Card);

	// 카드 + 태그 줄을 한 항목으로 묶어 스크롤에 넣는다.
	UVerticalBox* Item = WidgetTree->ConstructWidget<UVerticalBox>();
	Item->AddChild(Frame);

	UHorizontalBox* Tags = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (const FText& Tag : {Info->DietTag, Info->PeriodTag})
	{
		if (Tag.IsEmpty())
		{
			continue;
		}
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Tags->AddChild(MakeTagPill(WidgetTree, Tag))))
		{
			S->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
		}
	}
	if (Tags->GetChildrenCount() > 0)
	{
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Item->AddChild(Tags)))
		{
			S->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
		}
	}

	ChatScroll->AddChild(Item);
	if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(Item->Slot))
	{
		S->SetPadding(ChatInsertMargin);
	}
}

void UDocentChatWidget::AddTopicMenu()
{
	if (WidgetTree == nullptr || ChatScroll == nullptr || ChipClass == nullptr)
	{
		return;
	}

	// 메뉴는 대화의 일부라, 시작 화면 위에 띄우지 않고 시작 화면을 걷은 뒤 붙인다.
	if (!bHasAskedOnce)
	{
		bHasAskedOnce = true;
		if (EmptyStateBox != nullptr)
		{
			EmptyStateBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		ApplyQuickQuestionVisibility();
	}

	AddBubble(/*bIsUser=*/false, TEXT("어떤 부분이 더 궁금하신가요?\n아래 주제 중에서 선택하거나,\n직접 질문해도 좋아요!"));

	// 전시물이 정해졌으면 "이 공룡", 아니면 공룡 일반으로 묻는다.
	const TCHAR* Subject = HasExhibitContext() ? TEXT("이 공룡") : TEXT("공룡");
	struct FTopic { const TCHAR* Title; const TCHAR* Subtitle; FName Icon; FString Question; };
	const FTopic Topics[] = {
		{ TEXT("기본 정보"),     TEXT("시대, 크기, 특징"),           TEXT("menu_book"),      FString::Printf(TEXT("%s의 시대, 크기, 특징을 알려줘"), Subject) },
		{ TEXT("식성"),          TEXT("무엇을 먹었을까?"),            TEXT("eco"),            FString::Printf(TEXT("%s은 무엇을 먹었을까?"), Subject) },
		{ TEXT("서식지"),        TEXT("어디에 살았을까?"),            TEXT("public"),         FString::Printf(TEXT("%s은 어디에 살았을까?"), Subject) },
		{ TEXT("발견과 연구"),   TEXT("언제, 어떻게 발견되었을까?"),  TEXT("history"),        FString::Printf(TEXT("%s은 언제, 어떻게 발견되었을까?"), Subject) },
		{ TEXT("재미있는 사실"), TEXT("더 놀라운 이야기"),            TEXT("travel_explore"), FString::Printf(TEXT("%s에 대한 재미있는 사실을 알려줘"), Subject) },
	};
	for (const FTopic& T : Topics)
	{
		UDocentQuickChip* Row = CreateWidget<UDocentQuickChip>(this, ChipClass);
		if (Row == nullptr)
		{
			continue;
		}
		Row->SetTopic(T.Question, T.Title, T.Subtitle, T.Icon, ChatInsertWidth);
		Row->OnClicked.BindUObject(this, &UDocentChatWidget::HandleQuickChipClicked);
		ChatScroll->AddChild(Row);
		if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(Row->Slot))
		{
			S->SetPadding(FMargin(ChatInsertMargin.Left, 6.f, ChatInsertMargin.Right, 6.f));
		}
	}

	AddBubble(/*bIsUser=*/false, TEXT("궁금한 것을 선택해보세요!\n언제든지 다른 질문도 할 수 있어요."));
	ScrollToLatest();
}

#if !UE_BUILD_SHIPPING
void UDocentChatWidget::AddPreviewBubble(bool bInIsUser, const FString& InText)
{
	AddBubble(bInIsUser, InText);
	if (!bHasAskedOnce)
	{
		bHasAskedOnce = true;
		if (EmptyStateBox != nullptr)
		{
			EmptyStateBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		ApplyQuickQuestionVisibility();
	}
}
#endif
