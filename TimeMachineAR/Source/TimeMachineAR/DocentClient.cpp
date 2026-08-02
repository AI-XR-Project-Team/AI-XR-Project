#include "DocentClient.h"

#include "SseParser.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogDocent, Log, All);

/** 설정이 비었거나 망가졌을 때 되돌릴 값. 에디터 PIE 기준. */
static const TCHAR* DefaultServerBaseUrl = TEXT("http://127.0.0.1:8000");

/** 완료 콜백의 요청 포인터는 실패 경로에서 무효할 수 있어 역참조 전에 확인한다. */
static FString SafeGetUrl(const FHttpRequestPtr& Request)
{
	return Request.IsValid() ? Request->GetURL() : TEXT("(알 수 없음)");
}

void UDocentClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	NormalizeServerBaseUrl();

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

void UDocentClient::NormalizeServerBaseUrl()
{
	ServerBaseUrl.TrimStartAndEndInline();

	// 끝 슬래시가 있으면 BuildUrl 에서 "//health" 가 된다.
	while (ServerBaseUrl.EndsWith(TEXT("/")))
	{
		ServerBaseUrl.LeftChopInline(1);
	}

	const bool bHasScheme = ServerBaseUrl.StartsWith(TEXT("http://")) || ServerBaseUrl.StartsWith(TEXT("https://"));
	if (ServerBaseUrl.IsEmpty() || !bHasScheme)
	{
		UE_LOG(LogDocent, Error,
			TEXT("ServerBaseUrl 이 유효하지 않습니다(값=\"%s\"). DefaultGame.ini 의 ")
			TEXT("[/Script/TimeMachineAR.DocentClient] ServerBaseUrl 을 확인하세요. ")
			TEXT("http:// 또는 https:// 로 시작해야 합니다. 일단 %s 로 진행합니다."),
			*ServerBaseUrl, DefaultServerBaseUrl);
		ServerBaseUrl = DefaultServerBaseUrl;
	}
}

void UDocentClient::SetServerBaseUrl(const FString& NewBaseUrl)
{
	ServerBaseUrl = NewBaseUrl;
	NormalizeServerBaseUrl();
	UE_LOG(LogDocent, Log, TEXT("서버 주소를 %s 로 변경했습니다(이번 실행에만 적용)."), *ServerBaseUrl);
}

FString UDocentClient::BuildUrl(const FString& Path) const
{
	return ServerBaseUrl + Path;
}

void UDocentClient::CheckHealth()
{
	const FString Url = BuildUrl(TEXT("/health"));

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
		// 어떤 URL 로 나갔는지 함께 남긴다. 주소가 잘못된 경우와 네트워크가
		// 막힌 경우는 증상이 같아서, URL 없이는 원인을 가릴 수 없다.
		ReportFailure(FString::Printf(
			TEXT("서버 연결 실패 (요청 URL: %s). 주소가 맞다면 uvicorn --host 0.0.0.0 / 방화벽 / adb reverse 를 확인하세요."),
			*SafeGetUrl(Request)));
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

	const FString Url = BuildUrl(TEXT("/docent/ask"));

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
		ReportFailure(FString::Printf(
			TEXT("서버 연결 실패 (요청 URL: %s). 주소가 맞다면 uvicorn --host 0.0.0.0 / 방화벽 / adb reverse 를 확인하세요."),
			*SafeGetUrl(Request)));
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

// ---------------------------------------------------------------------- 챗봇

void UDocentClient::StartChatSession(const FString& ExhibitId)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("device_uuid"), DeviceUuid);
	if (!ExhibitId.IsEmpty())
	{
		Body->SetStringField(TEXT("exhibit_id"), ExhibitId);
	}

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Body, Writer);

	const FString Url = BuildUrl(TEXT("/docent/sessions"));

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(RequestTimeoutSec);
	Request->SetContentAsString(Payload);
	Request->OnProcessRequestComplete().BindUObject(this, &UDocentClient::OnSessionCreated);

	UE_LOG(LogDocent, Log, TEXT("[chat] 세션 생성 POST %s exhibit=%s"), *Url,
		ExhibitId.IsEmpty() ? TEXT("(없음)") : *ExhibitId);
	Request->ProcessRequest();
}

