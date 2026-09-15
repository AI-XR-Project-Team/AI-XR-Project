// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavArCoreConfig.h"

#include "Misc/ConfigCacheIni.h"

// ARCore(GoogleARCoreServices) 의존은 Android 타깃에만 걸린다(TimeMachineAR.Build.cs) — 플러그인 호출은 전부 이 안이다.
#if NAV_CLOUD_RESOLVE
#include "ARBlueprintLibrary.h"
#include "ARSessionConfig.h"
#include "ARSupportInterface.h"                          // 세션 핸들 — 리졸버 TryCancelPendingPin 과 같은 경로
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"
#include "GoogleARCoreServicesFunctionLibrary.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNavArCoreConfig, Log, All);

// 익명 네임스페이스라도 유니티 빌드에선 다른 Nav*.cpp 와 한 TU 에 묶이므로 이름을 ArCoreCfg* 로 구분한다.
namespace
{
	const TCHAR* const kArCoreCfgSpawnerSection = TEXT("/Script/TimeMachineAR.NavCloudAssetSpawner");
}

bool NavArCoreConfig::AreMarkerTriggersEnabled()
{
	bool bEnabled = true;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(kArCoreCfgSpawnerSection, TEXT("bMarkerTriggers"), bEnabled, GGameIni);
	}
	return bEnabled;
}

#if NAV_CLOUD_RESOLVE

// ARCore C API — 쓰는 함수만 엔진 동봉 arcore_c_api.h 와 **같은 시그니처**로 선언한다(헤더 주석 · 리졸버 ArFuture_cancel 과
// 같은 방식). 심볼은 GoogleARCoreBase·Services 가 이미 링크하는 libarcore_sdk_c.so 에 있다. 시그니처를 바꾸지 말 것.
typedef struct ArSession_ ArSession;
typedef struct ArConfig_ ArConfig;
typedef struct ArAugmentedImageDatabase_ ArAugmentedImageDatabase;
enum ArStatus : int32_t;
extern "C"
{
	void ArConfig_create(const ArSession* session, ArConfig** out_config);
	void ArConfig_destroy(ArConfig* config);
	void ArSession_getConfig(ArSession* session, ArConfig* out_config);
	ArStatus ArSession_configure(ArSession* session, const ArConfig* config);
	void ArConfig_getAugmentedImageDatabase(const ArSession* session, const ArConfig* config,
		ArAugmentedImageDatabase* out_augmented_image_database);
	void ArConfig_setAugmentedImageDatabase(const ArSession* session, ArConfig* config,
		const ArAugmentedImageDatabase* augmented_image_database);
	void ArAugmentedImageDatabase_create(const ArSession* session, ArAugmentedImageDatabase** out_augmented_image_database);
	ArStatus ArAugmentedImageDatabase_deserialize(const ArSession* session, const uint8_t* database_raw_bytes,
		int64_t database_raw_bytes_size, ArAugmentedImageDatabase** out_augmented_image_database);
	void ArAugmentedImageDatabase_getNumImages(const ArSession* session,
		const ArAugmentedImageDatabase* augmented_image_database, int32_t* out_number_of_images);
	void ArAugmentedImageDatabase_destroy(ArAugmentedImageDatabase* augmented_image_database);
}

namespace
{
	/** `AR_SUCCESS`. */
	constexpr int32 kArCoreCfgSuccess = 0;

	/** 이 모듈로 Cloud Anchor 모드를 켰나 — 켠 적이 없으면 플러그인이 DB 를 버리지 않았다. */
	bool GArCoreCfgCloudModeOn = false;
	/** 지난 틱에 세션이 돌고 있었나 — 다시 Running 이 되는 순간(카메라 전환·재시작)을 잡는다. */
	bool GArCoreCfgWasRunning = false;
	/** 세션 재시작 뒤 남은 확인 횟수와 다음 확인 시각(월드초). */
	int32 GArCoreCfgChecksLeft = 0;
	double GArCoreCfgNextCheck = 0.0;

	ArSession* ArCoreCfgSessionHandle()
	{
		const TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSystem =
			(GEngine != nullptr && GEngine->XRSystem.IsValid()) ? GEngine->XRSystem->GetARCompositionComponent() : nullptr;
		return ARSystem.IsValid() ? static_cast<ArSession*>(ARSystem->GetARSessionRawPointer()) : nullptr;
	}

