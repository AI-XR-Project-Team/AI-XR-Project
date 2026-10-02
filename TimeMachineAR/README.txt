전시물 마커 4종 — DA_ARSession 등록용 데이터에셋
=================================================

이 압축은 전시물(공룡) 마커 4개를 언리얼에 바로 넣어 DA_ARSession 의
CandidateImages 에 등록할 수 있게 만든 것입니다. (인쇄용 PDF 가 아니라
실제 .uasset 데이터에셋입니다.)

들어 있는 것 (Content/UI/Nav/Markers/)
--------------------------------------
  Cand_EX1_A2.uasset   + T_Marker_EX1_A2.uasset
  Cand_EX2.uasset      + T_Marker_EX2.uasset
  Cand_EX3.uasset      + T_Marker_EX3.uasset
  Cand_EX4.uasset      + T_Marker_EX4.uasset

  Cand_* = ARCandidateImage (DA_ARSession 에 넣는 것)
  T_Marker_* = 그 candidate 가 참조하는 원본 텍스처 (같이 있어야 참조가 안 깨짐)

마커 code(FriendlyName) ↔ 공룡  ★ DinoRegistry 가 이 code 로 분기
------------------------------------------------------------------
  Cand_EX1_A2  ->  EX1-TRICERATOPS  ->  트리케라톱스
  Cand_EX2     ->  EX2-BRACHIO      ->  브라키오사우르스
  Cand_EX3     ->  EX3-TREX         ->  티라노사우르스 렉스
  Cand_EX4     ->  EX4-ANKYLO       ->  안킬로사우르스

설치 방법
---------
1. 언리얼 에디터를 끈다.
2. 이 압축의 `Content` 폴더를 프로젝트의 `TimeMachineAR/` 안에 풀어
   합친다 → 파일이 `TimeMachineAR/Content/UI/Nav/Markers/` 에 놓인다.
   (경로가 중요하다. Cand 가 텍스처를 `/Game/UI/Nav/Markers/...` 로 참조하므로
    이 경로에 있어야 참조가 산다.)
3. 에디터를 켠다. 콘텐츠 브라우저 `UI/Nav/Markers` 에 위 8개가 보인다.

DA_ARSession 에 넣기 (둘 중 하나)
---------------------------------
방법 A (자동): 현재 develop 의 `DA_ARSession` 은 이미 CandidateImages 목록에
  Cand_EX1_A2 / Cand_EX2 / Cand_EX3 / Cand_EX4 를 경로로 참조하고 있다.
  위처럼 파일만 제 위치에 놓으면 그 깨져 있던 참조가 자동으로 되살아난다.
  → 별도 작업 없이 바로 인식된다.
방법 B (수동): DA_ARSession 을 열고 CandidateImages 배열에 Cand_EX1_A2·
  Cand_EX2·Cand_EX3·Cand_EX4 를 직접 추가한다.

공룡 연결
---------
ARTrackingManager 가 `MarkerCode = GetFriendlyName()` → `DinoRegistry.FindByMarker`
로 분기하므로(대소문자 무시), DA_DinoRegistry.Species 에서 위 code(EX1-TRICERATOPS 등)를
각 DA_Dino_* 에 매핑하면 그 마커 위에 해당 공룡이 뜬다.

참고
----
- EX1 은 인쇄본이 두 판(_A2 / _Old)이라 DA_ARSession 에 Cand_EX1_Old 참조가
  남아 있을 수 있다. 여기엔 최종본인 _A2 만 넣었다. _Old 참조가 거슬리면
  DA_ARSession 의 CandidateImages 에서 그 항목만 지우면 된다(같은 지점·같은 code).
- 물리 인쇄 배율은 candidate 에셋에 이미 들어 있다(별도 설정 불필요).
- 시작점 마커(MARK-NEUTI4-START, A-1)는 네비 측위용이라 전시물 4종에는 뺐다.
