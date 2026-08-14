#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "NavTypes.h"
#include "NavClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavMarkersReceived, const TArray<FNavMarker>&, Markers);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavDestinationsReceived, const TArray<FNavDestination>&, Destinations);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavRouteReceived, const FNavRoute&, Route);
/** 네트워크 실패(-1) / 비-200(HTTP 코드) / JSON 파싱 실패(-2)를 모두 여기로 수렴한다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNavRequestFailed, int32, StatusCode, const FString&, Reason);

/**
 * 네비게이션 서버(FastAPI)와 통신하는 게임 인스턴스 서브시스템.
 *
 * 검증된 UDocentClient(GameInstanceSubsystem + HTTP + Json)를 복제한 것이다.
 * 레벨이 바뀌어도 살아남아야 하므로 Actor 가 아닌 GameInstanceSubsystem 이다.
 * 서버는 측위 비종속 — 맵 좌표만 입출력한다(측위·좌표변환은 앱의 다음 단계 몫).
 *
 * 서버 주소는 DefaultGame.ini 의 [/Script/TimeMachineAR.NavClient] ServerBaseUrl 로 바꾼다.
 * 비워 두면 DocentClient 의 주소를 폴백으로 쓴다(IP 를 한 군데만 고쳐도 동작).
 *
 * 계약 원문: docs/nav-server-integration-guide.md. 서버 스키마가 바뀌면 NavTypes.h 와
 * 이 클래스의 파서를 같이 고친다.
 */
UCLASS(Config = Game)
class TIMEMACHINEAR_API UNavClient : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ------------------------------------------------------------------ 조회

	/** GET /maps/{id}/markers. 성공 시 OnMarkersReceived, 실패 시 OnRequestFailed. */
	UFUNCTION(BlueprintCallable, Category = "Nav")
	void GetMarkers(const FString& MapId);

	/** GET /maps/{id}/destinations. 성공 시 OnDestinationsReceived, 실패 시 OnRequestFailed. */
	UFUNCTION(BlueprintCallable, Category = "Nav")
	void GetDestinations(const FString& MapId);

	// ------------------------------------------------------------------ 경로

	/** POST /navigation/route. 성공 시 OnRouteReceived, 실패 시 OnRequestFailed. */
	UFUNCTION(BlueprintCallable, Category = "Nav")
	void RequestRoute(const FString& MapId, const FNavMapPose& From,
		const FString& ToNodeId, bool bAccessibleOnly = false);

	/** POST /navigation/reroute. 이탈 감지 시 새 from 으로 호출. route 와 스키마 동일. */
	UFUNCTION(BlueprintCallable, Category = "Nav")
	void Reroute(const FString& MapId, const FNavMapPose& From,
		const FString& ToNodeId, bool bAccessibleOnly = false);

	// ------------------------------------------------------------------ 델리게이트

	UPROPERTY(BlueprintAssignable, Category = "Nav")
	FOnNavMarkersReceived OnMarkersReceived;

	UPROPERTY(BlueprintAssignable, Category = "Nav")
	FOnNavDestinationsReceived OnDestinationsReceived;

	UPROPERTY(BlueprintAssignable, Category = "Nav")
	FOnNavRouteReceived OnRouteReceived;

	UPROPERTY(BlueprintAssignable, Category = "Nav")
	FOnNavRequestFailed OnRequestFailed;

	/** 현재 적용 중인 서버 주소. 설정이 제대로 로드됐는지 확인용. */
	UFUNCTION(BlueprintPure, Category = "Nav")
	FString GetServerBaseUrl() const { return ServerBaseUrl; }

	/**
	 * 부팅 스모크 테스트: maps → markers → destinations → route 를 순서대로 호출하고
	 * 전 과정을 LogNav 로 남긴다. 실기기에는 화면 UI 가 없으므로(이 단계 범위 밖),
	 * `adb logcat -s LogNav:V` 만 보면 배관이 살아 있는지 확인할 수 있다.
	 *
	 * 각 항목은 실패해도 다음으로 계속 진행하고, 마지막에 "n/4 성공" 으로 요약한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Nav")
	void RunSmokeTest();

protected:
	/**
	 * 네비게이션 서버 주소(끝에 / 없이). 에디터 PIE 는 127.0.0.1, 실기기는 PC 의 LAN IP.
	 * 서버는 uvicorn --host 0.0.0.0 으로 띄워야 폰에서 붙는다.
	 * 이름·패턴은 DocentClient.ServerBaseUrl 과 맞춘다.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nav")
	FString ServerBaseUrl = TEXT("http://127.0.0.1:8000");

	/** 스모크 테스트가 쓰는 기본 맵 UUID. 비어 있으면 스모크 테스트를 건너뛴다. */
	UPROPERTY(Config, EditAnywhere, Category = "Nav")
	FString DefaultMapId;

	/** HTTP 응답 대기 상한(초). 네비는 조회·계산뿐이라 도슨트(15초)보다 짧게. */
	UPROPERTY(Config, EditAnywhere, Category = "Nav")
	float RequestTimeoutSec = 5.0f;

	/** true 면 Initialize 에서 자동으로 스모크 테스트를 돈다. 출시 시엔 ini 에서 끈다. */
	UPROPERTY(Config, EditAnywhere, Category = "Nav")
	bool bRunSmokeTestOnStart = true;

