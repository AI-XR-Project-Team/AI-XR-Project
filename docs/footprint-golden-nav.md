# 황금빛 공룡 발자국 AR 길안내 — 구현 기록

작성: 2026-09-14. 브랜치 `feature/golden-footprint-nav` (기준 `fix/archelon-info-ui-and-docent` `0b2c868`).
지시서: `docs/footprint-opus-handoff/CLAUDE_OPUS_발자국_구현지시서.md`. 실행 모델: Claude Opus 5 (claude-opus-5).
커밋/푸시/PR 은 하지 않았다(지시서 범위 밖).

## 1. 무엇이 바뀌었나

| 영역 | 파일 | 내용 |
|---|---|---|
| 에셋 추출 | `TimeMachineAR/Scripts/extract_nav_footprints.py` | 시트(02)에서 왼발/오른발 M, 링, 셰브론, 글로우를 단일 PNG 로 분리. 원본은 `docs/ai-handoff/inbox/nav_footprints/src/` 에 보존. `crop_manifest.json` + `verify/`(밝은 회색·어두운 회색·체크보드·알파) 생성. |
| UE 임포트 | `TimeMachineAR/Scripts/import_nav_footprints.py` | `/Game/UI/Nav/Floor/Golden/` 텍스처 5장 + `M_NavFootprintGolden` 머티리얼(그래프를 코드로 생성) + `NavFloorGoldenCookLabel`. |
| 에셋 검사 | `TimeMachineAR/Scripts/inspect_nav_floor_assets.py` | .uasset 의 실제 블렌드/셰이딩/파라미터/텍스처 설정 덤프(주석이 아니라 에셋으로 확인). |
| 순수 배치 | `NavFloorGuide.h/.cpp` | `FNavFloorPlacement.StepIndex`(경로 시작 기준 k), `LateralScale`(급회전 0), yaw 를 앞뒤 ½Spacing 현(chord)으로 → 코너에서 부드럽게. `OffsetForStep`(월드 접선 기준 좌/우 오프셋). |
| 액터 | `NavFloorGuideActor.h/.cpp` | 좌/우/링 MID 3개(풀 생성 시 1회), `StepIndex` 짝/홀로 텍스처·오프셋, 도착 링+대표 발자국(`FNavFloorArrival` 로 소유자가 명시 전달), 표시 범위 밖 목적지엔 링 안 그림, 에셋 누락 시 숨김+경고 1회, `UpdateGuidePreview`(측위 없이 항등 변환, 테스트용), `GetGuideHeadWorld`(HUD 셰브론 위치). |
| 목적지 규칙 | `NavDestinations.h/.cpp` | `FloorStyle(NodeType, Label)`: exhibit → 황금 좌/우 쌍 + 교대, facility/entrance → 기존 화살표 한 장(오프셋 없음). `FloorTextureObjectPath` 와 기존 공룡별 매핑은 그대로. `ForwardChevronObjectPath`. |
| 도착 판정 | `NavRouteProgress.h/.cpp` | `ArriveExitHysteresisCm=40`: 도착이 서면 80+40cm 를 넘어야 풀린다(문턱 떨림 방지). 다른 소비자(GuideLog, BP)도 같이 안정. |
| 소유자 | `NavMinimapWidget.cpp` `RefreshArGuides` | 액터에 `bArrived`·목적지 좌표 전달, 액터의 가이드 헤드를 DestMarker 에 전달. Follow/Hide/Destroy 수명주기는 그대로. |
| HUD | `NavDestMarkerWidget.h/.cpp` | 가장 먼 보이는 발자국 위(60cm)에 전방 셰브론(`T_NavForwardChevron`) + 남은 거리(`RemainingCm/100`). 도착 시 마름모 위 pill 이 "도착". 카메라 뒤/화면 밖은 안 그림, HitTestInvisible 유지. |
| 테스트 | `NavFloorGuideTest.cpp`, `NavRouteProgressTest.cpp`, `Tests/NavFloorGuidePreviewTest.cpp` | 코너 현/오프셋 축소, k 기반 교대 안정성, 네 방향, 짧은 경로, 중복점, 빈 경로, 월드 오프셋 부호, 스타일 분기, 도착 히스테리시스. 프리뷰: 에디터 월드에 액터를 띄워 SceneCapture 로 PNG 5장. |
| 빌드 | `TimeMachineAR.Build.cs` | 에디터 빌드에만 `RenderCore`, `RHI` 추가(프리뷰 테스트용). |

## 2. 에셋 검수에서 발견한 것

