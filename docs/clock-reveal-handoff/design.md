# 회중시계 투척 → 시간의 문 → 아르켈론 등장 — 확정 설계 (design.md)

작성: 2026-09-14, Opus(claude-opus-5). 기준: `develop` `0fba913` + 이 문서와 함께 추가된 감사 스크립트/테스트.
지침서: `IMPLEMENTATION_GUIDE_KO.md`. 진행 기록: `handoff.md`.

이 문서의 수치는 **실제 에셋을 열어 잰 값**이다(§1). 추정값은 "(초기값, 검수 후 조정)"으로 표시했다.

---

## 1. 조사 결과 (에디터 커맨드릿·자동화 테스트로 확인)

### 1.1 시계 FBX (Scripts/audit_clock_fbx.py + Tests/ClockAuditTest.cpp → Saved/ClockAudit_*.png)

| 부품 | 파일 | 삼각형 | 바운즈(임포트 cm, 중심≈0) | 확인된 형상 |
|---|---|---|---|---|
| 본체 | ..._body_... .fbx (103.7MB) | **1,993,019** | 139.8 × 80.9 × **190.3** | 헌터 케이스. **문자판은 +Y 를 향한 앞 케이스(Y +20~+40) 의 +Y 면(Y≈37.5)**. 뒤(Y −45~−25)는 180° 젖혀진 조각 뚜껑. 보우/크라운 +Z. **문자판에 바늘 없음**(중앙 점만) → 중복 바늘 문제 없음. |
| 시침(hour_han) | (101.1MB) | 1,928,112 | 43.3 × 10.5 × 190.3 | 넓은 브레게 스타일. 평면 = XZ(두께 Y). **끝 +Z, 허브 고리 −Z 끝**. 허브 중심 (−2.7, 0, **−79.9**), 고리 폭 37. |
| 분침(minute_h) | (66.8MB) | 1,074,001 | 27.4 × 7.4 × 190.3 | 가늘고 김. 평면 XZ. 끝 +Z, 허브 하트형 고리 −Z 끝, 허브 중심 (0, 0, **−79.8**). |

- Meshy 가 세 부품을 **각각 190 cm 높이로 정규화**했다 → 바늘 길이가 본체와 같다. 실제 비율은 우리가 정한다(§3).
- 문자판 중심(본체 메시 좌표): **(X +0.6, Y 37.5, Z −25.5)** — +Y 직교 렌더에서 중앙 점을 찾은 값. 숫자 링 반지름 ≈ 35~48, 분 트랙 ≈ 48, 베젤 바깥 ≈ 66, 본체 반지름 ≈ 69.
- 텍스처: Base/Normal 4096², Metallic/Roughness 2048². FBX 에 metallic/roughness 가 임베디드(임포트 시 `.fbm` 추출됨 → 삭제, 원본 폴더 그대로). 첫 감사 때 임포트 `uvs=0` 은 API 오용이며 실제로는 UV 가 있다(베이스컬러가 렌더됨).
- 감사용 자산 `/Game/TimeReveal/Audit/*`(원본 그대로 2M tris, 86MB×3)은 **패키징 금지·커밋 금지**(gitignore 대상으로 둔다).

### 1.2 Niagara 원본 (Tests/NiagaraAuditTest.cpp → NIAGARA_AUDIT 로그)

| 시스템 | emitter | Sim | 공간 | Bounds | SpawnRate | Lifetime | 렌더러/재질 | 색 |
|---|---|---|---|---|---|---|---|---|
| NS_ActiveAtom | Ring, Ring001, Ring002 | **GPU** | World | 고정 ±1000 | **5000/s** ×3 | 4~8 s | Sprite, `/Niagara/DefaultAssets/DefaultSpriteMaterial` | 흰색(1,1,1) + ColorFromCurve |
| | Singularity_Point_ | GPU | World | | 1000/s | 4~8 s | Sprite, Default | 흰색 |
| NS_Worm-Hole | Worm-Hole_Up / _Down | **GPU** | World | 고정 ±1000 | 1000/s ×2 + Grid 10×10×10 burst | 8~10 s | Sprite, Default | 흰색 |