	/**
	 * 지금 ARCore 설정에 이미지 DB 가 없으면 현재 `UARSessionConfig` 원본으로 다시 넣는다. DB 말고는 바꾸지 않는다.
	 * @return 새로 넣었나(이미 있었거나 넣을 게 없으면 false).
	 */
	bool ArCoreCfgRestoreImageDatabase(const TCHAR* Why)
	{
		ArSession* Session = ArCoreCfgSessionHandle();
		const UARSessionConfig* SessionConfig = UARBlueprintLibrary::GetSessionConfig();
		if (Session == nullptr || SessionConfig == nullptr)
		{
			return false;
		}
		const TArray<uint8>& Serialized = SessionConfig->GetSerializedARCandidateImageDatabase();
		if (SessionConfig->GetCandidateImageList().Num() == 0 || Serialized.Num() == 0)
		{
			return false; // 전면 카메라 설정처럼 후보가 없는 세션 — 되돌릴 DB 가 없다.
		}

		ArConfig* Config = nullptr;
		ArConfig_create(Session, &Config);
		ArSession_getConfig(Session, Config);

		ArAugmentedImageDatabase* Current = nullptr;
		ArAugmentedImageDatabase_create(Session, &Current);
		ArConfig_getAugmentedImageDatabase(Session, Config, Current);
		int32_t CurrentImages = 0;
		ArAugmentedImageDatabase_getNumImages(Session, Current, &CurrentImages);
		ArAugmentedImageDatabase_destroy(Current);
		if (CurrentImages > 0)
		{
			ArConfig_destroy(Config);
			return false; // 이미 들어 있다 — 설정을 다시 걸지 않는다.
		}

		ArAugmentedImageDatabase* Database = nullptr;
		const int32 DeserializeStatus = static_cast<int32>(ArAugmentedImageDatabase_deserialize(
			Session, Serialized.GetData(), static_cast<int64_t>(Serialized.Num()), &Database));
		if (DeserializeStatus != kArCoreCfgSuccess || Database == nullptr)
		{
			ArConfig_destroy(Config);
			UE_LOG(LogNavArCoreConfig, Error,
				TEXT("[NavArCoreConfig] 마커 이미지 DB 복원 실패(%s) — deserialize status=%d · 마커 인식이 꺼진 채다"),
				Why, DeserializeStatus);
			return false;
		}
		int32_t Images = 0;
		ArAugmentedImageDatabase_getNumImages(Session, Database, &Images);
		ArConfig_setAugmentedImageDatabase(Session, Config, Database); // 설정이 복사본을 가진다
		ArAugmentedImageDatabase_destroy(Database);
		const int32 ConfigureStatus = static_cast<int32>(ArSession_configure(Session, Config));
		ArConfig_destroy(Config);

		if (ConfigureStatus != kArCoreCfgSuccess)
		{
			UE_LOG(LogNavArCoreConfig, Error,
				TEXT("[NavArCoreConfig] 마커 이미지 DB 복원 실패(%s) — configure status=%d · 마커 인식이 꺼진 채다"),
				Why, ConfigureStatus);
			return false;
		}
		UE_LOG(LogNavArCoreConfig, Log,
			TEXT("[NavArCoreConfig] Cloud Anchor 모드 유지 + 마커 이미지 DB %d장 복원(%s)"), Images, Why);
		return true;
	}
}

bool NavArCoreConfig::EnableCloudAnchorMode(const TCHAR* Caller)
{
	FGoogleARCoreServicesConfig Config;
	Config.ARPinCloudMode = EARPinCloudMode::Enabled;
	if (!UGoogleARCoreServicesFunctionLibrary::ConfigGoogleARCoreServices(Config))
	{
		return false;
	}
	GArCoreCfgCloudModeOn = true;
	GArCoreCfgWasRunning = true; // 지금 세션은 여기서 바로 되돌린다 — 재시작 감지는 다음 Running 부터
	if (AreMarkerTriggersEnabled())
	{
		ArCoreCfgRestoreImageDatabase(Caller);
	}
	return true;
}

void NavArCoreConfig::TickKeepMarkerImages(double NowSeconds)
{
	if (!GArCoreCfgCloudModeOn)
	{
		return;
	}
	const bool bRunning = UARBlueprintLibrary::GetARSessionStatus().Status == EARSessionStatus::Running;
	if (bRunning && !GArCoreCfgWasRunning)
	{
		// 세션이 다시 시작됐다 — 플러그인이 OnARSessionStarted 에서 Cloud Anchor 모드를 다시 걸며 DB 를 또 버렸다.
		// 바로 한 번, 1초 뒤 한 번 더 본다(같은 틱 안의 호출 순서에 기대지 않는다).
		GArCoreCfgChecksLeft = 2;
		GArCoreCfgNextCheck = NowSeconds;
	}
	GArCoreCfgWasRunning = bRunning;
	if (!bRunning || GArCoreCfgChecksLeft <= 0 || NowSeconds < GArCoreCfgNextCheck)
	{
		return;
	}
	--GArCoreCfgChecksLeft;
	GArCoreCfgNextCheck = NowSeconds + 1.0;
	if (AreMarkerTriggersEnabled())
	{
		ArCoreCfgRestoreImageDatabase(TEXT("AR 세션 재시작"));
	}
}

#else // !NAV_CLOUD_RESOLVE — Mac 에디터 등: Cloud Anchor 도 없고 되돌릴 DB 도 없다.

bool NavArCoreConfig::EnableCloudAnchorMode(const TCHAR* /*Caller*/) { return false; }
void NavArCoreConfig::TickKeepMarkerImages(double /*NowSeconds*/) {}

#endif
