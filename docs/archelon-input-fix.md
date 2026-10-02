# 아르켈론 정보창 입력 수정 — 2026-09-13

증상: 마커 인식 후 모델을 터치해도 새 정보창이 보이지 않고 화면 전환 입력도 막힘.

확인한 코드 결함: `BuildOceanSkin`은 `WidgetTree->RootWidget`을 교체하지만 `TakeWidget()`은 기존 Slate를 캐시한다. 뷰포트/현재 입력 경로가 그 Slate를 유지하면 `RemoveFromParent` + `AddToViewport`만으로 새 루트가 반영되지 않는다. 숨겨진 이전 화면을 유지한 채 사용자 위젯이 Visible이 되는 경로가 있다.

수정: `UDinoInfoCardWidget::RebuildWidget`에서 안정적인 SBox 컨테이너를 만들고 스킨 변경 시 해당 컨테이너의 자식 콘텐츠를 현재 루트로 바꾼다. 기존 뷰포트 참조에서도 새 화면과 hit-test 트리가 함께 적용된다. 디자인 및 Blueprint 이벤트 연결은 보존했다.

검증:
- TimeMachineAREditor Win64 Development 빌드 성공.
- Android Development ASTC BuildCookRun 성공.
- `TimeMachineAR.DinoCard.LiveSkinSwitch` 성공: 먼저 Slate를 생성·보유한 상태에서 정보창 열기, 탭 변경, 닫기, 다시 열기를 검사. 기존 프리뷰와 달리 앱 시작 후 첫 터치의 수명주기를 재현한다.
- `TimeMachineAR.DinoCard.SkinPreview` 성공: 네 탭 및 기존 기본 스킨 렌더.
- 로그: `TimeMachineAR/Saved/Logs/DinoCardInputFixTests.log`.
- APK: `TimeMachineAR/Saved/InputFixBuild/Android_ASTC/TimeMachineAR-arm64.apk`.
- 연결된 Android 기기에 `adb install -r` 성공. 실제 마커 터치와 다른 화면 이동은 사용자 확인 대기.

수정 전 휴대폰 logcat에서는 해당 클릭 이벤트를 확보하지 못했다. 따라서 코드 결함의 수정/테스트 통과와 실기기 최종 동작 확인은 구분한다.