void UDocentClient::OnSessionCreated(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		ReportChatFailure(
			FString::Printf(TEXT("세션 생성 실패 — 서버에 연결하지 못했습니다 (URL: %s)."),
				*SafeGetUrl(Request)),
			/*bPartial=*/false);
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		ReportChatFailure(
			FString::Printf(TEXT("세션 생성 실패 — 서버가 %d 를 반환했습니다: %s"),
				Response->GetResponseCode(), *Response->GetContentAsString()),
			false);
		return;
	}

	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid() ||
		!Json->TryGetStringField(TEXT("session_id"), ChatSessionId))
	{
		ReportChatFailure(
			FString::Printf(TEXT("세션 응답을 해석하지 못했습니다: %s"),
				*Response->GetContentAsString()),
			false);
		return;
	}

	UE_LOG(LogDocent, Log, TEXT("[chat] 세션 준비됨: %s"), *ChatSessionId);
	OnChatSessionReady.Broadcast(ChatSessionId);
}

void UDocentClient::SendChatMessage(const FString& Message, const FString& PoiId)
{
	if (ChatSessionId.IsEmpty())
	{
		ReportChatFailure(TEXT("대화 세션이 아직 없습니다. StartChatSession 을 먼저 호출하세요."), false);
		return;
	}
	if (Message.TrimStartAndEnd().IsEmpty())
	{
		ReportChatFailure(TEXT("빈 질문은 보낼 수 없습니다."), false);
		return;
	}
	if (bIsChatStreaming)
	{
		// 응답을 받는 중에 또 보내면 두 응답의 조각이 한 말풍선에 섞인다.
		ReportChatFailure(TEXT("이전 응답을 받는 중입니다."), false);
		return;
	}

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("session_id"), ChatSessionId);
	Body->SetStringField(TEXT("message"), Message);
	if (!PoiId.IsEmpty())
	{
		Body->SetStringField(TEXT("poi_id"), PoiId);
	}

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Body, Writer);

	const FString Url = BuildUrl(TEXT("/docent/chat/stream"));

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
	Request->SetTimeout(RequestTimeoutSec);
	Request->SetContentAsString(Payload);

	// 수신 버퍼는 요청 하나에 종속된다. 델리게이트가 HTTP 스레드에서 호출되므로
	// UObject 멤버가 아니라 이 공유 상태에 담아 수명을 요청과 묶는다.
	TSharedPtr<FSseParser, ESPMode::ThreadSafe> Parser = MakeShared<FSseParser, ESPMode::ThreadSafe>();
	TWeakObjectPtr<UDocentClient> WeakThis(this);

	const bool bStreamReady = Request->SetResponseBodyReceiveStreamDelegate(
		FHttpRequestStreamDelegate::CreateLambda(
			[Parser, WeakThis](void* Ptr, int64 Length) -> bool
			{
				// !! 게임 스레드가 아니다 !!
				// 파싱까지만 여기서 하고, 브로드캐스트는 게임 스레드로 넘긴다.
				Parser->AppendBytes(Ptr, Length);

				FSseEvent Event;
				while (Parser->NextEvent(Event))
				{
					const FString EventName = Event.Event;
					const FString Data = Event.Data;
					AsyncTask(ENamedThreads::GameThread,
						[WeakThis, EventName, Data]()
						{
							if (UDocentClient* Self = WeakThis.Get())
							{
								Self->HandleSseEvent(EventName, Data);
							}
						});
				}
				return true;
			}));

	if (!bStreamReady)
	{
		// 이 플랫폼의 HTTP 구현이 스트리밍 수신을 지원하지 않는다.
		// 서버에 같은 대화를 비스트리밍으로 처리하는 경로가 있으므로 그쪽을 쓴다.
		UE_LOG(LogDocent, Warning,
			TEXT("[chat] 스트리밍 수신을 쓸 수 없어 비스트리밍(/docent/chat)으로 전환합니다."));
		Request->SetURL(BuildUrl(TEXT("/docent/chat")));
	}

	Request->OnProcessRequestComplete().BindUObject(this, &UDocentClient::OnChatStreamComplete);

	bIsChatStreaming = true;
	bChatTerminated = false;
	ChatAccumulated.Reset();

	UE_LOG(LogDocent, Log, TEXT("[chat] POST %s poi=%s q=%s"), *Request->GetURL(),
		PoiId.IsEmpty() ? TEXT("(전체)") : *PoiId, *Message);
	Request->ProcessRequest();
}

