#include "NavClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNav, Log, All);

/** 설정이 비었거나 망가졌고 도슨트 폴백도 없을 때 되돌릴 값. 에디터 PIE 기준. */
static const TCHAR* NavDefaultServerBaseUrl = TEXT("http://127.0.0.1:8000");

// Unity 빌드가 이 파일과 DocentClient.cpp 를 한 번역 단위로 합치므로, 같은 이름의
// 헬퍼를 파일마다 네임스페이스로 가둔다. static 이나 익명 네임스페이스로는 막을 수 없다.
namespace NavClientPrivate
{
	/** 완료 콜백의 요청 포인터는 실패 경로에서 무효할 수 있어 역참조 전에 확인한다. */
	FString SafeGetUrl(const FHttpRequestPtr& Request)
	{
		return Request.IsValid() ? Request->GetURL() : TEXT("(알 수 없음)");
	}
}

/** 공백·끝 슬래시를 정리하고, http(s):// 스킴이 있으면 true. */
static bool NormalizeUrlString(FString& Url)
{
	Url.TrimStartAndEndInline();
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	return !Url.IsEmpty() && (Url.StartsWith(TEXT("http://")) || Url.StartsWith(TEXT("https://")));
}

/**
 * JSON 오브젝트에서 숫자 필드를 float 로 뽑는다. 없으면 0.
 *
 * 서버가 Decimal 컬럼을 **문자열**("130.00")로 직렬화하는 엔드포인트가 있다
 * (markers 는 문자열, route 의 waypoints 는 숫자 — 실측 확인 2026-08-12). 그래서
 * 숫자 파싱이 실패하면 문자열로도 시도한다. 이게 없으면 마커 좌표가 전부 0 이 된다.
 */
static float NavJsonFloat(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
{
	double Value = 0.0;
	if (Obj->TryGetNumberField(Key, Value))
	{
		return static_cast<float>(Value);
	}
	FString Str;
	if (Obj->TryGetStringField(Key, Str) && !Str.IsEmpty())
	{
		return FCString::Atof(*Str);
	}
	return 0.f;
}

/** 스모크/에러 로그용 짧은 HTTP 실패 사유. */
static FString DescribeHttp(bool bConnected, const FHttpResponsePtr& Response)
{
	if (!bConnected || !Response.IsValid())
	{
		return TEXT("서버 연결 실패");
	}
	return FString::Printf(TEXT("HTTP %d: %s"),
		Response->GetResponseCode(), *Response->GetContentAsString().Left(200));
}

void UNavClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	NormalizeServerBaseUrl();

	UE_LOG(LogNav, Log, TEXT("NavClient 초기화. 서버=%s mapId=%s timeout=%.0fs"),
		*ServerBaseUrl, DefaultMapId.IsEmpty() ? TEXT("(미설정)") : *DefaultMapId, RequestTimeoutSec);

#if !UE_BUILD_SHIPPING
	RegisterDebugConsoleCommands();
#endif

	if (bRunSmokeTestOnStart)
	{
		RunSmokeTest();
	}
}

void UNavClient::Deinitialize()
{
#if !UE_BUILD_SHIPPING
	for (IConsoleObject* Command : DebugConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command);
	}
	DebugConsoleCommands.Reset();
#endif

	Super::Deinitialize();
}