- 시트는 진짜 알파(RGBA)다. 검정을 투명 처리하는 색상 키는 쓰지 않았다.
- **저알파 픽셀의 RGB 가 순수 빨강/노랑(255,0,0 / 255,255,0)** 이다(언프리멀티플라이 내보내기 잔재). 뷰어에선 안 보이지만 ASTC + 밉 생성 시 밝은 바닥에서 붉은 테두리로 번진다 → alpha<64 구간의 RGB 를 글로우 밴드 평균색(약 217,171,112)으로 교체했다.
- 이웃 발자국 글로우가 36~55px 간격에서 서로 겹친다 → 발 코어(alpha>128 최대 컴포넌트)에서 거리 18px 까지 유지, 28px 에서 0 으로 감쇠. 자기 글로우(18px 에서 alpha≈4)는 거의 안 잘린다.
- 오른발 M 아래 18px 에 "M" 라벨 → 어두운/불투명 조각 제거 + 3px 팽창(잔여 안티에일리어싱 선까지).
- 왼발/오른발은 실제로 다른 그림(세부 질감 차이 확인). UV 반전 폴백을 쓰지 않았다.
- 도착 링·글로우 풀은 시트에 **원근 타원**으로 그려져 있고 라벨이 6~7px 아래 붙어 있다 → 가로 반지름 프로파일(라벨 없는 방향)을 샘플해 정원(圓)으로 재생성. 링 안쪽 채움 alpha 는 0.5배(실제 바닥이 보이게). 섹션 04 전체가 하나의 헤이즈를 공유해 링 꼬리 alpha 가 39 에서 멈추므로 반지름 98→111px 에서 0 으로 페이드.
- 섹션 03(바닥 원형 사진), 02(경로 묶음), 05 먼지/그림자는 런타임 소스로 쓰지 않았다.
- 텍스처 설정: 발자국/링/글로우 = World 그룹, TC_Default(ASTC RGBA), sRGB, 밉 사용, 스트리밍(기존 Floor 텍스처와 동일). 셰브론 = UI 그룹, 밉 없음(HUD 전용).

## 3. 사용 파라미터 (기본값, `[/Script/TimeMachineAR.NavFloorGuideActor]` ini 로 조정)

| 값 | 기본 | 비고 |
|---|---|---|
| FloorGuideRangeCm / SpacingCm | 600 / 100 | 기존 유지 |
| FloorPlaneSizeCm | 60 | 글로우 여백 포함. 실제 발 길이 = 70% ≈ 42cm. 시안(Image 1)은 더 커 보여 현장에서 80 까지 올려 볼 것 |
| FloorZOffsetCm | 1 | 기존 유지 |
| FloorYawOffsetDeg | 90 | 프리뷰(`Dirs.png`)로 +X/+Y/−X/−Y 네 방향 발가락 방향 확인. 새 텍스처도 발가락이 row 0 |
| FloorLateralOffsetCm | 10 | 급회전(30°~75°)에서 1→0 |
| ArrivalRingSizeCm / ArrivalFootSizeCm | 120 / 70 | 링 지름 ≈ 110cm |
| GuideHeadHeightCm | 60 | HUD 셰브론 투영 높이 |
| FloorBreathAmplitude / PeriodSec | 0.12 / 1.8 | 약한 호흡. 0 이면 고정 밝기 |
| NavRouteProgress.ArriveExitHysteresisCm | 40 | |
| NavDestMarkerWidget.ChevronSizePx | 96 | |

렌더러: `DefaultEngine.ini` bBuildForES31=True, bSupportsVulkan=False, r.MobileHDR=True → **OpenGL ES 3.1**. 바꾸지 않았다.

## 4. 검증 로그

- 에디터 C++ 빌드: `Build.bat TimeMachineAREditor Win64 Development` 성공.
- 헤드리스(-nullrhi): `TimeMachineAR.Nav.FloorGuide / RouteProgress / Guidance / Destinations / LocalizerAnchor` 전부 Success. (`FloorGuidePreview` 는 nullrhi 에서 경고 후 건너뜀.)
- 오프스크린(-RenderOffscreen): `TimeMachineAR.Nav.FloorGuidePreview` Success → `docs/screenshots/footprint/NavFloorGuidePreview_{Top,Walk,Dirs,Arrival,Dark}.png`.
  Top: ㄱ자 경로에서 코너 발이 45° 현 방향, 좌우 교대. Dirs: 네 방향 모두 발가락이 진행 방향. Walk: 밝은 바닥에서 어두운 발 질감 + amber 가장자리 구분됨. Arrival: 링 + 대표 발자국.
  (프리뷰 바닥은 SceneCapture 노출 때문에 실제보다 밝다 — 알파/방향 검증용이지 색 검증용이 아니다.)
