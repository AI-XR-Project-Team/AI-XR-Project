#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "DocentClient.generated.h"

/**
 * AI 도슨트 서버(FastAPI) 응답 1건.
 *
 * backend_server 의 DocentAskResponse 와 필드가 1:1 대응한다.
 * 서버 스키마를 바꾸면 이 구조체도 같이 고쳐야 한다.
 */
USTRUCT(BlueprintType)
struct FDocentAnswer
{
	GENERATED_BODY()

	/** 도슨트 응답 본문 */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	FString Answer;

	/** "llm" = 모델 생성 / "fallback" = 서버가 DB 의 사전 작성 해설을 반환 */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	FString Source;

	/** 서버 처리 시간(ms). 도슨트 응답 KPI 계측용. */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	int32 ElapsedMs = 0;
};

/**
 * 대화 한 턴이 끝났을 때의 결과.
 *
 * 서버 SSE 의 `done` 프레임과 대응한다.
 */
USTRUCT(BlueprintType)
struct FDocentChatResult
{
	GENERATED_BODY()

	/** 조각을 모두 이어 붙인 최종 응답. */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	FString FullText;

	/** "llm" = 모델 생성 / "fallback" = LLM 실패로 DB 해설 대체 */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	FString Source;

	/** 첫 글자가 도착하기까지 걸린 시간(ms). 스트리밍의 체감 지표다. */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	int32 TtftMs = 0;

	/** 전체 생성 시간(ms). */
	UPROPERTY(BlueprintReadOnly, Category = "Docent")
	int32 TotalMs = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDocentAnswered, const FDocentAnswer&, Answer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDocentFailed, const FString&, Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatSessionReady, const FString&, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatDelta, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatCompleted, const FDocentChatResult&, Result);
/** `bPartial=true` 면 앞서 받은 조각은 유효하다. 지우지 말고 남겨야 한다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChatFailed, const FString&, Reason, bool, bPartial);

/**
 * AI 도슨트 서버와 통신하는 게임 인스턴스 서브시스템.
 *
 * ADR-001 에 따라 C++ 코어서버를 거치지 않고 FastAPI 에 직접 HTTP 로 붙는다.
 * 레벨이 바뀌어도 살아남아야 하므로 Actor 가 아닌 GameInstanceSubsystem 이다.
 *
 * 사용법 (블루프린트):
 *   1. OnAnswered / OnFailed 에 바인드
 *   2. AskDocent(PoiId, Question) 호출
 *
 * 서버 주소는 DefaultGame.ini 의 [/Script/TimeMachineAR.DocentClient] ServerBaseUrl
 * 로 바꾼다. 개발 중 PC 의 LAN IP 가 바뀌어도 재빌드가 필요 없게 하기 위함이다.
 */
UCLASS(Config = Game)
class TIMEMACHINEAR_API UDocentClient : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * POI 에 대한 도슨트 응답을 요청한다 (POST /docent/ask).
	 *
	 * @param PoiId     GET /exhibits/{id} 응답에서 얻은 POI 의 UUID 문자열.
	 * @param Question  관람객 질문. 비우면 서버가 해당 부위의 기본 해설을 생성한다.
	 *
	 * 응답은 OnAnswered, 실패는 OnFailed 로 브로드캐스트된다.
	 * 서버가 LLM 실패 시에도 폴백 해설을 200 으로 돌려주므로, OnFailed 는
	 * 사실상 네트워크·파싱 오류일 때만 발생한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent")
	void AskDocent(const FString& PoiId, const FString& Question);

	/** 서버가 살아 있는지 확인한다 (GET /health). 실기기 연결 진단용. */
	UFUNCTION(BlueprintCallable, Category = "Docent")
	void CheckHealth();

	/**
	 * 서버 주소를 런타임에 바꾼다 (이번 실행에만 적용, 저장하지 않음).
	 *
	 * 실기기에서는 ini 를 고치려면 재패키징해야 하므로, 주소 후보를 빠르게
	 * 바꿔가며 시험할 때 쓴다. 영구 설정은 DefaultGame.ini 의 ServerBaseUrl 이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent")
	void SetServerBaseUrl(const FString& NewBaseUrl);

	/** 현재 적용 중인 서버 주소. 설정이 제대로 로드됐는지 화면에 찍어볼 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Docent")
	FString GetServerBaseUrl() const { return ServerBaseUrl; }

	UPROPERTY(BlueprintAssignable, Category = "Docent")
	FOnDocentAnswered OnAnswered;

	UPROPERTY(BlueprintAssignable, Category = "Docent")
	FOnDocentFailed OnFailed;

	// ---------------------------------------------------------------- 챗봇

	/**
	 * 대화 세션을 시작한다 (POST /docent/sessions).
	 *
	 * 채팅창을 열 때 한 번 호출한다. 완료되면 OnChatSessionReady 가 뜨고,
	 * 그 뒤부터 SendChatMessage 를 쓸 수 있다.
	 *
	 * @param ExhibitId 대화 대상 전시물의 UUID. 비우면 매 질문마다 PoiId 를
	 *                  줘야 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void StartChatSession(const FString& ExhibitId);

	/**
	 * 질문을 보내고 응답을 조각 단위로 받는다 (POST /docent/chat/stream).
	 *
	 * 응답은 OnChatDelta 로 여러 번 나뉘어 오고, 마지막에 OnChatCompleted 가
	 * 한 번 뜬다. 말풍선 하나를 만들어 두고 델타가 올 때마다 이어 붙이면 된다.
	 *
	 * @param Message 관람객 질문.
	 * @param PoiId   이번 질문에서 탭한 부위의 UUID. 비우면 전시물 전체를
	 *                대상으로 답한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Docent|Chat")
	void SendChatMessage(const FString& Message, const FString& PoiId);

	/** 응답을 받는 중인지. 중복 전송을 막고 입력창을 잠그는 데 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	bool IsChatStreaming() const { return bIsChatStreaming; }

	/** 현재 대화 세션 id. 아직 시작하지 않았으면 빈 문자열. */
	UFUNCTION(BlueprintPure, Category = "Docent|Chat")
	FString GetChatSessionId() const { return ChatSessionId; }

	UPROPERTY(BlueprintAssignable, Category = "Docent|Chat")
	FOnChatSessionReady OnChatSessionReady;

	UPROPERTY(BlueprintAssignable, Category = "Docent|Chat")
	FOnChatDelta OnChatDelta;

	UPROPERTY(BlueprintAssignable, Category = "Docent|Chat")
	FOnChatCompleted OnChatCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Docent|Chat")
	FOnChatFailed OnChatFailed;

	/** 이 기기의 익명 식별자. 서버의 device_uuid 로 전달된다. */
	UFUNCTION(BlueprintPure, Category = "Docent")
	FString GetDeviceUuid() const { return DeviceUuid; }