- **User 파라미터 없음(exposed=[])**. 색은 모듈 상수·커브. 모든 emitter 가 GPU 시뮬이고 파티클 수가 수만 개(Ring 3개 ≈ 90k). 원본은 Android 용도가 아니다.
- 모듈: TorusLocation/SphereLocation/Vortex/Spring/Drag/ScaleColor(ActiveAtom), GridLocation/CurlNoise(TilingCurl32 벡터필드)/PointAttraction/Vortex/Wind(Worm-Hole). **depth collision·GPU 전용 DI 없음** → CPU 전환 가능한 구성. 공유 재질 의존 없음(엔진 기본 스프라이트 재질만) → 복제해도 원본 팩과 얽히지 않는다.
- 원본 GPU 시뮬은 에디터 오프스크린 캡처에서 **진행되지 않는다**(틱 필요) → 외형 검수는 CPU 파생본으로 한다.
- `Content/FreeNiagaraPack/` 은 **git 미추적**(사용자가 복사만 함, 3 파일). 파생본만 커밋하고 원본 팩은 커밋 여부를 사용자에게 묻는다.

### 1.3 코드/BP 경로

- 게임모드 `BP_TestGameMode`: Pawn=**BP_ARPawn**(순수 Pawn+Camera, 로직 없음), PC=`ATimeMachineARPlayerController`. **`ATimeMachineARDebugPawn::CheckGaze` 는 AR 맵에서 돌지 않는다**(BP_DebugPawn 미사용). 그래도 StartReveal 은 공개 API 라 잠금을 둔다.
- `BP_ARManager`(AR_MainMap 배치, ARTrackingManager 자식): OverlayActorClass=**BP_DinoOverlay_T-Rex**(모든 종 공통), DinoRegistry=DA_DinoRegistry, Session=DA_ARSession, ignoreUnknown=True. `BP_DinoOverlay`(/Game/Stuff)는 T-Rex 로의 리다이렉터.
- Registry: `EX5-ARCHELON → DA_Dino_Archelon`(exhibit_key archelon_full_skeleton, flesh=/Game/Stuff/Asset/Archelon/Archelon, bone=None, MeshTransform T(290,55,8) R(−0.03,−0.03,0.706,0.706) S1.1). 세션 후보 이미지에 EX5-ARCHELON(폭 14.6cm) 있음.
- 아르켈론 메시: **1,968,055 tris, LOD 1개**, 재질 `Archelon_Material` **Opaque/DefaultLit, `Alpha` 파라미터 없음** → 현재 `StartReveal` 페이드는 아르켈론에 아무 효과가 없고 인식 즉시 모델이 보인다. (2M tris 는 이 작업 범위 밖이지만 성능 측정 시 지배 항목이다.)
- `BP_DinoOverlay_Archelon`(팀원 caustics 작업)은 **런타임에 스폰되지 않는다**(ARManager 가 T-Rex BP 를 쓰고 Registry 로 메시만 바꿈). 이 설계는 그 BP 를 건드리지 않는다.
- `BP_DinoOverlay_T-Rex` BeginPlay: `GetAllWidgetsOfClass(WBP_DinoInfoCard)` → `OnDinoClicked` 바인딩. 등장 연출과 무관, 유지.
- `DocentChatWidget::HandleMarkerFound`: 다음 틱 ScanPanel(SelfHitTestInvisible) 복원 + Btn_CloseAR 표시. `HandleReferenceRescan()` = AR 스캔 재시작(ClearOverlay 경로). 둘 다 유지.
- 기존 카메라 블러: `UDinoInfoCardWidget` 이 **UMG `UBackgroundBlur`** 로 3D 뷰포트 전체를 흐린다(실기기 검증됨). 포털 피크 블러는 이 방식을 재사용한다(§5).
- Android: ES3.1, Vulkan 없음, MobileHDR On. 렌더러는 바꾸지 않는다.

---

## 2. 구조·소유권

