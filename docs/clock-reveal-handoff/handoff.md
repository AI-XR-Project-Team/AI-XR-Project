# handoff.md — 회중시계 투척 · 시간의 문 · 아르켈론 등장

각 작업이 끝날 때마다 아래에 **변경 파일 · 실행한 검증 · 캡처 · 미해결** 을 덧붙인다. 다음 담당은 맨 아래 항목부터 읽는다.
브랜치: `feature/clock-time-reveal` (develop `0fba913` 에서 분기). 원격 push/merge 는 사용자 승인 후에만.

---

## A. Opus 사전 조사·설계 (2026-09-14, claude-opus-5)

### 변경/추가 파일
- `docs/clock-reveal-handoff/design.md` — 확정 설계(구조·조립 수치·상태/입력/궤적 계약·FX 계약·파일 소유).
- `docs/clock-reveal-handoff/clock_geometry_audit.log`, `niagara_audit_original.log` — 실제 에셋 덤프.
- `docs/screenshots/clock/*` — 본체 6방향·3/4·바늘 렌더(원본 2M tris, 베이스컬러만).
- `TimeMachineAR/Scripts/inspect_clock_reveal_inputs.py` — BP/Registry/세션/메시/의존성 읽기 전용 감사.
- `TimeMachineAR/Scripts/audit_clock_fbx.py` — FBX 3개를 `/Game/TimeReveal/Audit/` 에 원본 그대로 임포트(+베이스컬러 재질). **감사 전용, gitignore.**
- `TimeMachineAR/Source/TimeMachineAR/Tests/ClockAuditTest.cpp` — `TimeMachineAR.Reveal.ClockAudit`: 6방향 직교 렌더 + 정점 분석(문자판 평면/중심/반지름, 바늘 허브/끝/두께).
- `TimeMachineAR/Source/TimeMachineAR/Tests/NiagaraAuditTest.cpp` — `TimeMachineAR.Reveal.NiagaraAudit`: emitter Sim/공간/bounds/렌더러/RapidIteration 값 덤프 + CPU 시스템 오프스크린 캡처.
- `TimeMachineAR/Source/TimeMachineAR/TimeMachineAR.Build.cs` — `Niagara` 모듈 의존 추가(런타임).
- `.gitignore` — `TimeMachineAR/Content/TimeReveal/Audit/` 제외.

### 확인한 사실 (design.md §1 요약)
- 시계: 본체 1.99M / 시침 1.93M / 분침 1.07M tris, 각각 190cm 로 정규화. 문자판 = 앞 케이스 +Y 면(Y 37.5), 중심 (0.6, 37.5, −25.5), 숫자 링 35~48, 베젤 66. **문자판에 바늘 없음**. 바늘: 평면 XZ, 끝 +Z, 허브 고리 −Z 끝(Z −79.8/−79.9). FBX 에 텍스처 임베디드(추출 `.fbm` 삭제해 원본 폴더 복원).
- Niagara: 두 원본 모두 **전 emitter GPU 시뮬**, 5000/s(Ring×3)·1000/s, 고정 bounds ±1000, 월드 공간, User 파라미터 0개, 엔진 기본 스프라이트 재질. 깊이 충돌/GPU 전용 DI 없음 → CPU 재작성 가능. 원본 GPU 시뮬은 에디터 오프스크린 캡처에서 진행되지 않음(검수는 CPU 파생본으로).
- 코드: AR 맵 Pawn = BP_ARPawn(응시 없음). Overlay 클래스는 모든 종 BP_DinoOverlay_T-Rex. 아르켈론 재질 Opaque·Alpha 없음 → 인식 즉시 노출(현재). BP_DinoOverlay_Archelon 은 런타임 미사용. 기존 카메라 블러는 UMG BackgroundBlur(실기기 검증됨).
- `Content/FreeNiagaraPack/` 은 git 미추적(3파일). `TimeMachineAR/Asset/`(FBX 원본)도 gitignore.