private:
	/** 한 번의 스모크 테스트 진행 상태. 비동기 4단계가 이 공유 상태를 이어받는다. */
	struct FSmokeRun
	{
		int32 Success = 0;
		FString MapId;
		FNavMarker FirstMarker;
		bool bHaveMarker = false;
		FString FirstDestId;
		FString FirstDestLabel;
	};

	/**
	 * ServerBaseUrl 의 공백·끝 슬래시를 정리하고 스킴을 검증한다. 비어 있으면
	 * DocentClient 의 주소를 폴백으로 읽어 온다. 둘 다 값이 있는데 다르면 경고한다
	 * (도슨트는 되는데 네비만 안 되는 디버깅 지옥 방지 — docs/ue-nav-ui-plan.md §0.5).
	 */
	void NormalizeServerBaseUrl();

	/** ServerBaseUrl 에 경로를 붙여 완전한 URL 을 만든다. */
	FString BuildUrl(const FString& Path) const;

	/** MapId 가 비면 DefaultMapId 로 대체한다. */
	FString ResolveMapId(const FString& MapId) const;

	/** GET 요청을 만들어 보낸다. 완료 콜백은 호출부가 바인드한다. */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeGet(const FString& Url) const;
	/** POST(JSON) 요청을 만들어 보낸다. */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakePost(const FString& Url, const FString& Body) const;

	/** route/reroute 공용 구현. URL 만 분기한다. */
	void SendRoute(const FString& Url, const FString& MapId, const FNavMapPose& From,
		const FString& ToNodeId, bool bAccessibleOnly);
	/** 경로 요청 바디를 만든다. from 의 heading_deg 는 bHasHeading 일 때만 넣는다. */
	FString BuildRouteBody(const FString& MapId, const FNavMapPose& From,
		const FString& ToNodeId, bool bAccessibleOnly) const;

	// 파서 — 스모크 테스트와 공개 API 가 공유한다.
	static bool ParseMarkerObject(const TSharedPtr<FJsonObject>& Obj, FNavMarker& Out);
	static bool ParseMarkersArray(const FString& Content, TArray<FNavMarker>& Out);
	static bool ParseDestinationsArray(const FString& Content, TArray<FNavDestination>& Out);
	static bool ParseRoute(const FString& Content, FNavRoute& Out);

	// 공개 API 완료 콜백
	void OnMarkersComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnDestinationsComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
	void OnRouteComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** HTTP 실패/비-200/파싱 실패를 로그 + OnRequestFailed 로 동시에 알린다. */
	void ReportFailure(int32 StatusCode, const FString& Reason);

	// 스모크 테스트 4단계(순차 체인). 각 단계는 실패해도 다음을 계속 호출한다.
	void SmokeStepMap(const TSharedRef<FSmokeRun>& Run);
	void SmokeStepMarkers(const TSharedRef<FSmokeRun>& Run);
	void SmokeStepDestinations(const TSharedRef<FSmokeRun>& Run);
	void SmokeStepRoute(const TSharedRef<FSmokeRun>& Run);
	void SmokeFinish(const TSharedRef<FSmokeRun>& Run);

#if !UE_BUILD_SHIPPING
	/**
	 * 실기기 진단용 콘솔 명령을 등록한다 (Nav.Smoke / Nav.DumpMarkers / Nav.DumpDestinations / Nav.DumpRoute).
	 *
	 * 서브시스템의 UFUNCTION(Exec) 는 콘솔에서 잡히지 않는다(UGameInstance 가
	 * ProcessConsoleExec 을 서브시스템까지 내려보내지 않는다 — DocentClient 선례).
	 * 그래서 Exec 대신 콘솔 명령으로 등록한다.
	 */
	void RegisterDebugConsoleCommands();

	/** 등록한 콘솔 명령. Deinitialize 에서 해제하지 않으면 다음 실행 때 중복 등록된다. */
	TArray<IConsoleObject*> DebugConsoleCommands;
#endif
};