```
UDinoInfoData
  + TObjectPtr<UTimeRevealProfile> TimeRevealProfile   // null = 기능 없음(기존 동작). 아르켈론 DA 에만 지정.

AARTrackingManager::CheckForTrackedImages()
  FinishSpawning 직후: if (Species && Species->TimeRevealProfile)
      UTimeRevealComponent::AttachTo(SpawnedOverlay, Species->TimeRevealProfile)   // 첫 렌더 전 메시 숨김

ADinoOverlayActor
  + bool bRevealLocked (기본 false) : true 면 StartReveal/HandleTapped 무시. 컴포넌트가 Ready~Revealing 동안 true.
  (그 외 변경 없음. BP 도 변경 없음.)

UTimeRevealComponent  (ActorComponent, 오버레이 액터에 붙음 — 단일 소유자)
  상태: Idle → Ready → Throwing → Impact → Portal → Revealing → Complete | Cancelled
  소유: ATimeWatchActor 스폰/파괴, Niagara 컴포넌트 2개, 블러 위젯, 터치 입력, 메시 가시성 저장/복구, 오버레이 잠금.
  오버레이 액터가 파괴되면(ClearOverlay/카메라 전환) 컴포넌트도 사라진다 → EndPlay 에서 전부 정리.

ATimeWatchActor  (시각 전용, 상태 없음)
  Root(SceneComponent = 문자판 중심, 문자판 법선 = 액터 +X)
   ├ BodyMesh (SM_Watch_Body)
   ├ HourPivot (SceneComponent) ─ HourMesh (SM_Watch_HandHour)
   └ MinutePivot (SceneComponent) ─ MinuteMesh (SM_Watch_HandMinute)
  API: SetHandSpeeds(HourDegPerSec, MinuteDegPerSec), SetBodySpin(DegPerSec, Axis), SetGlow(0..1), SetHandsAngle(h,m)

UTimeRevealProfile (PrimaryDataAsset)  — 에셋·시간·배치·커브. 아르켈론용 DA_TimeReveal_Archelon 1개.
```

기능 플래그: `TimeRevealProfile == null` 또는 `Profile->bEnabled == false` → 컴포넌트를 붙이지 않는다 = 기존 동작 그대로.

---

## 3. 시계 조립 계약 (Sonnet 작업 1)

에셋 경로 `/Game/TimeReveal/Clock/` : `SM_Watch_Body`, `SM_Watch_HandHour`, `SM_Watch_HandMinute`, `T_Watch_*`, `M_Watch_Body`, `M_Watch_Hand`, `TimeRevealClockCookLabel`.
원본 FBX/PNG 는 `TimeMachineAR/Asset/Clock/` 그대로. 파생 PNG 는 `docs/ai-handoff/inbox/clock/`.

### 3.1 임포트 (Scripts/import_time_watch.py, 재실행 가능)
- FBX 옵션: 재질/텍스처 임포트 안 함, CombineMeshes, Nanite 끔, 라이트맵 UV 생성 끔, 콜리전 자동생성 끔(시계는 콜리전 없음).
- **LOD0 자체를 축소**: `EditorStaticMeshLibrary.set_lods` 로 LOD0 percent = 본체 0.4%(≈8,000 tris), 시침 0.08%(≈1,500), 분침 0.14%(≈1,500). 축소 후 렌더 검수에서 보우/숫자판 실루엣이 깨지면 본체 0.6%(12k)까지 허용. 전체 목표 ≤ 12k~15k tris. **실제 결과 tris 를 보고**.
- 텍스처는 원본 4K 를 임포트하지 않는다. `Scripts/derive_clock_textures.py`(CPython/PIL)로 파생: 본체 BaseColor 2048·Normal 2048·MR 1024, 바늘 BaseColor 1024·Normal 1024·MR 512. MR = R:metallic, G:roughness 패킹(선형). BaseColor sRGB, Normal `TC_Normalmap`(녹색 반전은 렌더 검수 후 결정 — 기본 반전 없음), MR `TC_Masks`/sRGB off.
- 재질 `M_Watch_Body`/`M_Watch_Hand`: DefaultLit, BaseColor·Normal·Metallic(MR.R)·Roughness(MR.G), **`GoldGlow`(Scalar, 기본 0) → Emissive = BaseColor × GoldGlow**(어두운 배경에서 금속이 죽을 때 시계에만 제한적으로 올리는 보조 표현. 전체 Emissive 대체 아님), `Tint`(Vector, 기본 흰색). Python `MaterialEditingLibrary` 로 그래프를 코드로 만든다(재현 가능).
- Cook: `DirectoriesToAlwaysCook=/Game/UI` 는 `/Game/TimeReveal` 을 덮지 않는다 → **프로파일 DataAsset 의 하드 참조 + `TimeRevealClockCookLabel`(PrimaryAssetLabel, AlwaysCook)**. 최종 검증은 스테이징 `.utoc` 목록(`UnrealPak <utoc> -List | grep TimeReveal`).

