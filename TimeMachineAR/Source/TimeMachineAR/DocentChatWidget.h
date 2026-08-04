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
	 * 창이 열릴 때 세션을 열 전시물. 비워 두면 OpenChat 을 직접 불러야 한다.
	 * POI 마커가 붙기 전까지는 여기에 시드 전시물 UUID 를 넣어 시험한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	FString ExhibitId;

	/**
	 * 처음부터 채팅창을 펼쳐 둘지.
	 *
	 * AR 앱이므로 false 가 자연스럽다 — 전시물을 먼저 보고 궁금할 때 연다.
	 * 다만 OpenButton 이 없으면 다시 열 방법이 없으므로, 그 경우에는 이 값과
	 * 무관하게 열린 채로 시작한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Docent|Chat")
	bool bStartOpen = true;

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
	// 도슨트 델리게이트는 전부 다이나믹이라 핸들러가 UFUNCTION 이어야 한다.
	UFUNCTION() void HandleSessionReady(const FString& SessionId);
	UFUNCTION() void HandleDelta(const FString& Text);
	UFUNCTION() void HandleCompleted(const FDocentChatResult& Result);
	UFUNCTION() void HandleFailed(const FString& Reason, bool bPartial);
	UFUNCTION() void HandleSendClicked();
	UFUNCTION() void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleOpenClicked();

	/** 열림 상태를 위젯에 반영한다. ShowChat/HideChat 의 공통부. */
	void ApplyOpenState(bool bOpen);

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

	bool bIsOpen = true;
};