protected:
	/**
	 * FastAPI 서버 주소 (끝에 / 없이).
	 *
	 * 에디터 PIE 는 localhost 로 되지만, 실기기는 PC 의 LAN IP 여야 한다.
	 * 서버도 uvicorn --host 0.0.0.0 으로 띄워야 외부에서 붙을 수 있다.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Docent")
	FString ServerBaseUrl = TEXT("http://127.0.0.1:8000");

	/** HTTP 응답 대기 상한(초). 서버 LLM 타임아웃(10초)보다 넉넉해야 한다. */
	UPROPERTY(Config, EditAnywhere, Category = "Docent")
	float RequestTimeoutSec = 15.0f;

private:
	/**
	 * 기기별로 고정된 익명 ID.
	 *
	 * Config 프로퍼티로 두면 SaveConfig() 가 커밋 대상인 DefaultGame.ini 에
	 * 기기별 값을 써버리므로, 기기 로컬 파일(GameUserSettings.ini)에 직접 저장한다.
	 */
	FString DeviceUuid;

	/**
	 * ServerBaseUrl 의 공백·끝 슬래시를 정리하고 유효성을 검사한다.
	 *
	 * 설정이 비어 있거나 스킴이 없으면 컴파일 기본값으로 되돌린다. 이 방어가 없으면
	 * 빈 값 + "/health" 가 그대로 요청돼 libcurl 이 "health" 를 호스트명으로 해석하고
	 * "Couldn't resolve host name" 이라는, 원인과 무관해 보이는 에러가 난다.
	 */
	void NormalizeServerBaseUrl();

	/** ServerBaseUrl 에 경로를 붙여 완전한 URL 을 만든다. */
	FString BuildUrl(const FString& Path) const;

	void OnAskComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnHealthComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** 실패를 로그 + 델리게이트로 동시에 알린다. 실기기 디버깅용. */
	void ReportFailure(const FString& Reason);

	// ---------------------------------------------------------------- 챗봇

	void OnSessionCreated(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnChatStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** SSE 이벤트 하나를 처리한다. 반드시 게임 스레드에서 호출된다. */
	void HandleSseEvent(const FString& EventName, const FString& Data);

	/** 대화 실패를 로그 + OnChatFailed 로 알리고 스트리밍 상태를 정리한다. */
	void ReportChatFailure(const FString& Reason, bool bPartial);

	/** 발급받은 대화 세션. 비어 있으면 아직 시작 전이다. */
	FString ChatSessionId;

	/** 응답 수신 중 여부. 중복 전송 방지용. */
	bool bIsChatStreaming = false;

	/** 이번 턴에 지금까지 받은 조각을 이어 붙인 것. 게임 스레드에서만 만진다. */
	FString ChatAccumulated;

	/** done/error 프레임을 받았는지. 완료 콜백에서 조용한 끊김을 판별한다. */
	bool bChatTerminated = false;

#if !UE_BUILD_SHIPPING
	/**
	 * 실기기 진단용 콘솔 명령을 등록한다 (Docent.Health / Docent.StartSession / Docent.Ask).
	 *
	 * 서브시스템의 Exec 함수는 콘솔에서 잡히지 않는다. UGameInstance 가
	 * ProcessConsoleExec 을 오버라이드하지 않아 서브시스템까지 내려가지 않기 때문이다.
	 * 그래서 Exec 대신 콘솔 명령으로 등록한다.
	 *
	 * 패키징된 APK 에서는 UI 없이 이렇게 호출할 수 있다:
	 *   adb shell "am broadcast -a android.intent.action.RUN -e cmd 'Docent.Health'"
	 * (GameActivity 는 Shipping 이 아닐 때만 이 리시버를 등록한다.)
	 */
	void RegisterDebugConsoleCommands();

	/** 등록한 콘솔 명령. Deinitialize 에서 해제하지 않으면 다음 실행 때 중복 등록된다. */
	TArray<IConsoleObject*> DebugConsoleCommands;
#endif
};
