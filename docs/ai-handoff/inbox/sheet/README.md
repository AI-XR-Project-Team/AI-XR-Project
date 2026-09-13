# 시트에서 잘라낸 UI 에셋

ChatGPT 가 한 장으로 준 에셋 시트를 알파 채널 기준으로 자동 분할한 결과다.
원본 시트는 사람이 바탕화면에 받은 파일이고, 분할은
`tools/slice_sprite_sheet.ps1` 이 한다.

## 분할 방식

시트는 배경이 완전 투명(A=0)이고 스프라이트만 불투명하다. 그래서

1. 알파 170 이상만 남긴 마스크에서 연결 요소(8-이웃)를 찾는다
2. 4px 이내로 붙어 있는 덩어리는 하나로 합친다 (글로우 조각이 떨어져 나오는 것 방지)
3. 각 덩어리 경계에 12px 여백을 주고 잘라낸다

여백을 주되 **낮은 알파로 다시 트림하지 않는다.** 시트가 스프라이트마다
아래에 흐린 캡션(파일명)을 찍어 두었는데, 낮은 임계값으로 트림하면 그
글자까지 크롭에 딸려 들어온다.

행/열 투영 방식은 쓸 수 없었다. 글로우가 번져서 1283행 중 1159행에 내용이
잡혀 분리가 안 된다.

## 목록

| 파일 | 용도 |
|---|---|
| `lexi_pointing` `lexi_idle` `lexi_happy` `lexi_question` `lexi_wink` | 도슨트 렉시 표정 5종 |
| `bubble_speech` `bubble_textbox` | 말풍선 배경 (9-slice 로 늘려 쓸 것) |
| `map_floorplan` | 층 도면 |
| `poi_triceratops` `poi_brachiosaurus` `poi_ankylosaurus` `poi_trex` | 지도 전시물 마커 |
| `poi_toilet` `poi_entrance` | 지도 편의시설 마커 |
| `btn_*` (6종) | 하단 목적지 버튼 완성본 |
| `btn_state_normal` `btn_state_selected` `btn_state_disabled` | 버튼 상태 배경 |
| `btn_close` `selector_floor` | 상단 UI |
| `dot_orange` `dot_blue` `dot_green` | 위치 점 |
| `prop_stairs` `prop_door` `prop_rocks` `prop_fossil` `prop_plant` | 지도 소품 |
| `prop_sparkle_a~d` `prop_question` `prop_dot` `prop_glow` | 장식 소품 |
| `bg_overlay` | 배경 오버레이 |

## 주의

- **`btn_*` 6종은 글자가 박혀 있다.** 앱은 목적지 이름을 서버에서 받아
  그리므로 이 이미지를 그대로 쓰면 안 된다. 배경만 필요하면
  `btn_state_*` 를 쓰고, 이 6종은 디자인 참고용이다.
- `selector_floor` 는 층 선택기인데 이 박물관은 단층이라 쓰지 않는다.
- `map_floorplan` 은 서버 nav 그래프 좌표와 픽셀 정렬이 맞지 않는다.
  배경으로 깔려면 스케일·오프셋 보정이 필요하다.