void UNavClient::NormalizeServerBaseUrl()
{
	const bool bSelfValid = NormalizeUrlString(ServerBaseUrl);

	// 도슨트 주소를 폴백/대조용으로 읽어 온다. DefaultGame.ini 의 이웃 섹션.
	FString DocentUrl;
	GConfig->GetString(TEXT("/Script/TimeMachineAR.DocentClient"), TEXT("ServerBaseUrl"), DocentUrl, GGameIni);
	const bool bDocentValid = NormalizeUrlString(DocentUrl);

	if (!bSelfValid)
	{
		if (bDocentValid)
		{
			// 네비 주소를 비워 두면 IP 를 도슨트 한 군데만 고쳐도 네비가 따라온다.
			UE_LOG(LogNav, Warning,
				TEXT("NavClient.ServerBaseUrl 이 비었거나 무효라 DocentClient 주소로 폴백합니다: %s"), *DocentUrl);
			ServerBaseUrl = DocentUrl;
		}
		else
		{
			UE_LOG(LogNav, Error,
				TEXT("ServerBaseUrl 이 유효하지 않습니다(값=\"%s\"). DefaultGame.ini 의 ")
				TEXT("[/Script/TimeMachineAR.NavClient] ServerBaseUrl 을 http:// 또는 https:// 로 ")
				TEXT("시작하도록 고치세요(따옴표째). 일단 %s 로 진행합니다."),
				*ServerBaseUrl, NavDefaultServerBaseUrl);
			ServerBaseUrl = NavDefaultServerBaseUrl;
		}
	}
	else if (bDocentValid && DocentUrl != ServerBaseUrl)
	{
		// 실기기에서 한쪽 IP 만 바꾸면 "도슨트는 되는데 네비만 안 되는" 디버깅 지옥이 열린다.
		UE_LOG(LogNav, Warning,
			TEXT("네비(%s)와 도슨트(%s) 서버 주소가 다릅니다. 실기기 테스트 시 두 섹션의 ")
			TEXT("IP 를 함께 맞췄는지 확인하세요."),
			*ServerBaseUrl, *DocentUrl);
	}
}

FString UNavClient::BuildUrl(const FString& Path) const
{
	return ServerBaseUrl + Path;
}

FString UNavClient::ResolveMapId(const FString& MapId) const
{
	return MapId.IsEmpty() ? DefaultMapId : MapId;
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UNavClient::MakeGet(const FString& Url) const
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(RequestTimeoutSec);
	return Request;
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UNavClient::MakePost(const FString& Url, const FString& Body) const
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(RequestTimeoutSec);
	Request->SetContentAsString(Body);
	return Request;
}

// -------------------------------------------------------------------- 조회 API

void UNavClient::GetMarkers(const FString& MapId)
{
	const FString Id = ResolveMapId(MapId);
	if (Id.IsEmpty())
	{
		ReportFailure(-1, TEXT("MapId 가 비어 있습니다(DefaultMapId 도 미설정)."));
		return;
	}

	const FString Url = BuildUrl(FString::Printf(TEXT("/maps/%s/markers"), *Id));
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeGet(Url);
	Request->OnProcessRequestComplete().BindUObject(this, &UNavClient::OnMarkersComplete);

	UE_LOG(LogNav, Log, TEXT("[markers] GET %s"), *Url);
	Request->ProcessRequest();
}

void UNavClient::OnMarkersComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		ReportFailure(-1, FString::Printf(
			TEXT("마커 조회 실패 (URL: %s). uvicorn --host 0.0.0.0 / 방화벽 / 같은 WiFi 를 확인하세요."),
			*NavClientPrivate::SafeGetUrl(Request)));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		ReportFailure(Code, FString::Printf(TEXT("마커 조회 — 서버가 %d 를 반환: %s"),
			Code, *Response->GetContentAsString().Left(300)));
		return;
	}

	TArray<FNavMarker> Markers;
	if (!ParseMarkersArray(Response->GetContentAsString(), Markers))
	{
		ReportFailure(-2, FString::Printf(TEXT("마커 응답 파싱 실패: %s"),
			*Response->GetContentAsString().Left(300)));
		return;
	}

	UE_LOG(LogNav, Log, TEXT("[markers] 200 (%d건)"), Markers.Num());
	OnMarkersReceived.Broadcast(Markers);
}