### 3.2 조립 수치 (초기값, 렌더 검수 후 조정)
메시 로컬 = 임포트 좌표(§1.1) 그대로(재중심화하지 않는다 — 감사 수치가 그대로 유효하게).

| 컴포넌트 | 상대 회전 | 상대 위치 | 스케일 | 근거 |
|---|---|---|---|---|
| BodyMesh | Yaw −90° (문자판 법선 +Y → 액터 +X) | (−37.5, +0.6, +25.5) | 1 | 문자판 중심 (0.6, 37.5, −25.5) 를 원점으로. Yaw −90 은 (x,y)→(y,−x). |
| HourPivot | Roll = 시침 각(0 = 12시) | (+1.4, 0, 0) | 1 | 문자판 위 여유 1.4 (문자판 y=37.5 → 액터 x=0, 바늘 두께 반 0.9 + 0.5) |
| HourMesh | Yaw −90° | (0, −2.7×s, +79.9×s) ← 허브 (−2.7,0,−79.9) 를 피벗 원점으로 | s=0.17 | 끝이 0.17×175 ≈ 30 (숫자 링 안쪽) |
| MinutePivot | Roll = 분침 각 | (+3.5, 0, 0) | 1 | 시침 위 1.8+0.3 |
| MinuteMesh | Yaw −90° | (0, 0, +79.8×s) | s=0.25 | 끝 ≈ 44 (분 트랙 48 안쪽) |

- 바늘 회전축 = **피벗의 로컬 X(문자판 법선)**, World Z 를 쓰지 않는다. 회전 방향(시계방향 = 앞에서 봤을 때)은 조립 렌더(`Tests/…WatchAssembly`)로 확정하고 부호를 코드 주석에 기록.
- 깊이 분리: 시침 x 1.4, 분침 x 3.5 → 관통·z-fighting 없음을 측면 렌더로 확인. 필요하면 0.5 단위로 조정.
- **합격 기준**: 정면(+X 에서 −X 로 본) 렌더에서 두 바늘의 허브가 문자판 중앙 점 위에 겹치고, 0°/90°/180°/270° 프레임에서 허브가 움직이지 않으며(공전 없음), 측면 렌더에서 바늘이 문자판을 뚫지 않는다. 뒷면 렌더에서 뚜껑 조각이 온전하다. 삼각형·텍스처·재질 수 보고.
- 검수 테스트: `TimeMachineAR.Reveal.WatchAssembly`(새로 작성) — ATimeWatchActor 를 에디터 월드에 놓고 정면/측면/뒷면/3-4 + 바늘 4각도 캡처 → `Saved/WatchAssembly_*.png`. 기존 `TimeMachineAR.Reveal.ClockAudit` 는 `-ClockAuditPrefix=/Game/TimeReveal/Clock/SM_Watch_` 로 파생 메시 6방향 검수에 재사용.

### 3.3 AR 크기
프로파일 `WatchScale = 0.07`(초기값): 베젤 지름 ≈ 2×66×0.07 ≈ 9.2 cm, 카메라 50 cm 앞에서 화면 폭의 ≈ 22%(세로 화면 수평 FOV ≈ 45° 가정). 실기기에서 18~24% 를 확인하고 조정.

---

## 4. 상태·입력·궤적 계약 (Sonnet 작업 2)

