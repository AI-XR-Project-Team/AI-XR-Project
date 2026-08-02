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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDocentAnswered, const FDocentAnswer&, Answer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDocentFailed, const FString&, Reason);

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

	UPROPERTY(BlueprintAssignable, Category = "Docent")
	FOnDocentAnswered OnAnswered;

	UPROPERTY(BlueprintAssignable, Category = "Docent")
	FOnDocentFailed OnFailed;

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
};