- 기존 렌더 회귀: `Nav.SkinPreview, Docent.SkinPreview, Docent.ScanPreview, DinoCard.SkinPreview, DinoCard.LiveSkinSwitch` 전부 Success.
- Android: `RunUAT BuildCookRun -platform=Android -cookflavor=ASTC -clientconfig=Shipping` 성공.
  APK: `TimeMachineAR/Build/GoldenFootprintAndroid/Android_ASTC/TimeMachineAR-Android-Shipping-arm64.apk` (391MB).
  IoStore 컨테이너 `Saved/StagedBuilds/Android_ASTC/TimeMachineAR/Content/Paks/TimeMachineAR-Android_ASTC.utoc` 를 `UnrealPak -List` 로 확인 → `UI/Nav/Floor/Golden/` 의 T_Footprint_Left/Right(.uasset+.ubulk), T_NavArrivalRing, T_NavForwardChevron, T_Footprint_Glow, M_NavFootprintGolden 전부 포함. (.pak 에는 ini/슬레이트만 있고 에셋은 .ucas 에 있다 — pak 만 보면 누락으로 오판한다.)
- 실기기(SM-S936N, R3CY8009NKF): `adb install -r` 성공, 앱 기동 확인(`docs/screenshots/footprint/phone_launch.png`, 측위 대기 화면).

## 5. 실기기에서 아직 확인하지 않은 것 (완료 주장 범위 밖)

QR 마커 측위와 실제 보행이 필요해 이 세션에서 하지 못했다.
- 직진 / 좌회전 / 우회전 / 도착 / 취소 / 측위 상실 시나리오의 실제 화면.
- 발이 바닥에 떠/잠기는지(FloorZOffsetCm), 실제 바닥에서의 크기감(FloorPlaneSizeCm), 글로우 밝기.
- ASTC 알파 품질(붉은 테두리가 정말 사라졌는지)은 실기기에서 밝은 바닥 캡처로 봐야 한다.
- 변경 전/후 프레임 시간·투명 오버드로: **측정하지 않았다.** 발자국 수는 창당 최대 6개 + 도착 시 2개(링·발), 각 60cm Plane 1장(글로우 별도 레이어 없음)이라 이전 구현과 드로우 수는 같다.
- Shipping 빌드라 logcat 에 LogNav 경고가 안 찍힌다. 에셋 누락 경고를 기기에서 보려면 Development 빌드가 필요하다.

## 6. 남은 차이 목록

- 시안은 공룡 한 종(세 발가락)만 준다 → 전시물 4종 모두 같은 황금 발자국. 공룡별 황금 쌍이 생기면 `FNavDestinations::FloorStyle` 한 곳만 갈라 주면 된다(기존 `FloorTextureObjectPath` 매핑은 보존).
- 시설/입구는 기존 화살표 텍스처(`T_Floor_Arrow_Facility`)를 새 머티리얼로 그린다(스타일 그대로, 교대 없음).
- 예상 시간("약 1분")은 기존 값이 없어 **표시하지 않았다**(가짜 상수 금지). 보행 속도 추정치를 넣으려면 DestMarker 에 한 줄이면 된다.
- "지금 AR로 관람하기" 버튼: 기존 HUD 에 해당 버튼이 없고(도착 시 GuideLog 문구 "…발자국을 인식시켜주세요" + 사용자가 마커를 스캔), 새 UI 추가는 지시서 범위 밖이라 만들지 않았다. 전환 함수는 `UDocentChatWidget::HandleReferenceRescan()`(NavPanel 접고 ScanPanel + StartScan)이 있으므로 BP/WBP 에 버튼만 놓아 잇으면 된다.
- 먼지 파티클, 글로우 풀 2차 레이어(T_Footprint_Glow 는 임포트만 함), 발별 호흡 위상: 미적용.
- 목적지 마름모(기존 216px)는 그대로 둔다 — 시안엔 없지만 "기존 UI 재사용" 원칙.

## 7. 재현 절차

```
python TimeMachineAR/Scripts/extract_nav_footprints.py
UnrealEditor-Cmd.exe TimeMachineAR.uproject -run=pythonscript -script=TimeMachineAR/Scripts/import_nav_footprints.py -unattended -nullrhi
Build.bat TimeMachineAREditor Win64 Development -Project=...
UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests TimeMachineAR.Nav; Quit" -unattended -nullrhi
UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests TimeMachineAR.Nav.FloorGuidePreview; Quit" -unattended -RenderOffscreen
RunUAT.bat BuildCookRun -project=... -platform=Android -cookflavor=ASTC -clientconfig=Shipping -build -cook -stage -pak -package -archive -archivedirectory=...
UnrealPak.exe <StagedBuilds>/.../TimeMachineAR-Android_ASTC.utoc -List | findstr Golden
```