void UNavClient::GetDestinations(const FString& MapId)
{
	const FString Id = ResolveMapId(MapId);
	if (Id.IsEmpty())
	{
		ReportFailure(-1, TEXT("MapId 가 비어 있습니다(DefaultMapId 도 미설정)."));
		return;
	}

	const FString Url = BuildUrl(FString::Printf(TEXT("/maps/%s/destinations"), *Id));
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeGet(Url);
	Request->OnProcessRequestComplete().BindUObject(this, &UNavClient::OnDestinationsComplete);

	UE_LOG(LogNav, Log, TEXT("[destinations] GET %s"), *Url);
	Request->ProcessRequest();
}

void UNavClient::OnDestinationsComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		ReportFailure(-1, FString::Printf(
			TEXT("목적지 조회 실패 (URL: %s)."), *NavClientPrivate::SafeGetUrl(Request)));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		ReportFailure(Code, FString::Printf(TEXT("목적지 조회 — 서버가 %d 를 반환: %s"),
			Code, *Response->GetContentAsString().Left(300)));
		return;
	}

	TArray<FNavDestination> Destinations;
	if (!ParseDestinationsArray(Response->GetContentAsString(), Destinations))
	{
		ReportFailure(-2, FString::Printf(TEXT("목적지 응답 파싱 실패: %s"),
			*Response->GetContentAsString().Left(300)));
		return;
	}

	UE_LOG(LogNav, Log, TEXT("[destinations] 200 (%d건)"), Destinations.Num());
	OnDestinationsReceived.Broadcast(Destinations);
}

// -------------------------------------------------------------------- 경로 API

void UNavClient::RequestRoute(const FString& MapId, const FNavMapPose& From,
	const FString& ToNodeId, bool bAccessibleOnly)
{
	SendRoute(BuildUrl(TEXT("/navigation/route")), MapId, From, ToNodeId, bAccessibleOnly);
}

void UNavClient::Reroute(const FString& MapId, const FNavMapPose& From,
	const FString& ToNodeId, bool bAccessibleOnly)
{
	SendRoute(BuildUrl(TEXT("/navigation/reroute")), MapId, From, ToNodeId, bAccessibleOnly);
}

void UNavClient::SendRoute(const FString& Url, const FString& MapId, const FNavMapPose& From,
	const FString& ToNodeId, bool bAccessibleOnly)
{
	const FString Id = ResolveMapId(MapId);
	if (Id.IsEmpty())
	{
		ReportFailure(-1, TEXT("MapId 가 비어 있습니다(DefaultMapId 도 미설정)."));
		return;
	}
	if (ToNodeId.IsEmpty())
	{
		ReportFailure(-1, TEXT("ToNodeId 가 비어 있습니다."));
		return;
	}

	const FString Body = BuildRouteBody(Id, From, ToNodeId, bAccessibleOnly);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakePost(Url, Body);
	Request->OnProcessRequestComplete().BindUObject(this, &UNavClient::OnRouteComplete);

	UE_LOG(LogNav, Log, TEXT("[route] POST %s to=%s accessible=%s"),
		*Url, *ToNodeId, bAccessibleOnly ? TEXT("true") : TEXT("false"));
	Request->ProcessRequest();
}

FString UNavClient::BuildRouteBody(const FString& MapId, const FNavMapPose& From,
	const FString& ToNodeId, bool bAccessibleOnly) const
{
	// 손으로 문자열 조립하면 UUID·부동소수 표기에서 JSON 이 깨질 수 있어 직렬화기를 통한다.
	const TSharedRef<FJsonObject> FromObj = MakeShared<FJsonObject>();
	FromObj->SetNumberField(TEXT("pos_x_cm"), From.PosXCm);
	FromObj->SetNumberField(TEXT("pos_y_cm"), From.PosYCm);
	FromObj->SetNumberField(TEXT("pos_z_cm"), From.PosZCm);
	if (From.bHasHeading)
	{
		// heading 을 생략하면 서버가 출발 회전 안내를 건너뛴다.
		FromObj->SetNumberField(TEXT("heading_deg"), From.HeadingDeg);
	}

	const TSharedRef<FJsonObject> ToObj = MakeShared<FJsonObject>();
	ToObj->SetStringField(TEXT("node_id"), ToNodeId);

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("map_id"), MapId);
	Body->SetObjectField(TEXT("from"), FromObj);
	Body->SetObjectField(TEXT("to"), ToObj);
	Body->SetBoolField(TEXT("accessible_only"), bAccessibleOnly);

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Body, Writer);
	return Payload;
}

