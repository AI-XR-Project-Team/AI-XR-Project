# 아르켈론 정보 화면 (바다 스킨)

AR 에서 아르켈론을 터치하면 열리는 세로 전체 화면 정보창. 시안(`docs/ai-handoff/inbox/archelon_info/src/ref_02~05`)을
기존 `WBP_DinoInfoCard` 위에 C++ 로 재구성했다. 도슨트·내비 스킨과 같은 방식이다 — WBP 파일은 그대로 두고
`UDinoInfoCardWidget::BuildOceanSkin` 이 위젯 트리를 짠다.

## 켜는 방법

`DA_Dino_*` 의 **CardSkin = Ocean** 이면 이 화면, `Default` 면 기존 카드다. 지금은 `DA_Dino_Archelon` 만 Ocean 이다.
다른 종은 손대지 않았다(티라노 카드 렌더가 스킨 전환 전후로 동일함을 `TimeMachineAR.DinoCard.SkinPreview` 가 남긴다).

## 데이터 (`DinoInfoData.h`)

| 필드 | 용도 |
|---|---|
| `CardSkin`, `Background`, `ZoneName`, `ZoneSub` | 스킨 선택, 배경 텍스처, 상단 위치 배지 두 줄 |
| `DocentPrompt` | CTA 아랫줄 공통 문구. 탭의 `DocentPrompt` 가 있으면 그것이 우선 |
| `FDinoTab::Stats` | 탭별 요약 타일 4개. 비우면 종 공통 `Stats` 로 폴백(예전 DA 호환) |
| `FDinoStat::IconTint` | 흰 마스크 아이콘에 입힐 색. 바다 스킨 타일만 쓴다 |
| `DietTag`/`DietIcon`, `PeriodTag`/`PeriodIcon` | 히어로 오른쪽 배지(해양 파충류 / 백악기 후기) |

값은 `TimeMachineAR/Scripts/fill_archelon_info_data.py` 가 채운다(멱등). 문구·수치는 디자인 레퍼런스에서 옮긴
초안이며 학술 검증 전이다.

## 리소스

- 원본 7장: `docs/ai-handoff/inbox/archelon_info/src/` (수정 금지)
- 파생: `tools/derive_archelon_info_assets.py` → `docs/ai-handoff/inbox/archelon_info/T_*.png` + `manifest.json`
  (원본 파일·좌표·가공 내용 기록). 시트의 알파는 실제 컷아웃이라 색상 키를 쓰지 않았다. 겹친 요소는 색온도
  (청록/갈색)와 연결 성분으로 갈랐고, 지도의 영문 라벨은 inpaint 로 지웠다. 아이콘 25종은 흰 마스크다.
- 배경 `T_OceanBackground` 는 전용 원본이 없어 합성(남색 그라데이션 + 수면 글로우 + 빛줄기 + 보케).
  시안의 박물관 사진 배경과는 다르다.
- 임포트: `TimeMachineAR/Scripts/import_archelon_info_skin.py` → `/Game/UI/DinoCard/ArchelonSkin` + `ArchelonSkinCookLabel`
  (경로 문자열 로드라 ALWAYS_COOK 라벨로 쿡에 묶는다).

## 동작 연결

- 터치 → `BP_DinoOverlay_Archelon`(OnDinoClicked → ShowFor) 그대로. 카드는 ZOrder 100 하나만 쓴다.
- 탭 전환 → 제목·본문·삽화·타일·CTA 문구만 바뀌고 본문 스크롤은 맨 위로.
- 뒤로 버튼 / Android 뒤로 키 / 메뉴의 "AR 화면으로 돌아가기" / 하단 AR 스캔 → `HideCard`.
- CTA / 하단 도슨트 / 메뉴 "렉시에게 물어보기" → `OnAskDocentClicked(ExhibitKey)` → 레벨 BP 가 HideCard → OpenChat → ShowChat.
  채팅이 닫히면(`UDocentChatWidget::OnOpenStateChanged`) 같은 공룡·같은 탭으로 다시 연다.
- 하단 내비게이션 / 메뉴 "내비게이션 열기" → 카드를 닫고 AR 화면 하단 바의 `NabButton` 을 대신 눌러 기존 지도 화면을 연다.
- 밀어 닫기는 이 스킨에서 끈다(본문 스크롤과 충돌).
- ExhibitKey 는 `archelon_full_skeleton`. 서버 시드 `backend_server/seeds/05_dino_archelon.sql` 을 적용해야 도슨트 세션이 열린다
  (예전 DA 값은 티라노 DA 를 복사하며 딸려 온 `trex_full_skeleton` 이었다).

## 검증

- 에디터 빌드: `TimeMachineAREditor Win64 Development`
- 렌더: `UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests TimeMachineAR.DinoCard.SkinPreview; Quit" -RenderOffscreen -unattended`
  → `Saved/DinoCardOcean_<n>_<tab>_<2340|1920>.png`, `Saved/DinoCardDefault_TRex.png`, `Saved/DinoCardSwitch_*.png`.
  1080×2340 은 실기기 1440×3120(DPI 1.333), 1080×1920 은 720×1280 / 1080×1920 기기다.
- 실기기: `docs/screenshots/archelon_info/` 참고.

## 남은 차이

- 배경이 시안의 박물관 사진이 아니라 합성 그라데이션이다.
- 패널·탭 바·타일은 9-slice 대신 둥근 브러시(반투명 남색 + 가는 청록 테두리)다. 시안의 발광 테두리는 테두리 알파로만 표현했다.
- 학명 이탤릭은 폰트에 이탤릭이 없어 `SkewAmount` 로 기울였다.
- 긴 화면(2340)에서는 본문과 타일 사이 여백이 시안보다 넓다(타일을 패널 바닥에 붙이는 배치).
