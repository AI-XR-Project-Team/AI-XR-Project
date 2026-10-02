# 정보창 및 렉시 UI 피드백 반영 (2026-09-14)

- 아르켈론 정보창의 내비게이션/AR 스캔/도슨트 하단 바 제거. 뒤로 및 메뉴로 복귀한다.
- 정보창을 여는 동안 기존 스캔 HUD를 숨기고 닫을 때 이전 visibility를 복원한다.
- 합성 바다 배경 대신 현재 AR 뷰에 BlurStrength 3의 BackgroundBlur와 반투명 색을 얹는다. Android OpenGL의 backbuffer sampling을 활성화했다. 실제 기기에서 블러 강도 및 성능 확인은 별도 필요하다.
- 제목과 본문을 동일한 스크롤 콘텐츠로 묶고, 콘텐츠 최소 높이를 스크롤 영역에 맞춰 짧은 글은 세로 중앙 정렬, 긴 글은 스크롤한다.
- 코드 경로로 로드하는 버튼/렉시 이미지가 cook에서 빠지던 문제 해결: DefaultGame.ini의 ProjectPackagingSettings에 /Game/UI를 DirectoriesToAlwaysCook로 추가. 기존 로컬 skip-worktree 설정 때문에 이 파일은 git diff에 나타나지 않을 수 있으므로 인계 시 주의한다. 서버 주소나 git 설정은 변경하지 않았다.
- 렉시 화면의 구형 Glow/보라색 Ring 중첩 제거, 기본 렉시와 수평 청록 링 사용. 기존 불투명 HeaderBG/InputBG/ChatBackdrop을 유리 스타일로 교체하고 상단 버튼 크기 및 제목 정렬 조정.
- 기존 클릭 정보창 수정(SkinHost)은 유지했다.

검증: 정보창 LiveSkinSwitch 및 SkinPreview 성공. 도슨트 SkinPreview(시작/대화/메뉴) 성공. 최종 로그는 Saved/Logs/DocentRestoreTests.log. Cook 결과에 Docent/Skin 29개 텍스처와 T_BtnBack/T_BtnMenu 포함 확인.

최종 APK 위치: TimeMachineAR/Saved/FeedbackBuild/Android_ASTC/TimeMachineAR-arm64.apk.
렌더는 카메라가 없는 에디터에서 수행되므로 실기기 카메라 배경의 블러/가독성과 동일하다고 단정하지 않는다.