void UNavClient::OnRouteComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		ReportFailure(-1, FString::Printf(
			TEXT("경로 요청 실패 (URL: %s)."), *NavClientPrivate::SafeGetUrl(Request)));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		// 404 = 맵/노드 없음, 422 = 연결된 경로 없음(또는 to 검증 실패).
		ReportFailure(Code, FString::Printf(TEXT("경로 요청 — 서버가 %d 를 반환: %s"),
			Code, *Response->GetContentAsString().Left(300)));
		return;
	}

	FNavRoute Route;
	if (!ParseRoute(Response->GetContentAsString(), Route))
	{
		ReportFailure(-2, FString::Printf(TEXT("경로 응답 파싱 실패: %s"),
			*Response->GetContentAsString().Left(300)));
		return;
	}

	UE_LOG(LogNav, Log, TEXT("[route] 200 (total=%.1fcm, waypoints=%d, steps=%d)"),
		Route.TotalDistanceCm, Route.Waypoints.Num(), Route.Steps.Num());
	OnRouteReceived.Broadcast(Route);
}

void UNavClient::ReportFailure(int32 StatusCode, const FString& Reason)
{
	UE_LOG(LogNav, Error, TEXT("[nav] (%d) %s"), StatusCode, *Reason);
	OnRequestFailed.Broadcast(StatusCode, Reason);
}

// -------------------------------------------------------------------- 파서

bool UNavClient::ParseMarkerObject(const TSharedPtr<FJsonObject>& Obj, FNavMarker& Out)
{
	if (!Obj.IsValid())
	{
		return false;
	}
	// code 는 필수. 나머지는 없으면 기본값(0/빈문자열)으로 둔다.
	if (!Obj->TryGetStringField(TEXT("code"), Out.Code) || Out.Code.IsEmpty())
	{
		return false;
	}
	Obj->TryGetStringField(TEXT("marker_type"), Out.MarkerType);
	Out.PosXCm = NavJsonFloat(Obj, TEXT("pos_x_cm"));
	Out.PosYCm = NavJsonFloat(Obj, TEXT("pos_y_cm"));
	Out.PosZCm = NavJsonFloat(Obj, TEXT("pos_z_cm"));
	Out.HeadingDeg = NavJsonFloat(Obj, TEXT("heading_deg"));
	Obj->TryGetStringField(TEXT("note"), Out.Note);
	return true;
}

bool UNavClient::ParseMarkersArray(const FString& Content, TArray<FNavMarker>& Out)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Array))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : Array)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Value.IsValid() && Value->TryGetObject(Obj))
		{
			FNavMarker Marker;
			if (ParseMarkerObject(*Obj, Marker))
			{
				Out.Add(Marker);
			}
		}
	}
	return true;
}

bool UNavClient::ParseDestinationsArray(const FString& Content, TArray<FNavDestination>& Out)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Array))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : Array)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Value.IsValid() && Value->TryGetObject(Obj))
		{
			FNavDestination Dest;
			if ((*Obj)->TryGetStringField(TEXT("node_id"), Dest.NodeId) && !Dest.NodeId.IsEmpty())
			{
				(*Obj)->TryGetStringField(TEXT("label"), Dest.Label);
				(*Obj)->TryGetStringField(TEXT("node_type"), Dest.NodeType);
				Out.Add(Dest);
			}
		}
	}
	return true;
}