void UDocentClient::HandleSseEvent(const FString& EventName, const FString& Data)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		UE_LOG(LogDocent, Warning, TEXT("[chat] data 를 해석하지 못했습니다: %s"), *Data);
		return;
	}

	if (EventName == TEXT("delta"))
	{
		FString Text;
		if (Json->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			ChatAccumulated += Text;
			OnChatDelta.Broadcast(Text);
		}
		return;
	}

	if (EventName == TEXT("done"))
	{
		FDocentChatResult Result;
		Result.FullText = ChatAccumulated;
		Json->TryGetStringField(TEXT("source"), Result.Source);
		Json->TryGetNumberField(TEXT("ttft_ms"), Result.TtftMs);
		Json->TryGetNumberField(TEXT("total_ms"), Result.TotalMs);

		bChatTerminated = true;
		bIsChatStreaming = false;

		UE_LOG(LogDocent, Log, TEXT("[chat] 완료 source=%s ttft=%dms total=%dms len=%d"),
			*Result.Source, Result.TtftMs, Result.TotalMs, Result.FullText.Len());
		OnChatCompleted.Broadcast(Result);
		return;
	}

	if (EventName == TEXT("error"))
	{
		FString Detail;
		bool bPartial = false;
		Json->TryGetStringField(TEXT("detail"), Detail);
		Json->TryGetBoolField(TEXT("partial"), bPartial);

		bChatTerminated = true;
		ReportChatFailure(Detail, bPartial);
		return;
	}

	// meta 등 그 외 프레임은 로그만 남긴다.
	UE_LOG(LogDocent, Verbose, TEXT("[chat] %s %s"), *EventName, *Data);
}

void UDocentClient::OnChatStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	// 스트리밍을 켜면 응답 본문이 캐시되지 않아 여기서 GetContentAsString() 은
	// 비어 있다. 본문은 전부 스트림 델리게이트로 이미 처리됐다.
	if (bChatTerminated)
	{
		bIsChatStreaming = false;
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		ReportChatFailure(
			FString::Printf(TEXT("대화 요청 실패 (URL: %s). 서버 연결을 확인하세요."),
				*SafeGetUrl(Request)),
			!ChatAccumulated.IsEmpty());
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		ReportChatFailure(
			FString::Printf(TEXT("서버가 %d 를 반환했습니다: %s"), Code, *Response->GetContentAsString()),
			false);
		return;
	}

	// 200 인데 done/error 를 못 받았다면 스트림이 조용히 끊긴 것이다.
	// 비스트리밍으로 전환된 경우도 여기로 온다.
	const FString Body = Response->GetContentAsString();
	if (!Body.IsEmpty())
	{
		// 비스트리밍 폴백 경로: 단일 JSON 응답이다.
		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
		{
			FDocentChatResult Result;
			if (Json->TryGetStringField(TEXT("answer"), Result.FullText))
			{
				Json->TryGetStringField(TEXT("source"), Result.Source);
				Json->TryGetNumberField(TEXT("ttft_ms"), Result.TtftMs);
				Json->TryGetNumberField(TEXT("total_ms"), Result.TotalMs);

				bIsChatStreaming = false;
				OnChatDelta.Broadcast(Result.FullText);
				OnChatCompleted.Broadcast(Result);
				return;
			}
		}
	}

	ReportChatFailure(TEXT("응답이 완료되지 않은 채 연결이 끊겼습니다."),
		!ChatAccumulated.IsEmpty());
}

void UDocentClient::ReportChatFailure(const FString& Reason, bool bPartial)
{
	bIsChatStreaming = false;
	UE_LOG(LogDocent, Error, TEXT("[chat] %s (부분응답=%s)"), *Reason,
		bPartial ? TEXT("있음") : TEXT("없음"));
	OnChatFailed.Broadcast(Reason, bPartial);
}
