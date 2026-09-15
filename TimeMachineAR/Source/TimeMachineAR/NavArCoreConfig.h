// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Cloud Anchor 모드와 **마커(증강 이미지) 인식을 같이** 켠다 — 엔진 플러그인 우회(2026-09-15).
 *
 * ## 왜 필요한가
 * UE 5.4 `GoogleARCoreServices` 의 `SetCloudARPinMode` 는 `ArConfig_create` 로 **빈 설정**을 만들어 조명·평면·업데이트
 * 모드만 옮기고 `ArSession_configure` 한다(엔진 GoogleARCoreCloudARPinManager.cpp). 그래서 리졸버·스포너가 Cloud Anchor
 * 모드를 켜는 순간 `DA_ARSession` 의 후보 이미지 DB 가 빠져 **마커 인식이 통째로 꺼진다**(13-1 결과 §5 · 13-3 결과 §10.12
 * 현장 확인). 플러그인은 AR 세션이 다시 시작될 때마다(카메라 전환 등 — `OnARSessionStarted`) 같은 일을 또 한다.
 * 12단계 이후 develop 빌드는 팀원 폰에서도 마커 스캔이 안 됐다.
 *
 * ## 무엇을 하나
 * 플러그인 호출 **뒤** 지금 설정(`ArSession_getConfig` — Cloud Anchor 모드·초점 FIXED 등 플러그인이 둔 그대로)에
 * **이미지 DB 만** 되돌려 다시 `ArSession_configure` 한다. 다른 값은 건드리지 않는다(현장에서 검증된 리졸브 조건 유지).
 * DB 는 플러그인이 세션을 시작할 때 쓰는 원본(`UARSessionConfig::GetSerializedARCandidateImageDatabase`)에서 매번 새로
 * 만든다 — 세션이 다시 만들어지면 옛 세션의 DB 핸들을 쓸 수 없고, 같은 원본이라 이미지 순서가 같아 플러그인의
 * `GetCandidateImageList()[index]` 대응도 그대로 맞는다. (NavLocalizer 의 런타임 후보 등록은 기본 꺼짐이라 따로 챙기지 않는다.)
 *
 * ## 기존 것을 고치지 않는다
 * 엔진·`DA_ARSession`·`ARTrackingManager`·Build.cs 무수정. ARCore C 함수는 리졸버의 `ArFuture_cancel` 처럼 엔진 동봉
 * `arcore_c_api.h` 와 **같은 시그니처**로 직접 선언한다(헤더 경로를 열려면 Build.cs 를 고쳐야 한다 — 공지 트리거 5).
 *
 * ## 끄기
 * 스포너 섹션 `[/Script/TimeMachineAR.NavCloudAssetSpawner] bMarkerTriggers=False` — 마커 트리거와 이 복원을 같이 끈다
 * (13-3 현장 빌드와 같은 상태 · 이미지 인식 부하 0). 키가 없으면 켠다.
 *
 * Android(`NAV_CLOUD_RESOLVE`) 밖에서는 아무것도 하지 않는다.
 */
namespace NavArCoreConfig
{
	/**
	 * `ConfigGoogleARCoreServices(ARPinCloudMode=Enabled)` 를 부르고, 플러그인이 버린 마커 이미지 DB 를 되돌린다.
	 * @param Caller 로그 머리말(누가 켰나).
	 * @return 플러그인 결과 — Cloud Anchor 모드가 켜졌나. DB 복원 실패는 로그만 남기고 결과에 섞지 않는다.
	 */
	TIMEMACHINEAR_API bool EnableCloudAnchorMode(const TCHAR* Caller);

	/**
	 * 매 틱(세션이 안 돌 때도) 부른다. AR 세션이 **다시** Running 이 되면 플러그인이 또 DB 를 버리므로 확인해 되돌린다.
	 * 이 모듈로 Cloud Anchor 모드를 켠 적이 없으면 아무것도 하지 않는다.
	 */
	TIMEMACHINEAR_API void TickKeepMarkerImages(double NowSeconds);

	/** 마커 트리거·이미지 DB 복원을 쓰나 — 스포너 섹션 `bMarkerTriggers`(키 없음 = 켬). */
	TIMEMACHINEAR_API bool AreMarkerTriggersEnabled();
}
