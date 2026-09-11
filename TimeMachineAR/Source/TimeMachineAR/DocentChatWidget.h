#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DocentClient.h"
#include "DocentChatWidget.generated.h"

class UButton;
class UDocentChatBubble;
class UDocentQuickChip;
class UEditableTextBox;
class UPanelWidget;
class UScrollBox;
class USpacer;
class UTextBlock;

/**
 * 도슨트 챗봇 창.
 *
 * 대화 로직 전부가 여기 있고, WBP 는 배치와 스타일만 맡는다. 델리게이트
 * 바인딩·말풍선 생성·스트리밍 이어 붙이기·실패 처리를 블루프린트 그래프로
 * 옮기면 노드가 수십 개로 늘고 컴파일 검증도 못 받는다.
 *
 * WBP 로 상속할 때 필요한 자식 위젯 (이름이 다르면 WBP 컴파일이 에러로 알려준다):
 *   - ChatScroll  (Scroll Box)        : 필수. 말풍선이 여기 직접 붙는다
 *   - InputBox    (Editable Text Box) : 필수
 *   - SendButton  (Button)            : 필수
 *   - QuickQuestionBox (Wrap/Vertical Box) : 선택. 빠른 질문 칩이 붙는 자리
 *   - DocentNameText   (Text Block)        : 선택. 헤더의 도슨트 이름
 *   - ChatPanel        (아무 위젯)          : 선택. 열고 닫을 대상
 *   - CloseButton      (Button)            : 선택. 채팅창 닫기
 *   - OpenButton       (Button)            : 선택. 닫힌 상태에서 다시 열기
 *   - EmptyStateBox    (아무 위젯)          : 선택. 첫 질문 전 아바타+인사말
 *   - GreetingLabel    (Text Block)        : 선택. EmptyStateBox 안의 인사말
 *   - ChatBackdrop     (아무 위젯)          : 선택. 안전영역 밖의 배경
 *   - KeyboardSpacer   (Spacer)            : 선택. ChatPanel 의 마지막 자식
 *
 * 그리고 클래스 기본값에서 BubbleClass / ChipClass 를 지정해야 한다.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UDocentChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 질문을 보낸다. 빠른 질문 칩과 전송 버튼이 모두 이 경로를 탄다.
	 *
	 * 세션이 없거나 이미 응답을 받는 중이면 무시한다. 중복 전송을 막지 않으면
	 * 두 응답의 조각이 한 말풍선에 섞인다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void SendQuestion(const FString& Question);

	/**
	 * 대화 대상 전시물을 바꾸고 세션을 새로 연다.
	 *
	 * 다른 전시물 앞으로 이동했을 때 부른다. 화면의 기존 대화는 지우지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	// 파라미터 이름(InExhibitId)은 BP 노드 핀 이름이라 바꾸면 AR_MainMap 레벨
	// BP 의 OpenChat 호출 노드가 깨진다. 이름은 유지하고, 값은 안정 키가 흐른다.
	void OpenChat(const FString& InExhibitId);

	/** 이번 질문에 딸려 보낼 부위. 빈 문자열이면 전시물 전체를 대상으로 답한다. */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void SetFocusedPoi(const FString& InPoiId) { FocusedPoiId = InPoiId; }

	/** 대화 내용을 화면에서 지운다. 세션은 그대로 두므로 맥락은 유지된다. */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void ClearMessages();

	// ------------------------------------------------------------ 열고 닫기

	/**
	 * 채팅창을 연다.
	 *
	 * AR 앱이라 채팅창이 떠 있는 동안 카메라 영상이 가려진다. 관람객이
	 * 전시물을 보려면 닫을 수 있어야 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void ShowChat();

	/** 채팅창을 닫는다. 세션과 대화 내용은 유지되므로 다시 열면 이어진다. */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void HideChat();

	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void ToggleChat();

	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	bool IsChatOpen() const { return bIsOpen; }

	/**
	 * 마커로 고른 공룡이 있는지.
	 *
	 * 없으면 전시물을 상정하지 않는 일반 AI 챗이고, 빠른 질문 칩도 띄우지
	 * 않는다. 칩 문구가 "이 공룡"을 가리키는데 가리킬 대상이 없기 때문이다.
	 */
	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	bool HasExhibitContext() const { return bExhibitContextSet && !ExhibitKey.IsEmpty(); }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ------------------------------------------------------------ 바인딩 위젯

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Docent|Chat")
	TObjectPtr<UScrollBox> ChatScroll;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Docent|Chat")
	TObjectPtr<UEditableTextBox> InputBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Docent|Chat")
	TObjectPtr<UButton> SendButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UPanelWidget> QuickQuestionBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UTextBlock> DocentNameText;

	/**
	 * 첫 질문 전까지만 보이는 블록. 큰 아바타와 인사말이 들어간다.
	 *
	 * ChatScroll 과 같은 자리에 겹쳐 두고(오버레이) 첫 질문에서 접는다.
	 * 스크롤 안에 넣으면 ClearMessages 의 ClearChildren 에 같이 지워지고,
	 * 대화가 시작된 뒤에도 첫 말풍선 위에 그대로 남는다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UWidget> EmptyStateBox;

	/** EmptyStateBox 안의 인사말 텍스트. GreetingText 가 여기 들어간다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UTextBlock> GreetingLabel;

	/**
	 * 열고 닫을 대상. 보통 헤더·대화·입력을 감싼 패널이다.
	 *
	 * 이 위젯 자체가 아니라 별도 패널을 숨기는 이유는, 루트를 숨기면 채팅창을
	 * 다시 열 버튼까지 같이 사라지기 때문이다. OpenButton 은 이 패널 밖에 둔다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UWidget> ChatPanel;

	/** 채팅창을 닫는 버튼. 보통 헤더의 X. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UButton> CloseButton;

	/** 닫힌 상태에서 다시 여는 버튼. ChatPanel 밖에 있어야 한다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UButton> OpenButton;

	/** 우측 상단 X 버튼 (AR 렌더링 종료) */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|AR")
	TObjectPtr<UButton> Btn_CloseAR;

	/**
	 * AR 화면 하단 바. 채팅이 열리면 접히고, 닫히면 다시 나온다.
	 *
	 * ChatPanel 밖에 있어야 한다. 안에 두면 채팅을 닫을 때 같이 사라져서
	 * 다시 열 방법이 없어진다. OpenButton 을 이 바 안에 넣어 두면 된다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UWidget> BottomBar;

	/**
	 * 채팅창 뒤를 덮는 배경. ChatPanel 과 함께 보였다 사라진다.
	 *
	 * 배경만 ChatPanel 밖에 두는 이유는 노치 아래까지 꽉 채워야 하기 때문이다.
	 * 안전영역 안에 넣으면 화면 맨 위에 배경 없는 띠가 남는다. 대신 밖에 있는
	 * 만큼 열림 상태를 여기서 따로 챙겨 주지 않으면, 채팅을 닫아도 배경만 남아
	 * AR 카메라를 통째로 가린다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<UWidget> ChatBackdrop;

	/**
	 * 가상 키보드가 차지하는 높이를 대신 밀어 주는 빈 칸.
	 *
	 * ChatPanel 의 마지막 자식이어야 한다. 키보드가 올라오면 이 칸의 높이를
	 * 키보드만큼 늘려서 세로 박스가 다시 흐르게 하고, 그 결과 입력창이 키보드
	 * 위로 올라온다. 없으면 아무 일도 하지 않는다.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Docent|Chat")
	TObjectPtr<USpacer> KeyboardSpacer;

	// ------------------------------------------------------------ 클래스 기본값

	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	TSubclassOf<UDocentChatBubble> BubbleClass;

	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	TSubclassOf<UDocentQuickChip> ChipClass;

	/** 헤더에 표시할 도슨트 이름. 서버 페르소나와 맞춰 둔다. */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	FText DocentName = FText::FromString(TEXT("AI 도슨트 '렉시'"));

	/**
	 * 창을 열면 먼저 뜨는 인사말. 클라이언트가 직접 만들며 LLM 을 부르지 않는다.
	 * 빈 값이면 인사말을 띄우지 않는다.
	 *
	 * 표시 위치는 WBP 구성에 따라 갈린다. EmptyStateBox 가 있으면 그 안의
	 * GreetingLabel 에 들어가고, 없으면 예전처럼 첫 말풍선으로 붙는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat", meta = (MultiLine = true))
	FText GreetingText = FText::FromString(
		TEXT("안녕하세요! 저는 AI 도슨트 '렉시'예요.\n공룡에 대해 궁금한 것을 물어보세요!"));

	/** 빠른 질문 칩. 모바일에서 한글 입력이 번거로워 탭 한 번으로 시작할 수 있게 한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	TArray<FString> QuickQuestions = {
		TEXT("이 공룡에 대해 설명해줘"),
		TEXT("식성은 무엇이었을까?"),
		TEXT("크기는 얼마나 컸을까?"),
		TEXT("더 놀라운 사실이 있을까?")
	};

	/** 첫 질문을 보낸 뒤 칩을 감출지. 목업처럼 시작 화면에서만 보이게 한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	bool bHideChipsAfterFirstQuestion = true;

	/**
	 * 대화 대상 전시물. 런타임에는 OpenChat 이 채운다.
	 *
	 * 클래스 기본값은 마커가 없던 시절의 시험용이라 NativeConstruct 가 무시한다.
	 * 마커로 공룡을 고르지 않고 도슨트에 들어오면 특정 공룡을 상정하지 않는
	 * 일반 AI 챗이어야 하는데, 기본값을 그대로 쓰면 늘 그 전시물(티라노) 기준으로
	 * 답한다. 전시물 대화는 OpenChat 으로만 시작한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	FString ExhibitKey;

	/**
	 * 처음부터 채팅창을 펼쳐 둘지.
	 *
	 * AR 앱이므로 false 가 자연스럽다 — 전시물을 먼저 보고 궁금할 때 연다.
	 * 다만 OpenButton 이 없으면 다시 열 방법이 없으므로, 그 경우에는 이 값과
	 * 무관하게 열린 채로 시작한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	bool bStartOpen = true;

	/**
	 * 가상 키보드가 가릴 화면 비율. 플랫폼이 알려 주는 값을 못 믿을 때 쓴다.
	 *
	 * 안드로이드는 전체화면 창에서 adjustResize 를 무시하고, UE 는 몰입 모드가
	 * 기본이다. 그래서 GameActivity 가 getWindowVisibleDisplayFrame 으로 재는
	 * 키보드 높이가 0 이나 화면 전체로 잡힌다. 그 값이 터무니없으면 이 비율로
	 * 대신 민다. 한국어 키보드가 대략 화면의 35~40% 를 차지한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat", meta = (ClampMin = "0.0", ClampMax = "0.8"))
	float KeyboardHeightRatio = 0.38f;

	// ------------------------------------------------------------ 확장 지점

	/** 세션이 열려 입력이 가능해진 시점. 로딩 표시를 걷어내는 용도. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Docent|Chat")
	void OnChatReady();

	/** 응답 수신 시작/종료. "생각 중" 표시를 켜고 끄는 용도. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Docent|Chat")
	void OnStreamingChanged(bool bStreaming);

	/** 열림 상태가 바뀐 직후. 슬라이드 인/아웃 애니메이션을 붙이는 자리. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Docent|Chat")
	void OnChatOpenChanged(bool bOpen);

private:
	UFUNCTION() void HandleReferenceCapture();
	UFUNCTION() void HandleReferenceRescan();
	UFUNCTION() void HandleReferenceExit();
	void RefreshReferenceUI();
	bool bReferenceRecognized = false;
	// 도슨트 델리게이트는 전부 다이나믹이라 핸들러가 UFUNCTION 이어야 한다.
	UFUNCTION() void HandleSessionReady(const FString& SessionId);
	UFUNCTION() void HandleDelta(const FString& Text);
	UFUNCTION() void HandleCompleted(const FDocentChatResult& Result);
	UFUNCTION() void HandleFailed(const FString& Reason, bool bPartial);
	UFUNCTION() void HandleSendClicked();
	UFUNCTION() void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleOpenClicked();
	UFUNCTION() void HandleCloseARClicked();
	UFUNCTION() void HandleMarkerFound(UARPin* Pin, const FTransform& MarkerPose, const FString& MarkerCode);
	UFUNCTION() void HandleScanStateChanged(bool bIsScanning);

	/** 열림 상태를 위젯에 반영한다. ShowChat/HideChat 의 공통부. */
	void ApplyOpenState(bool bOpen);

	/** 빠른 질문 칩 자리를 보일지 접을지 정한다. 전시물이 없으면 늘 접는다. */
	void ApplyQuickQuestionVisibility();

	/**
	 * 가상 키보드가 가리는 높이를 레이아웃에 반영한다.
	 *
	 * @param KeyboardPixels 키보드 높이(실제 픽셀). 숨겨졌으면 0.
	 */
	void ApplyKeyboardInset(float KeyboardPixels);

	/**
	 * 입력창 포커스를 주기적으로 확인해 키보드 여백을 켜고 끈다.
	 *
	 * 포커스 변화를 알려 주는 UMG 이벤트가 없고, NativeTick 은 위젯 틱 빈도가
	 * Auto 라 블루프린트 Tick 이 없으면 아예 불리지 않는다. 그래서 타이머로 본다.
	 */
	void PollInputFocus();

	/** 이번에 밀어야 할 키보드 높이(픽셀). 보고값이 미덥지 않으면 비율로 계산한다. */
	float ResolveKeyboardPixels() const;

	// 플랫폼 애플리케이션은 위젯보다 오래 산다. 소멸 시 반드시 해제한다.
	FDelegateHandle KeyboardShownHandle;
	FDelegateHandle KeyboardHiddenHandle;

	FTimerHandle InputFocusTimer;

	/** 플랫폼이 마지막으로 알려 준 키보드 높이(픽셀). 0 이면 아직 못 믿는다. */
	float ReportedKeyboardPixels = 0.0f;

	/** 지금 레이아웃에 들어가 있는 여백(픽셀). 같은 값을 매 폴마다 다시 넣지 않는다. */
	float AppliedKeyboardPixels = 0.0f;

	bool bInputFocused = false;

	/** 빠른 질문 칩의 네이티브 델리게이트 수신부. */
	void HandleQuickChipClicked(const FString& Question);

	UDocentChatBubble* AddBubble(bool bIsUser, const FString& Text);
	void BuildQuickQuestions();
	void SetInputEnabled(bool bEnabled);
	void ScrollToLatest();

	/** 게임 인스턴스 서브시스템. 위젯보다 오래 살므로 소멸 시 반드시 언바인드한다. */
	UPROPERTY()
	TObjectPtr<UDocentClient> Client;

	/** 지금 조각을 받아 채우는 중인 말풍선. 없으면 수신 중이 아니다. */
	UPROPERTY()
	TObjectPtr<UDocentChatBubble> StreamingBubble;

	/** 이번 턴에 함께 보낼 부위. */
	FString FocusedPoiId;

	bool bHasAskedOnce = false;

	/**
	 * OpenChat 으로 전시물을 지정받았는지. WBP 클래스 기본값과 구분하려고 둔다.
	 *
	 * ExhibitKey 가 비었는지만 봐서는 둘을 구분할 수 없다. 위젯을 만든 직후
	 * NativeConstruct 전에 OpenChat 이 불릴 수 있어서(공룡을 이미 알고 여는 경로)
	 * 이 값이 서 있으면 NativeConstruct 는 지정된 전시물을 지우지 않는다.
	 */
	bool bExhibitContextSet = false;

	bool bIsOpen = true;
};
