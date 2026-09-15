# museum_final 전환 결과

## 적용 데이터
- 실제 지도 UUID: `5c31f7fc-ef7d-53e7-a4c3-6f5902f4aad9`.
- 원본 museum_final 외곽선, 장애물, 38개 노드 좌표와 38개 연결을 보존했다.
- 기존 구역 안내 지점에 확정된 13개 이름을 연결했다. 개별 전시물의 신규 실측 좌표가 아니다.
- 대응표: `backend_server/seeds/nav/museum_final_destinations.csv`.
- 일반 시드의 `nav_nodes.csv`도 같은 이름/분류로 수정해 재시드 시 옛 이름으로 돌아가지 않게 했다. 실험용 지도 행은 유지했다.
- `scripts/seed_museum_final_destinations.py`는 해당 지도만 생성/갱신하는 트랜잭션이다. 다른 지도·마커·전시 모델을 수정하지 않는다. 실행을 반복해도 노드·연결 수는 유지된다.

## 앱
- DefaultGame.ini 기본 지도를 museum_final로 전환했다. 이 파일에는 기존 skip-worktree 설정이 있어 일반 git status에 나타나지 않는다. 해당 설정은 변경하지 않았다.
- 표시 번호 1~13을 고정하고 출구를 분홍색으로 구별했다. 이름이 바뀌어도 옛 공룡 그림이 잘못 붙지 않도록 박물관 목적지는 번호/텍스트 기호로 표시한다.
- 픽셀 이미지의 장식용 5 m를 실제 치수로 사용하지 않았다.

## 검증
- 실제 API: 38개 노드, 38개 연결, 13개 목적지. 입구에서 나머지 12곳으로의 경로 요청 성공.
- 시드를 두 번 실행해 수량 유지 확인.
- 13개 안내 지점 모두 외곽선 안이며 장애물 내부에 위치하지 않음.
- 기존 연결마다 101개 지점을 표본 검사해 외곽선 이탈/장애물 내부 통과가 없음을 확인. 현장 보행 검증을 대체하는 정밀 실측 검사는 아니다.
- Unreal 자동 테스트 `TimeMachineAR.Nav.Destinations`, `TimeMachineAR.Nav.SkinPreview` 통과. 실제 박물관 시드로 13개 이름·순서·터치 가능 영역 확인.
- 테스트 스크립트: `Scripts/verify_museum_final.py`.
- Android Shipping 패키징 성공 후 연결된 SM-S936N에 업데이트 설치 완료. 실기기에서 museum_final 평면도와 13개 목적지 표시 확인. 캡처: `Saved/museum_final_phone.png`.

## 현장 한계
- museum_final에는 실제 현장 QR/cloud anchor 좌표가 아직 적재되지 않았다. 실험용 마커를 이전하지 않았다. AR 위치 기반 길 안내를 현장에서 사용하려면 기준점 등록과 정합 검증이 필요하다.
- 이미지의 정밀 화석 일러스트를 신규 제작한 작업은 아니다. 현재 시각 표시는 기존 UI에 번호/텍스트 기호를 적용한 상태다.