### 4.1 UTimeRevealProfile (DataAsset, `/Game/TimeReveal/DA_TimeReveal_Archelon`)
```
bEnabled = true
// 에셋 (하드 참조 → cook 포함)
TSubclassOf<ATimeWatchActor> WatchClass          // 기본 ATimeWatchActor (C++), BP 자식 허용
UNiagaraSystem* OrbitFX, * PortalFX              // /Game/TimeReveal/FX/NS_WatchOrbit, NS_TimePortal (Opus 가 만든다; null 이면 FX 없이 진행)
UMaterialInterface* RevealMaterial               // M_ArchelonReveal (Masked 디졸브; null 이면 가시성 토글로 등장)
// 배치
float WatchScale = 0.07
FVector2D ReadyScreenAnchor = (0.5, 0.70)        // 뷰포트 비율. 하단 바 위 빈 카메라 영역
float ReadyDistanceCm = 50
float ReadyFadeInSec = 0.3, ReadyBobAmpCm = 0.6, ReadyBobPeriodSec = 2.4
// 입력
float SwipeMinNormalized = 0.12  // 뷰포트 높이 대비 세로 이동
float SwipeMaxSec = 0.6, SwipeMaxSideRatio = 1.2, WatchHitPaddingPx = 40
// 투척
float ThrowSec = 0.7, ThrowArcHeightCm = 25, ThrowStopBeforeTargetCm = 30
UCurveFloat* ThrowEase (null = EaseOutQuad)
float MissReturnSec = 0.35
// 타임라인(명중 t=0, 초). 지침서 §5 값.
float FlashEnd = 0.18
float OrbitStart = 0.12, OrbitEnd = 1.10
float PortalStart = 0.65, PortalFullSize = 2.00
float BlurPeakStart = 1.30, BlurPeakEnd = 1.85, BlurMaxStrength = 6
float RevealStart = 1.60, RevealEnd = 2.70
float FadeOutStart = 2.70, FadeOutEnd = 3.50
UCurveFloat* PortalScaleCurve, * RevealCurve, * BlurCurve (null = smoothstep)
// 회전
float BodySpinDegPerSec = 540, HourHandDegPerSec = 1440, MinuteHandDegPerSec = 2400  (바늘 > 본체, 서로 다르게)
// 안전
float TrackingLostGraceSec = 2.0   // 이보다 짧은 끊김: 마지막 pose 유지·진행 정지, 길면 취소→폴백
float PortalSizeCm = 0(자동: 모델 bounds 지름의 0.8, 최대 320)
```

### 4.2 상태 전이 (UTimeRevealComponent)
```
Idle      : AttachTo() 직후. 메시 가시성/콜리전/bClickable 저장 → Flesh/Bone 숨김+콜리전 끔, Overlay->bRevealLocked=true.
            에셋 비동기 로드(StreamableManager: Watch mesh/재질, FX, RevealMaterial). 로드 완료 → Ready.
Ready     : 시계 스폰(카메라 기준 위치, 0.3s 페이드/스케일 인, 미세 부유), 입력 활성(EnableInput(PC) + BindTouch).
            매 틱 카메라 기준 위치 갱신. 타깃(오버레이 bounds 중심)이 화면 밖/추적 아님 → 스와이프 무시(시계는 계속 표시).
Throwing  : 유효 스와이프. 시계를 world 로 전환(detach), 시작점=현재 world, 끝점=타깃−카메라방향×StopBefore.
            P(s)=Lerp(A,B,e(s)) + Up×Arc×4s(1−s), s=t/ThrowSec. 회전: 던진 방향으로 천천히. 끝 → Impact.
            (빗나감은 만들지 않는다: 유효 스와이프면 항상 명중. 타깃 무효면 스와이프 자체를 거부.)
Impact    : t=0. 청백 섬광(작은 스프라이트/라이트 아님 — Orbit FX 의 첫 burst 또는 시계 GoldGlow 스파이크 0.18s),
            본체 spin·바늘 회전 가속(SetHandSpeeds). OrbitFX 를 시계에 부착 스폰(로컬 공간, 스케일 = WatchScale 기준).
Portal    : t≥PortalStart. PortalFX 를 타깃 위치에 스폰, **평면 = 스폰 순간의 카메라 방향(yaw만), 이후 고정**.
            스케일 0.1→1.0 (PortalScaleCurve, PortalStart~PortalFullSize). 블러 위젯 강도 0→Max→0 (BlurPeak 구간).
Revealing : t≥RevealStart. Flesh 메시: RevealMaterial 이 있으면 MID 로 교체 후 `Dissolve` 0→1 (RevealCurve), 없으면 가시성 켜고
            스케일 0.85→1.0. 시계는 포털 중심으로 수렴(위치 lerp)·스케일 →0. 오버레이 위치 보간(ARTrackingManager Tick)은 그대로.
Complete  : t≥FadeOutEnd. FX Deactivate → 자동 파괴, 시계 Destroy, 원 재질 복구(SetMaterial 원본), 콜리전·bClickable 복구,
            bRevealLocked=false, 입력 해제. 이후는 기존 탭→정보창.
Cancelled : (a) 오버레이 파괴/EndPlay, (b) 카메라 전면 전환, (c) 앱 백그라운드 진입, (d) 추적 소실 > Grace,
            (e) 외부 Cancel(bool bRevealModel). 전부 정리하고 bRevealModel 이면 모델 즉시 표시(폴백 열람), 아니면 숨김 유지.
            (a) 는 액터가 사라지므로 표시 복구 불필요. **StopScan/OnScanStateChanged 는 취소 트리거가 아니다.**
```
- 중복 방지 키: 컴포넌트가 오버레이 액터에 1개만 붙는다(AttachTo 가 기존 컴포넌트 있으면 반환). 새 스캔(ClearOverlay→새 스폰)이면 새 액터+새 컴포넌트 → 다시 재생 가능.
- 추적 끊김(OverlayPin TrackingState != Tracking): 진행 시간을 멈추고 마지막 위치 유지. Grace 초과 → Cancelled(bRevealModel=true). 카메라 원점으로 점프하지 않는다(ARTrackingManager 도 Tracking 일 때만 보간).
- 저프레임: 모든 단계는 `Elapsed`(누적 DeltaTime, 최대 0.1s 클램프)로 정규화. Delay 나열 금지.

