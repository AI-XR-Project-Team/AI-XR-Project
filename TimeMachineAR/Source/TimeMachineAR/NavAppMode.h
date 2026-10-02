// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 앱 표시 모드 — **사용자모드**(기본) · **개발모드**.
 *
 * 13-3 현장 검증 때 켜 둔 진단 표시를 스위치 하나로 묶는다. 개발모드에서만 보이는 것:
 *  - 앵커 토스트 — `N번 앵커로 측위 (1.8초)` · 재보정 · 기준 전환 · 에셋 스캔 진단(`UNavCloudResolveHud::ShowMessage` 전부)
 *  - AR 지도 겹쳐보기 — 벽·전시섬·앵커·인식 잔차·노드·엣지(`UNavMapArOverlaySubsystem` — 그 섹션 bEnabled 도 켜져 있어야 한다)
 *  - 전체 지도 그래프 디버그(`UNavMinimapWidget::bDrawGraphDebug`)
 * 사용자모드에도 남는 것: "위치를 다시 잡았습니다"(번호 없이) · 재측위 화면 · 안내 로그 · 미니맵.
 * logcat 로그는 모드와 무관하다 — 겹쳐보기의 RESID·POS 줄만 겹쳐보기와 함께 꺼진다.
 *
 * ini(새 섹션 — 공지 트리거 4 해당 없음):
 *   [/Script/TimeMachineAR.NavAppMode]
 *   bDevMode=True        ; 없거나 False 면 사용자모드
 */
namespace NavAppMode
{
	/** ini `bDevMode` 를 읽는다. 키가 없으면 false = 사용자모드. */
	TIMEMACHINEAR_API bool IsDevMode();
}