bool UNavClient::ParseRoute(const FString& Content, FNavRoute& Out)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return false;
	}

	Out.TotalDistanceCm = NavJsonFloat(Json, TEXT("total_distance_cm"));

	const TArray<TSharedPtr<FJsonValue>>* Waypoints = nullptr;
	if (Json->TryGetArrayField(TEXT("waypoints"), Waypoints))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Waypoints)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Obj))
			{
				FNavWaypoint Wp;
				(*Obj)->TryGetStringField(TEXT("node_id"), Wp.NodeId);
				Wp.PosXCm = NavJsonFloat(*Obj, TEXT("pos_x_cm"));
				Wp.PosYCm = NavJsonFloat(*Obj, TEXT("pos_y_cm"));
				Wp.PosZCm = NavJsonFloat(*Obj, TEXT("pos_z_cm"));
				(*Obj)->TryGetStringField(TEXT("node_type"), Wp.NodeType);
				Out.Waypoints.Add(Wp);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (Json->TryGetArrayField(TEXT("steps"), Steps))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Steps)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Obj))
			{
				FNavStep Step;
				(*Obj)->TryGetStringField(TEXT("instruction"), Step.Instruction);
				// distance_cm 는 회전/도착 step 에서 null 로 온다. TryGetNumberField 는
				// null·부재면 false 를 돌려주므로 그것으로 bHasDistance 를 판별한다.
				double Distance = 0.0;
				Step.bHasDistance = (*Obj)->TryGetNumberField(TEXT("distance_cm"), Distance);
				Step.DistanceCm = Step.bHasDistance ? static_cast<float>(Distance) : -1.f;
				// turn 은 도착 시 null → 빈 문자열로 남긴다.
				(*Obj)->TryGetStringField(TEXT("turn"), Step.Turn);
				(*Obj)->TryGetBoolField(TEXT("arrive"), Step.bArrive);
				Out.Steps.Add(Step);
			}
		}
	}

	return true;
}

// -------------------------------------------------------------------- 스모크 테스트

void UNavClient::RunSmokeTest()
{
	if (DefaultMapId.IsEmpty())
	{
		UE_LOG(LogNav, Warning,
			TEXT("=== 스모크 테스트 건너뜀: DefaultMapId 미설정. DefaultGame.ini 의 ")
			TEXT("[/Script/TimeMachineAR.NavClient] DefaultMapId 를 채우세요. ==="));
		return;
	}

	const TSharedRef<FSmokeRun> Run = MakeShared<FSmokeRun>();
	Run->MapId = DefaultMapId;

	UE_LOG(LogNav, Log, TEXT("=== NavClient 스모크 테스트 시작 ==="));
	UE_LOG(LogNav, Log, TEXT("BaseUrl=%s  MapId=%s"), *ServerBaseUrl, *Run->MapId);

	SmokeStepMap(Run);
}

void UNavClient::SmokeStepMap(const TSharedRef<FSmokeRun>& Run)
{
	const FString Url = BuildUrl(FString::Printf(TEXT("/maps/%s"), *Run->MapId));
	TWeakObjectPtr<UNavClient> WeakThis(this);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeGet(Url);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Run](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			UNavClient* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
			{
				TSharedPtr<FJsonObject> Json;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
				FString Name, Coord;
				if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
				{
					Json->TryGetStringField(TEXT("name"), Name);
					Json->TryGetStringField(TEXT("coord_system"), Coord);
				}
				Run->Success++;
				UE_LOG(LogNav, Log, TEXT("[1/4] GET /maps/{id} ... OK (name=\"%s\", coord=%s)"), *Name, *Coord);
			}
			else
			{
				UE_LOG(LogNav, Warning, TEXT("[1/4] GET /maps/{id} ... 실패 (%s)"), *DescribeHttp(bOk, Resp));
			}
			Self->SmokeStepMarkers(Run);
		});
	Request->ProcessRequest();
}