### 4.3 입력
- `EnableInput(PC)`; `InputComponent->BindTouch(IE_Pressed/Released/Repeat)`. Slate 가 소비한 터치(하단 바 버튼 등)는 도달하지 않는다 — 전체 화면 투명 위젯을 만들지 않는다.
- 소유 조건: 상태 Ready, FingerIndex==Touch1(첫 손가락)만, 두 번째 손가락이 눌려 있으면 취소. 눌린 위치가 **시계의 화면 투영 사각형(+WatchHitPaddingPx)** 안일 때만 추적 시작.
- 판정(Released): dy = (PressY − ReleaseY)/ViewportHeight ≥ SwipeMinNormalized, |dx| ≤ dy×SwipeMaxSideRatio, 지속 ≤ SwipeMaxSec, 마우스(에디터)도 동일 규칙(LeftMouseButton Pressed/Released)로 PIE 테스트 가능.
- Ready~Revealing 동안 `Overlay->bRevealLocked=true` → `HandleTapped` 가 정보창을 열지 않는다. 투척 터치가 오버레이 메시에 닿아도 메시 콜리전이 꺼져 있어 도달하지 않는다.
- 순수 판정은 `FTimeRevealMath`(정적 함수)로 분리: `ClassifySwipe(...)`, `ThrowPoint(A,B,Up,Arc,s,Ease)`, `PhaseAlpha(t, start, end, curve)`. 자동화 테스트 `TimeMachineAR.Reveal.Math` + 상태 전이 테스트 `TimeMachineAR.Reveal.State`(프로파일 null FX 로 Ready→Throwing→…→Complete, 각 상태에서 Cancel, 두 번째 AttachTo 무시, 프로파일 없음 = 아무것도 안 함).

### 4.4 타깃과 포털 위치
- 타깃 = `Overlay->FleshMesh->Bounds.Origin`(숨겨져 있어도 등록된 컴포넌트의 bounds 는 유효) — 원점(마커)이 아니라 실제 모델 중심. 매 프레임 갱신(앵커 보간을 따라감).
- 포털 크기 = min(모델 bounds 구 지름 × 0.8, 320 cm). 긴 지느러미 전체를 덮는 거대 이펙트를 만들지 않는다.
- 깊이: 포털은 타깃 중심에, 모델은 그 안에서 디졸브로 나타난다(앞/뒤 가림 없음). 포털 재질은 Additive 라 검은 원판이 생기지 않는다(§5).

---

## 5. FX·블러 계약 (Opus 작업 — Sonnet 은 이 계약대로 호출만)

파생 시스템 `/Game/TimeReveal/FX/NS_WatchOrbit`(← NS_ActiveAtom), `NS_TimePortal`(← NS_Worm-Hole). 원본은 수정하지 않는다. 생성은 에디터 C++ 자동화 `TimeMachineAR.Reveal.BuildFX`(재실행 가능)로 한다: 복제 → emitter 별 `SimTarget=CPUSim`, `bLocalSpace=true`(시계/포털에 붙어 스케일을 따르게 — emitter 별 의도 확인 후), SpawnRate/Lifetime/크기 RapidIteration 값 축소, Singularity 는 spawn 대폭 축소, 렌더러 재질을 **`User.SpriteMaterial`(Object) 바인딩**으로 교체 → 컴파일 → 저장.