### 검증
- 에디터 빌드(Niagara 의존 포함) 성공. `Reveal.ClockAudit`, `Reveal.NiagaraAudit` Success(-RenderOffscreen).
- 원본 폴더: `.fbm` 삭제 후 원본 5파일×3 그대로.

### 미해결 / 다음 담당에게
- Sonnet 작업 1(시계 임포트·조립) → design.md §3. 그다음 Sonnet 작업 2(상태·입력·궤적) → §4. FX/블러/디졸브/통합/실기기 = Opus §5.
- 아르켈론 2M tris + LOD 1개는 이번 범위 밖이지만 연출 중 프레임 시간의 지배 항목이 될 것. 측정 후 사용자에게 LOD 생성 여부를 물을 것.
- FreeNiagaraPack 원본 커밋 여부는 사용자 결정 필요(파생본만으로 앱은 동작).

---

## B. Sonnet 작업 1 — 시계 임포트·조립 (2026-09-14 18:22~18:55, claude-sonnet-5, 사용량 한도로 중단)

Sonnet 서브에이전트가 `Scripts/derive_clock_textures.py`, `Scripts/import_time_watch.py`, `Source/…/TimeWatchActor.h/.cpp`,
`Tests/WatchAssemblyTest.cpp`, `docs/ai-handoff/inbox/clock/*.png`(파생 텍스처)와 `/Game/TimeReveal/Clock/*` 를 만든 뒤
월 사용량 한도(HTTP 429)로 종료됐다. handoff 기록은 남기지 못했다. 결과물은 아래 C/D 에서 검증·인계했다.

## C. Codex 작업 (2026-09-14 18:49~21:15, OpenAI Codex, 사용자 별도 실행)

Opus 설계(design.md §4~§5)를 Codex 가 이어서 구현했다(파일은 저장소에 있는 그대로; 주석/들여쓰기 스타일이 저장소와 다르다).
- `TimeRevealProfile.h/.cpp`, `TimeRevealMath.h/.cpp`, `TimeRevealComponent.h/.cpp` — 상태 머신(Idle→Ready→Throwing→Impact→Portal→Revealing→Complete/Cancelled),
  스와이프 판정, 포물선, 입력 소유(시계 화면 반경 안 터치만, 두 번째 손가락 거부), 취소(오버레이 파괴·전면 카메라·백그라운드·추적 소실 Grace·채팅/내비 패널 열림), 블러(SBackgroundBlur ZOrder 5 = UMG 위젯 아래), 디졸브 MID 교체/복구.