void UNavClient::SmokeStepMarkers(const TSharedRef<FSmokeRun>& Run)
{
	const FString Url = BuildUrl(FString::Printf(TEXT("/maps/%s/markers"), *Run->MapId));
	TWeakObjectPtr<UNavClient> WeakThis(this);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeGet(Url);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Run](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			UNavClient* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			TArray<FNavMarker> Markers;
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 &&
				ParseMarkersArray(Resp->GetContentAsString(), Markers))
			{
				Run->Success++;
				if (Markers.Num() > 0)
				{
					Run->FirstMarker = Markers[0];
					Run->bHaveMarker = true;
					const FNavMarker& M = Markers[0];
					UE_LOG(LogNav, Log,
						TEXT("[2/4] GET /markers ... OK (%d건) code=%s pos=(%.1f,%.1f,%.1f) heading=%.1f"),
						Markers.Num(), *M.Code, M.PosXCm, M.PosYCm, M.PosZCm, M.HeadingDeg);
				}
				else
				{
					UE_LOG(LogNav, Log, TEXT("[2/4] GET /markers ... OK (0건)"));
				}
			}
			else
			{
				UE_LOG(LogNav, Warning, TEXT("[2/4] GET /markers ... 실패 (%s)"), *DescribeHttp(bOk, Resp));
			}
			Self->SmokeStepDestinations(Run);
		});
	Request->ProcessRequest();
}

void UNavClient::SmokeStepDestinations(const TSharedRef<FSmokeRun>& Run)
{
	const FString Url = BuildUrl(FString::Printf(TEXT("/maps/%s/destinations"), *Run->MapId));
	TWeakObjectPtr<UNavClient> WeakThis(this);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeGet(Url);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Run](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			UNavClient* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			TArray<FNavDestination> Destinations;
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 &&
				ParseDestinationsArray(Resp->GetContentAsString(), Destinations))
			{
				Run->Success++;
				if (Destinations.Num() > 0)
				{
					Run->FirstDestId = Destinations[0].NodeId;
					Run->FirstDestLabel = Destinations[0].Label;
				}
				// 목록을 "라벨/타입" 몇 개만 미리보기로 남긴다.
				FString Preview;
				for (int32 i = 0; i < Destinations.Num() && i < 5; ++i)
				{
					Preview += FString::Printf(TEXT("%s%s/%s"), i == 0 ? TEXT("") : TEXT(", "),
						*Destinations[i].Label, *Destinations[i].NodeType);
				}
				UE_LOG(LogNav, Log, TEXT("[3/4] GET /destinations ... OK (%d건) [%s]"),
					Destinations.Num(), *Preview);
			}
			else
			{
				UE_LOG(LogNav, Warning, TEXT("[3/4] GET /destinations ... 실패 (%s)"), *DescribeHttp(bOk, Resp));
			}
			Self->SmokeStepRoute(Run);
		});
	Request->ProcessRequest();
}