런타임 계약(실제로 소비됨을 검증한 것만):
| 채널 | 방법 | 소비처 |
|---|---|---|
| 색·밝기·페이드 | `M_TimeFX_Sprite`(Unlit, Additive, 텍스처 없이 UV 원형 마스크) MID 파라미터 `PrimaryColor`, `SecondaryColor`, `Intensity`, `Fade` → `NiagaraComponent::SetVariableMaterial("User.SpriteMaterial", MID)` | 스프라이트 렌더러 Material User Param Binding |
| 크기·확장 | 컴포넌트 `SetWorldScale3D` (로컬 공간 emitter) | 위치 모듈 |
| 속도 | RapidIteration 상수(에셋에 고정) | — (런타임 파라미터 아님; 필요하면 후속) |
| 종료 | `Deactivate()` + Fade→0 | |

색(sRGB→선형 변환해 넣는다): 청백 #DDF5FF, 전기 파랑 #4BA3FF, 청록 #58D8E8, 금 #D9AA57. Orbit = Primary 파랑, Ring002 = 금(포인트), Portal = 청백/파랑 + 금 소량(Down emitter).

블러: `UBackgroundBlur` 전체 화면 위젯(HitTestInvisible, ZOrder 5 — HUD(40~200) 아래) 강도 `BlurCurve`. 실제 Slate 블러이며 이미 정보창에서 실기기 검증됨. 3D 전부(카메라·모델·FX)가 흐려지고 UI 는 선명. 포털 피크(1.30~1.85 s)만.

디졸브: `M_ArchelonReveal` = `Archelon_Material` 복제 → Masked, OpacityMask = step(noise(UV×8), 1−Dissolve). 런타임에 Flesh 슬롯 0 만 MID 로 교체, Complete 시 원본 재질로 복구. 공유 원본 재질은 수정하지 않는다.

---

## 6. 파일 소유 범위

| 담당 | 새 파일 | 수정 파일 |
|---|---|---|
| Sonnet 1 | `Scripts/derive_clock_textures.py`, `Scripts/import_time_watch.py`, `Source/…/TimeWatchActor.h/.cpp`, `Tests/WatchAssemblyTest.cpp`, `docs/ai-handoff/inbox/clock/*` | (없음) — `Tests/ClockAuditTest.cpp` 는 읽기만 |
| Sonnet 2 | `Source/…/TimeRevealProfile.h`, `TimeRevealMath.h/.cpp`, `TimeRevealComponent.h/.cpp`, `Tests/TimeRevealTest.cpp` | `DinoInfoData.h`(필드 1개), `DinoOverlayActor.h/.cpp`(bRevealLocked 2줄), `ARTrackingManager.cpp`(AttachTo 호출 3줄) |
| Opus | `Tests/BuildTimeRevealFXTest.cpp`, `Scripts/build_time_reveal_materials.py`(M_TimeFX_Sprite, M_ArchelonReveal), `Content/TimeReveal/FX/*`, `DA_TimeReveal_Archelon` | `DA_Dino_Archelon`(TimeRevealProfile 지정), `TimeRevealComponent.cpp`(FX 연결부 검수) |

같은 uasset 을 두 담당이 동시에 만지지 않는다. `Content/TimeReveal/Audit/` 는 커밋하지 않는다.

---

## 7. 검증 계획

- 자동화: `TimeMachineAR.Reveal.Math`, `.State`, `.WatchAssembly`(렌더), `.ClockAudit`(파생 6방향), `.NiagaraAudit`(파생 CPU 시스템 0.5/1.5/3.0 s 캡처), 기존 `TimeMachineAR.DinoCard.*`, `Docent.*`, `Nav.*` 회귀.
- Android: `.utoc` 에 `/TimeReveal/` 에셋 포함 확인 → 실기기: 마커 인식→시계 준비→스와이프→명중→문→등장→정보창→AR 복귀 녹화(`adb shell screenrecord`), 5단계 캡처, 취소/반복 10회, 추적 끊김, 백그라운드 복귀, `stat unit`/`stat fps` 대신 `-csvprofile`/`adb shell dumpsys gfxinfo` 로 프레임 시간(기준 AR 대비).
- 완료 판정은 실기기 결과로만. 미검증 항목은 handoff.md 에 그대로 남긴다.
