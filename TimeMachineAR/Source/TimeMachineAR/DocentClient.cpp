#include "DocentClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogDocent, Log, All);

void UDocentClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 기기 익명 ID 는 최초 실행 때 한 번만 만들고 이후 재사용한다.
	// 서버는 로그인 없이 이 값으로 관람객을 구분한다(users.device_uuid).
	// GGameUserSettingsIni 는 기기 로컬(Saved/Config)이라 저장소에 올라가지 않는다.
	static const TCHAR* DeviceSection = TEXT("Docent");
	static const TCHAR* DeviceKey = TEXT("DeviceUuid");

	if (!GConfig->GetString(DeviceSection, DeviceKey, DeviceUuid, GGameUserSettingsIni) || DeviceUuid.IsEmpty())
	{
		DeviceUuid = FString::Printf(TEXT("android-%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
		GConfig->SetString(DeviceSection, DeviceKey, *DeviceUuid, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	UE_LOG(LogDocent, Log, TEXT("도슨트 클라이언트 초기화. 서버=%s device=%s"), *ServerBaseUrl, *DeviceUuid);
}

void UDocentClient::CheckHealth()
{
	const FString Url = ServerBaseUrl + TEXT("/health");

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(RequestTimeoutSec);
	Request->OnProcessRequestComplete().BindUObject(this, &UDocentClient::OnHealthComplete);

	UE_LOG(LogDocent, Log, TEXT("[health] GET %s"), *Url);
	Request->ProcessRequest();
}

void UDocentClient::OnHealthComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		// 실기기에서 가장 흔한 실패: cleartext HTTP 차단, LAN IP 오기입,
		// 서버가 0.0.0.0 이 아닌 127.0.0.1 로만 리슨, 방화벽.
		ReportFailure(TEXT("서버에 연결하지 못했습니다. LAN IP / uvicorn --host 0.0.0.0 / 방화벽을 확인하세요."));
		return;
	}

	UE_LOG(LogDocent, Log, TEXT("[health] %d %s"), Response->GetResponseCode(), *Response->GetContentAsString());
}

void UDocentClient::AskDocent(const FString& PoiId, const FString& Question)
{
	if (PoiId.IsEmpty())
	{
		ReportFailure(TEXT("PoiId 가 비어 있습니다."));
		return;
	}

	// 본문을 손으로 문자열 조립하면 질문에 들어간 따옴표·개행이 JSON 을 깨뜨린다.
	// 반드시 직렬화기를 통한다.
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("poi_id"), PoiId);
	Body->SetStringField(TEXT("device_uuid"), DeviceUuid);
	if (!Question.IsEmpty())
	{
		// question 을 생략하면 서버가 해당 부위의 기본 해설을 생성한다.
		Body->SetStringField(TEXT("question"), Question);
	}

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Body, Writer);

	const FString Url = ServerBaseUrl + TEXT("/docent/ask");

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(RequestTimeoutSec);
	Request->SetContentAsString(Payload);
	Request->OnProcessRequestComplete().BindUObject(this, &UDocentClient::OnAskComplete);

	UE_LOG(LogDocent, Log, TEXT("[ask] POST %s poi=%s q=%s"), *Url, *PoiId, Question.IsEmpty() ? TEXT("(기본 해설)") : *Question);
	Request->ProcessRequest();
}

void UDocentClient::OnAskComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		ReportFailure(TEXT("서버에 연결하지 못했습니다. LAN IP / uvicorn --host 0.0.0.0 / 방화벽을 확인하세요."));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Content = Response->GetContentAsString();

	if (Code != 200)
	{
		// 404 = poi not found, 422 = 스키마 불일치. 본문에 서버의 detail 이 들어 있다.
		ReportFailure(FString::Printf(TEXT("서버가 %d 를 반환했습니다: %s"), Code, *Content));
		return;
	}

	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		ReportFailure(FString::Printf(TEXT("응답 JSON 파싱에 실패했습니다: %s"), *Content));
		return;
	}

	FDocentAnswer Answer;
	// answer 는 필수. Get* 계열은 필드가 없어도 빈 값을 돌려주므로,
	// 서버 스키마가 바뀌었을 때 빈 말풍선이 뜨는 대신 여기서 실패로 잡는다.
	if (!Json->TryGetStringField(TEXT("answer"), Answer.Answer) || Answer.Answer.IsEmpty())
	{
		ReportFailure(FString::Printf(TEXT("응답에 answer 필드가 없습니다: %s"), *Content));
		return;
	}
	// source / elapsed_ms 는 진단용이라 없어도 표시는 진행한다.
	Json->TryGetStringField(TEXT("source"), Answer.Source);
	Json->TryGetNumberField(TEXT("elapsed_ms"), Answer.ElapsedMs);

	// source=fallback 은 오류가 아니라 "LLM 이 실패해 DB 해설로 대체됨" 이다.
	// 화면에는 정상 노출하되, 원인 추적을 위해 로그에는 남긴다.
	UE_LOG(LogDocent, Log, TEXT("[ask] 200 source=%s elapsed=%dms answer=%s"),
		*Answer.Source, Answer.ElapsedMs, *Answer.Answer);

	OnAnswered.Broadcast(Answer);
}

void UDocentClient::ReportFailure(const FString& Reason)
{
	UE_LOG(LogDocent, Error, TEXT("[docent] %s"), *Reason);
	OnFailed.Broadcast(Reason);
}