void UNavClient::SmokeStepRoute(const TSharedRef<FSmokeRun>& Run)
{
	if (Run->FirstDestId.IsEmpty())
	{
		// 목적지를 못 받았으면 경로를 낼 수 없다. 이 항목은 실패로 두고 요약으로 간다.
		UE_LOG(LogNav, Warning,
			TEXT("[4/4] POST /navigation/route ... 건너뜀 (목적지 목록이 비어 to 를 정할 수 없음)"));
		SmokeFinish(Run);
		return;
	}

	// from 은 마커 pose(측위는 다음 단계이므로), to 는 첫 목적지.
	FNavMapPose From;
	if (Run->bHaveMarker)
	{
		From.PosXCm = Run->FirstMarker.PosXCm;
		From.PosYCm = Run->FirstMarker.PosYCm;
		From.PosZCm = Run->FirstMarker.PosZCm;
		From.HeadingDeg = Run->FirstMarker.HeadingDeg;
		From.bHasHeading = true;
	}

	const FString Url = BuildUrl(TEXT("/navigation/route"));
	const FString Body = BuildRouteBody(Run->MapId, From, Run->FirstDestId, /*bAccessibleOnly=*/false);
	TWeakObjectPtr<UNavClient> WeakThis(this);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakePost(Url, Body);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Run](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			UNavClient* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			FNavRoute Route;
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 &&
				ParseRoute(Resp->GetContentAsString(), Route))
			{
				Run->Success++;
				UE_LOG(LogNav, Log,
					TEXT("[4/4] POST /navigation/route ... OK (total=%.1fcm, waypoints=%d, steps=%d) to=\"%s\""),
					Route.TotalDistanceCm, Route.Waypoints.Num(), Route.Steps.Num(), *Run->FirstDestLabel);
				if (Route.Steps.Num() > 0)
				{
					const FNavStep& S = Route.Steps[0];
					UE_LOG(LogNav, Log, TEXT("       step[0] turn=%s dist=%s \"%s\""),
						S.Turn.IsEmpty() ? TEXT("(none)") : *S.Turn,
						S.bHasDistance ? *FString::Printf(TEXT("%.1f"), S.DistanceCm) : TEXT("null"),
						*S.Instruction);
				}
			}
			else
			{
				UE_LOG(LogNav, Warning, TEXT("[4/4] POST /navigation/route ... 실패 (%s)"), *DescribeHttp(bOk, Resp));
			}
			Self->SmokeFinish(Run);
		});
	Request->ProcessRequest();
}

void UNavClient::SmokeFinish(const TSharedRef<FSmokeRun>& Run)
{
	UE_LOG(LogNav, Log, TEXT("=== 스모크 테스트 결과: %d/4 성공 ==="), Run->Success);
}

// -------------------------------------------------------------------- 디버그 콘솔

#if !UE_BUILD_SHIPPING
void UNavClient::RegisterDebugConsoleCommands()
{
	// 람다가 this 를 강하게 붙들면 서브시스템이 죽은 뒤에도 호출될 수 있다.
	TWeakObjectPtr<UNavClient> WeakThis(this);

	DebugConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Nav.Smoke"),
		TEXT("네비 배관 스모크 테스트를 다시 돈다 (maps→markers→destinations→route)."),
		FConsoleCommandDelegate::CreateLambda([WeakThis]()
		{
			if (UNavClient* Self = WeakThis.Get())
			{
				Self->RunSmokeTest();
			}
		})));

	DebugConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Nav.DumpMarkers"),
		TEXT("GET /maps/{DefaultMapId}/markers 를 호출하고 결과를 LogNav 에 남긴다."),
		FConsoleCommandDelegate::CreateLambda([WeakThis]()
		{
			if (UNavClient* Self = WeakThis.Get())
			{
				Self->GetMarkers(FString());
			}
		})));

	DebugConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Nav.DumpDestinations"),
		TEXT("GET /maps/{DefaultMapId}/destinations 를 호출하고 결과를 LogNav 에 남긴다."),
		FConsoleCommandDelegate::CreateLambda([WeakThis]()
		{
			if (UNavClient* Self = WeakThis.Get())
			{
				Self->GetDestinations(FString());
			}
		})));

	DebugConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Nav.DumpRoute"),
		TEXT("POST /navigation/route 를 호출한다. 인자: <ToNodeId>. from 은 원점(heading 없음)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([WeakThis](const TArray<FString>& Args)
		{
			UNavClient* Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			if (Args.Num() < 1)
			{
				UE_LOG(LogNav, Warning, TEXT("사용법: Nav.DumpRoute <ToNodeId>"));
				return;
			}
			// PIE 편의용: from 은 원점(heading 없음). 서버가 출발 노드로 스냅한다.
			Self->RequestRoute(FString(), FNavMapPose(), Args[0], /*bAccessibleOnly=*/false);
		})));

	UE_LOG(LogNav, Log,
		TEXT("진단 콘솔 명령 등록: Nav.Smoke / Nav.DumpMarkers / Nav.DumpDestinations / Nav.DumpRoute <ToNodeId>"));
}
#endif