- `ARTrackingManager.cpp`(Registry 직접 매핑일 때만 `UTimeRevealComponent::AttachTo`), `DinoInfoData.h`(`TimeRevealProfile`), `DinoOverlayActor`(`bRevealLocked` → StartReveal/HandleTapped 차단).
- `Tests/BuildTimeRevealFXTest.cpp`(`TimeMachineAR.Reveal.BuildFX`): 원본 복제 → 전 emitter CPU·LocalSpace·고정 bounds ±350, SpawnRate 240/12/280, Lifetime 0.8~1.4, Large Radius 78, Grid 4×4×4, 렌더러 재질 `M_TimeFX_Sprite` + **`User.SpriteMaterial` 바인딩(단일 — design §5 의 Gold 분리는 미적용)**, 컴파일·저장. 결과 `/Game/TimeReveal/FX/NS_WatchOrbit`, `NS_TimePortal`.
- `Scripts/build_time_reveal_materials.py`(Opus 작성) 실행 → `M_TimeFX_Sprite`(Additive), `T_TimeReveal_Noise`, `M_ArchelonReveal`(Masked 디졸브), cook label.
- `Scripts/configure_time_reveal.py` → `DA_TimeReveal_Archelon`(Watch/Orbit/Portal/Sprite/Reveal 참조, Primary=파랑 #4BA3FF 선형, Secondary=금 #D9AA57 선형, ReadyScreenAnchor (0.5,0.60)) + `DA_Dino_Archelon.TimeRevealProfile` 지정(마커 EX5-ARCHELON 확인 후).
- `Tests/TimeRevealTest.cpp`(`Reveal.Math`, `Reveal.State`), `Tests/RepairWatchMaterialTest.cpp`(MRTex 샘플러 Masks 교정).
- `Scripts/refine_watch_body.py`: 본체 LOD0 0.4%(7,972 tris)에서 문자판/뚜껑 텍스처가 조각나서 **5%(99,651 tris)** 로 재축소.

## D. Opus 완료·검증 (2026-09-14 21:11~, claude-opus-5)

Codex 가 남긴 상태에서 이어서 검증했다.
- 에디터 빌드 성공. 자동화 15개 Success: `Reveal.Math/State/WatchAssembly/NiagaraAudit`, `Nav.*`(6), `DinoCard.*`(2), `Docent.*`(2). (`Reveal.BuildFX` 는 자산 재생성용이라 재실행하지 않음 — 결과 자산은 NiagaraAudit 로 검수.)
- 시계 조립(`docs/screenshots/clock/WatchAssembly_*`): 본체 99,651 + 시침 1,542 + 분침 1,504 = **102,697 tris**(설계 초기 예산 10~20k 초과 — 8k 본체는 문자판 UV 조각화로 기각, `WatchAssembly_montage_8k_rejected.png`). 텍스처 본체 2K/2K/1K, 바늘 1K/1K/512(MR 패킹). 재질 3(M_Watch_Body, M_Watch_Hand + MI 2). 허브가 문자판 중앙 점 위, 4각도에서 공전 없음, 측면 관통 없음. 문자판의 흰 얼룩은 테스트 조명/노멀맵 영향으로 보이며 실기기 확인 항목.
- 파생 FX(`docs/screenshots/clock/NiagaraDerived_montage.png`, `niagara_audit_derived.log`): 두 시스템 전 emitter **CPU**, User.SpriteMaterial 로 넣은 청색이 실제로 렌더됨(그래프 소비 검증). 예산 ≈ Orbit 1,000 / Portal 850 파티클.
- Android Shipping 패키징 성공 → `TimeMachineAR/Build/ClockRevealAndroid/Android_ASTC/TimeMachineAR-Android-Shipping-arm64.apk`(405MB).
  `.utoc` 목록 확인: `/TimeReveal/` 아래 시계 메시·텍스처·재질·FX 2개·M_TimeFX_Sprite·M_ArchelonReveal·노이즈·DA 전부 포함, FreeNiagaraPack 원본과 `Audit/` 은 미포함.
- 폰(SM-S936N) 설치·기동 확인(프로세스 살아 있음). **화면 잠금(PIN) 상태라 캡처/연출 검증 불가** — 잠금 해제 후 EX5-ARCHELON 마커로 시나리오를 돌려야 한다.

### 미검증 (완료 아님)
- 실기기: 마커 인식→시계 준비→스와이프→명중→문→등장→정보창→AR 복귀, 5단계 캡처, 녹화, 취소/반복 10회, 추적 끊김, 백그라운드 복귀, 프레임 시간(기준 AR 대비). 전부 미실행.
- 시계 크기(WatchScale 0.07·거리 50cm)와 ReadyScreenAnchor(0.60)의 실기기 적합성, 금속이 어두운 배경에서 죽는지(GoldGlow 0.18 기본).
- Portal 컴포넌트 스케일(Size/200)·Orbit 스케일 1.1 은 에디터 캡처 기준 추정치.
- design §5 대비 차이: Gold 두 번째 재질 바인딩 없음(단일 SpriteMaterial, SecondaryMix 로만 금색 소량), Singularity 12/s 유지.
- 아르켈론 2M tris 는 그대로(연출 중 프레임 시간 지배 항목 예상).
- `Content/FreeNiagaraPack/`(원본 3파일)·`Asset/Clock/` 은 여전히 git 미추적. 커밋/푸시 미실행(브랜치 `feature/clock-time-reveal`).

---

## E. Sonnet — HUD 탭 배타 표시 수정 (2026-09-14, claude-sonnet-5)

회중시계 작업과는 별개로, 실기기 캡처(`docs/screenshots/phone_20260913_*.png`)에서 확인된 HUD 버그를 고쳤다:
도슨트 탭을 열면 스캔 패널(틀·조준원·위치 칩·힌트 버블·셔터)이 채팅 위에 그대로 남아 겹쳐 보이던 문제.
원인은 WBP_DocentChat 이벤트 그래프와 C++ 양쪽이 같은 버튼(NabButton/ScanButton/OpenButton 등)에 따로 붙어
패널 가시성을 각자 건드리던 것. WBP 그래프는 볼 수도 고칠 수도 없어, C++ 쪽이 매 틱 다시 강제하는 방식으로 해결했다.

### 변경 파일
- `Source/TimeMachineAR/DocentChatWidget.h` — `EDocentHudTab`(Scan/Nav/Docent) UENUM 추가. `SetHudTab`/`GetHudTab`(public), `ApplyHudTab`/`ActiveTab`/`HandleScanTabClicked`/`HandleNavTabClicked`(private), `NativeTick` 오버라이드 선언.
- `Source/TimeMachineAR/DocentChatWidget.cpp` — `ApplyHudTab` 구현, `SetHudTab`, `NativeTick`(`Super::NativeTick` 뒤 `ApplyHudTab()` 만 추가). `ShowChat`/`HideChat`/`HandleOpenClicked`/`HandleReferenceRescan`/`HandleMarkerFound` 의 다음 틱 람다를 탭 하나로 통일. `NativeConstruct`/`NativeDestruct` 에 `ScanButton`·`NabButton`·`NavCloseButton`·`ScanCloseButton` 바인딩(Add/RemoveUniqueDynamic) 추가, 기존 `HandleReferenceExit` 바인딩은 그대로 유지.
- `Source/TimeMachineAR/Tests/DocentTabTest.cpp`(신규) — `TimeMachineAR.Docent.TabExclusive`.

### 구현한 규칙
- 이 위젯이 `ScanPanel`/`NavPanel`/`ChatPanel`/`BottomBar`/`Btn_CloseAR` 배타 표시의 유일한 소유자(`ApplyHudTab`, `ActiveTab` 기준). `GetWidgetFromName` 으로 찾고, 값이 다를 때만 `SetVisibility` 한다.
  - Scan: ScanPanel=SelfHitTestInvisible, NavPanel=Collapsed, 채팅 닫힘, BottomBar=Visible, Btn_CloseAR=인식 여부에 따라 Visible/Hidden.
  - Nav: NavPanel=Visible, ScanPanel=Collapsed, 채팅 닫힘, BottomBar=Collapsed(전체화면 내비, 자체 NavCloseButton 이 Scan 으로 복귀), Btn_CloseAR=Hidden.
  - Docent: 채팅 열림(ApplyOpenState 가 ChatPanel 담당), ScanPanel/NavPanel=Collapsed, Btn_CloseAR=Hidden.
- WBP 그래프가 우리보다 먼저 같은 위젯을 건드릴 수 있어 (a) 각 클릭 핸들러에서 `SetHudTab` 호출, (b) `NativeTick` 에서 `ApplyHudTab()` 재호출, 두 경로로 다시 강제한다. `NativeTick` 은 `Super` 호출 뒤 그 한 줄만 추가했고, 기존 키보드/포커스 로직(`PollInputFocus` 타이머 기반)은 건드리지 않았다.
- `ShowChat()`→`SetHudTab(Docent)`, `HideChat()`→ 현재 탭이 Docent 면 `SetHudTab(Scan)`, 아니면 `ApplyOpenState(false)`만. `ApplyOpenState` 는 그대로 저수준 함수로 남겨 뒀다.
- 초기 상태: `NativeConstruct` 끝에서 `bStartOpen`(주로 WBP 기본값 false)·`OpenButton` 유무로 Scan 또는 Docent 중 하나를 고르되, 스캔 탭이어도 `StartScan()` 은 부르지 않는다(스캔 꺼진 채 "AR 스캔을 눌러 시작해주세요" 힌트만 보이는 첫 화면).
- `HandleMarkerFound` 의 다음 틱 람다는 `ActiveTab==Scan` 일 때만 `ApplyHudTab()` 을 불러 스캔 패널을 되돌리고, 아니면 `RefreshReferenceUI()` 만 한다. `HandleReferenceRescan` 은 패널을 직접 만지던 코드를 걷어내고 `SetHudTab(Scan)` 으로 바꿨다(그 뒤 `StartScan()` 은 그대로 호출).
- `SetRevealHint`/`RevealHint`(TimeRevealComponent 가 씀)는 손대지 않았다 — `RefreshReferenceUI` 내부 로직 그대로.

### 검증
- 빌드: `Build.bat TimeMachineAREditor Win64 Development` 성공(에러 0).
- 테스트(`UnrealEditor-Cmd -ExecCmds="Automation RunTests TimeMachineAR.Docent+TimeMachineAR.Reveal.State; Quit" -RenderOffscreen`): `Docent.SkinPreview`, `Docent.ScanPreview`, `Docent.TabExclusive`, `Reveal.State` 전부 **Success**. `TabExclusive` 로그에 "위젯을 찾지 못해 건너뜀" 경고가 없어 ScanPanel/NavPanel/ChatPanel/BottomBar 를 전부 실제로 찾아 검증했다(스킵 없음).
  로그: `scratchpad/logs/build1.log`, `scratchpad/logs/tests1.log`.

### 확인하지 못한 것
- 실기기 미검증(요청 범위 밖 — 에디터 자동화만). WBP 이벤트 그래프를 직접 읽을 수 없어서, 그 그래프가 정확히 무엇을 하는지는 추정(스크린샷·증상 기반)이며 `NativeTick` 매 프레임 재강제로 결과만 덮었다. 프레임마다 `ApplyHudTab`→`RefreshReferenceUI` 가 `GetWidgetFromName` 을 10회 안팎 호출하는 비용은 측정하지 않았다(기존 `PollInputFocus` 도 0.1초마다 같은 함수를 불렀던 것과 같은 패턴이라 크게 새로운 비용은 아니라고 판단).

## F. Opus — 탭 수정 검수·패키징 (2026-09-14 22:20)
- Sonnet 의 `ApplyHudTab` 에 `bForceRefresh` 를 추가해 매 틱 `RefreshReferenceUI`(브러시/텍스트 재설정)는 실제로 가시성이 바뀐 프레임에만 돌게 함(SetHudTab 은 항상 갱신).
- 렉시 단계 문구(`SetRevealHint`, TimeRevealComponent::ApplyHudHint)와 시계 뒤집힘 수정(MakeFromXZ)도 이 빌드에 포함.
- 빌드 성공, `Docent.SkinPreview/ScanPreview/TabExclusive`, `Reveal.State`, `DinoCard.*` Success. Android Shipping 패키징 → 폰 설치·기동(사용자가 내비 탭 사용 중이라 도슨트 탭 겹침 해소는 실기기 미확인).

---

## G. Sonnet — 내비 렉시 말풍선·도착 자동 종료 (2026-09-14, claude-sonnet-5)

`docs/nav-lexi-guide-design.md`(Opus 설계) 를 그대로 구현했다: 내비 상단 안내 로그를 렉시 말풍선 UI 로 바꾸고,
목적지 도착 시 도착 문구 → 마무리 문구 → 경로 정리 → 스캔 탭 복귀까지 자동으로 흐르게 했다.

### 변경/추가 파일
- `Source/TimeMachineAR/NavDestinations.h/.cpp` — `GuidingText`/`ArrivalText`/`RecognizedText` 제거, `ToParticle`("으로/로")·`SubjectParticle`("이/가", TimeRevealComponent 의 익명 함수를 이관)·`LexiNavOnText`/`LexiLocalizedText`/`LexiGuidingText`/`LexiOffRouteText`/`LexiArrivedText`/`LexiEndedText`/`LexiRecognizedText` 추가.
- `Source/TimeMachineAR/NavDestinationsTest.cpp` — 옛 문구 테스트 제거, 조사 규칙(ToParticle 전체 예시·SubjectParticle)·Lexi 문구(멀리/근접/서버원문없음/label 없음·도착·종료) 어서션 추가.
- `Source/TimeMachineAR/NavGuideLogWidget.h/.cpp` — 전면 재작성. 트리 = CanvasPanel→HorizontalBox(SizeBox 96×96+Image 아바타, Border 말풍선(FSlateRoundedBoxBrush 반지름22·유리 FColor(18,22,30,215)·외곽선1.5px)→VerticalBox("렉시" 이름표+MessageText)). `ENavGuidePhase` 에 `OffRoute`/`Ended` 추가. `OnNavigationEnded`→Ended, `OnNavigationClosed`→Localized 바인딩. Guiding 은 `LastGuidance` 를 저장해 매틱 재계산하되 텍스트가 같으면 `SetText` 생략. 아바타 4종(`lexi_question/pointing/happy/idle`)은 최초 1회 로드하고 실패하면 아바타 칸만 접는다. 테스트/미리보기 전용 `PreviewSetState(Phase, NodeType, Label, Guidance)` 추가(문서화된 대로 미니맵 없이 상태를 밀어 넣고 `SetVisibility(HitTestInvisible)`).
- `Source/TimeMachineAR/NavArrivalAutoEnd.h/.cpp`(신규) — 순수 상태 헬퍼 `FNavArrivalAutoEnd`(Dwell→Ended→EndMessage→Closed). 도착 판정이 Dwell 중 풀리면(Ended 전) 리셋, Ended 이후에는 bArrived 값을 더 보지 않고 EndMessage 로 직행, Closed 이후 Reset() 전까지 None.
- `Source/TimeMachineAR/Tests/NavArrivalAutoEndTest.cpp`(신규) — `TimeMachineAR.Nav.ArrivalAutoEnd`. Dwell 리셋·Ended→Closed 순서/시간·Reset 재사용 6개 시나리오.
- `Source/TimeMachineAR/NavMinimapWidget.h/.cpp` — 델리게이트 `OnNavigationEnded`/`OnNavigationClosed`(무인자, BlueprintAssignable), Config `ArriveAutoEndDwellSec=4`/`ArriveEndMessageSec=2.5`, 멤버 `FNavArrivalAutoEnd AutoEnd`+`LastAutoEndRealTimeSeconds`. `SetCurrentPose`(Follow, HasRoute) 안에서 `GetWorld()->GetRealTimeSeconds()` 두 시점 차를 0~0.25s 로 clamp 해 `AutoEnd.Update(P.bArrived, Delta)` 호출 → `Ended`면 `OnNavigationEnded.Broadcast()`, `Closed`면 `ClearRoute()+SetDestinationNode("")` 뒤 `OnNavigationClosed.Broadcast()`. `SetRouteXY`/`ClearRoute` 에서 `AutoEnd.Reset()`.
- `Source/TimeMachineAR/DocentChatWidget.h/.cpp` — `NativeConstruct` 에서 `GetWidgetFromName("WBP_NavMinimap")`→`Cast<UNavMinimapWidget>`→`OnNavigationClosed.AddUniqueDynamic(..HandleNavClosedByAutoEnd)`(→`SetHudTab(Scan)`), `NativeDestruct` 에서 `RemoveDynamic`.
- `Source/TimeMachineAR/TimeRevealComponent.cpp` — 익명 `SubjectParticle` 제거, `FNavDestinations::SubjectParticle` 호출로 교체.
- `Source/TimeMachineAR/Tests/NavGuideLogPreviewTest.cpp`(신규) — `TimeMachineAR.Nav.GuideLogPreview`. `CreateWidget<UNavGuideLogWidget>`(에디터 월드) + `PreviewSetState` 로 Guiding/Near/Arrived/Ended 4장을 1080×300 렌더.
- **설계 범위 밖의 불가피한 추가 변경**(design §5 는 이 두 파일을 "건드리지 않음"으로 명시했으나, §2 의 `HandleGuidanceUpdated`가 참조하는 `Guidance.bOffRoute` 가 `FNavGuidance` 에 실제로 없어 컴파일이 안 됐다 — 설계가 가정한 필드가 코드베이스에 없던 것으로 보인다. 최소 범위로 추가):
  - `Source/TimeMachineAR/NavTypes.h` — `FNavGuidance` 에 `bool bOffRoute = false;` 필드 추가(주석: FNavProgress.bOffRoute 를 그대로 옮긴 값).
  - `Source/TimeMachineAR/NavRouteProgress.cpp` — `GetGuidance()` 에 `G.bOffRoute = LastProgress.bOffRoute;` 한 줄 추가(다른 로직 무변경).

### 최종 문구(요지)
- NavOn: "바닥의 마커를 비춰주세요!\n제가 지금 위치를 찾아볼게요." / Localized: "위치를 찾았어요!\n오른쪽 미니맵을 눌러 목적지를 골라주세요."
- Guiding: RemainingCm≤500 이면 "조금만 더 가면 도착해요!", 아니면 "{목적지}{으로/로} 안내할게요!" + 2줄(서버 Instruction 원문, 없으면 발자국/화살표 안내).
- OffRoute: "경로에서 조금 벗어났어요.\n발자국이 보이는 곳으로 돌아와 주세요."
- Arrived: exhibit "{공룡} 앞에 도착했어요!\n지금 이 공룡의 마커를 스캔하면 AR로 더 자세히 볼 수 있어요." / facility "{Label}에 도착했어요!\n안내를 마칠게요." / entrance "{Label}에 도착했어요!\n즐거운 관람 되셨나요?"
- Ended: exhibit "안내를 마칠게요.\nAR 스캔 탭에서 마커를 비춰보세요!" / 그 외 "안내를 마칠게요.\n또 필요하면 미니맵을 눌러주세요."
- Recognized: "인식 완료!\n이제 공룡을 자세히 살펴보세요."

### 검증
- 빌드: `Build.bat TimeMachineAREditor Win64 Development` 성공(에러 0).
- 헤드리스(`-nullrhi`, `TimeMachineAR.Nav`): `ArrivalAutoEnd`/`Destinations`/`FloorGuide`/`FloorGuidePreview`/`Guidance`/`GuideLogPreview`/`LocalizerAnchor`/`RouteProgress` **8개 전부 Success**. (`Nav.SkinPreview` 는 렌더 테스트라 `-nullrhi` 로 돌리면 크래시하는데, 이 테스트를 손대기 전(원본 상태)부터 같은 크래시가 재현돼 이번 변경과 무관한 기존 동작으로 판단 — `-RenderOffscreen` 으로는 정상 통과.)
- 렌더(`-RenderOffscreen`, `TimeMachineAR.Nav.GuideLogPreview`+`TimeMachineAR.Nav.SkinPreview`+`TimeMachineAR.Docent`+`TimeMachineAR.Reveal.State`): `Nav.GuideLogPreview`/`Nav.SkinPreview`/`Docent.ScanPreview`/`Docent.SkinPreview`/`Docent.TabExclusive`/`Reveal.State` **6개 전부 Success**.
- 구현 중 발견·수정한 버그 2건(둘 다 새로 만든 코드/테스트의 버그였고 최종적으로 고쳐 전부 Success):
  1. `NavArrivalAutoEndTest.cpp` 최초 실패 — `FNavArrivalAutoEnd::Update` 가 Idle→Dwelling 으로 전이되는 바로 그 틱의 `DeltaSec` 을 버리고 있었다(그 틱은 그냥 리셋만 하고 `return`). 매 프레임 한 틱만큼 Dwell 판정이 밀리는 버그. `Idle` 블록에서 `return` 하지 않고 `Dwelling` 블록으로 흘려보내(같은 틱의 델타를 곧장 반영) 수정.
  2. `NavDestinationsTest.cpp` 자체 버그 — "트리케라톱스" 마지막 음절 "스"는 받침이 없는데 `SubjectParticle` 기대값을 "이"(받침 있음)로 잘못 적어 실패. 기대값을 "가"로 수정(코드는 처음부터 맞았다).

### 렌더 결과가 보여주는 것
- `NavGuideLogPreview_Guiding.png` — 파란 외곽선, `lexi_pointing` 아바타, "티라노사우루스 렉스로 안내할게요!" + 서버 원문 "앞으로 6m 직진하세요".
- `NavGuideLogPreview_Near.png` — 파란 외곽선, "조금만 더 가면 도착해요!" + (원문 없어) "바닥의 발자국을 따라오세요." 대체 문구.
- `NavGuideLogPreview_Arrived.png` — 초록 외곽선, `lexi_happy` 아바타, "티라노사우루스 렉스 앞에 도착했어요!" + AR 스캔 안내.
- `NavGuideLogPreview_Ended.png` — 파란 외곽선(기본색), `lexi_idle` 아바타, "안내를 마칠게요.\nAR 스캔 탭에서 마커를 비춰보세요!"
- 넷 다 아바타·이름표·말풍선·줄바꿈이 잘리지 않고 2줄 안에 들어온다(설계 최대 3줄 여유 안). 최초 렌더는 `NewObject` 로 위젯을 만들어 `WidgetTree` 가 비어 있어 완전히 검은 화면이었다 — `CreateWidget<UNavGuideLogWidget>(World, …)` 로 바꿔 해결(테스트 자체의 버그, 위젯 코드는 처음부터 정상).
- `NavSkinPreview.png`(전체 지도 회귀) — 이번 변경과 무관하게 그대로 렌더됨(레이아웃 이상 없음).
- 4장 전부 `docs/screenshots/clock/NavGuideLogPreview_{Guiding,Near,Arrived,Ended}.png` 로 복사해 두었다.

### 확인하지 못한 것(실기기 미검증)
- 마커 인식 → 목적지 선택 → 안내 문구 전이 → 도착 → 4초 뒤 마무리 문구 → 2.5초 뒤 스캔 탭 복귀. 에디터 자동화만 실행했고 Android 패키징/설치는 이번 작업 범위 밖이라 하지 않았다.
- `bOffRoute`(경로 이탈) 문구·주황 외곽선은 실측 GPS/측위 데이터로 이탈을 유도해 본 적이 없다 — `NavRouteProgress`/`NavMinimapWidget` 의 기존 이탈 판정 로직(`LateralOffsetCm > OffRouteThresholdCm`)을 그대로 타므로 로직상 문제는 없어 보이나 실기기 확인 필요.
- `ArriveAutoEndDwellSec`/`ArriveEndMessageSec` 를 ini 로 실제로 바꿔서 확인해 보지는 않았다(Config 프로퍼티로 노출만 함).
- WBP_NavMinimap 위젯 이름이 실제 WBP_DocentChat 트리 안에서 정확히 "WBP_NavMinimap" 인지는 design.md §0 조사 결과를 그대로 신뢰했다(WBP 파일을 직접 열어 보지 않음, "건드리지 않음" 지시 준수). 이름이 다르면 `Cast<UNavMinimapWidget>(GetWidgetFromName(...))` 가 매번 nullptr 이 되어 `OnNavigationClosed` 구독이 조용히 안 걸린다 — 실기기 확인 시 로그 없이도 스캔 탭 복귀가 안 되면 이 지점을 의심할 것.
