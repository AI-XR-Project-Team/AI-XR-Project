# 원본 UI 아트워크 적용

사용자가 첨부 이미지와 같은 UI를 요청함에 따라, 기존 네이티브 도형·텍스트 기반 근사 구현을 이미지 리소스 기반의 목적지 선택 화면으로 교체한다. 후속 요청에 따라 사진 배경을 분리하고 실제 카메라 블러를 적용했다.

## 시각 기준
- 원본: `Asset/NavReference/T_MuseumReference.png` (941×1672).
- Unreal 리소스: `/Game/UI/Nav/Reference/T_MuseumReference`.
- 현재 사용하는 투명 전경: `Asset/NavReference/T_MuseumSimple.png`, `/Game/UI/Nav/Reference/T_MuseumSimple` (941×1672 RGBA).
- 렉시는 후속 요청에 따라 기존 `T_MuseumForeground`의 입체적인 원본 모습으로 복원했다. UMG 영역 브러시로 렉시만 따로 표시하며, 단순화된 지도·말풍선·카드와 카메라 블러는 유지한다.
- 최신 요청(9월 16일): 클래식 렉시 적용을 되돌리고 `T_MuseumForeground`의 기존 입체 렉시를 사용한다. 렉시와 안내 메시지를 한 그룹으로 묶어 화면 상단(위 12, 왼쪽 148, 오른쪽 16, 높이 220 UMG 단위)에 표시한다. 왼쪽 뒤로가기와 겹치지 않으며 지도 비율과 독립적으로 배치한다.
- 정렬 보정: 그룹은 뒤로가기 하단(128)과 실제 표시된 지도 제목 상단 사이의 중앙에 놓는다. 화면 비율에 따라 간격을 계산하며 최대 높이는 220이다. 메시지 패널은 렉시와 같은 265 디자인 단위 높이로 만들고 텍스트를 따로 중앙 배치해 글자가 세로로 늘어나지 않는다.
- 가로 확장: 렉시를 왼쪽 여백 16에 맞추고, 나머지 가로 공간을 꼬리가 달린 말풍선으로 채운다. 디자인 너비를 화면 비율에 맞춰 계산해 렉시 비율을 유지한다. 안내 제목은 40, 보조 문구는 27 디자인 포인트로 확대했다.
- 전체 앱과 통일해 달라는 후속 요청을 반영해 네온/광택/무지개 구역 색을 제거했다. 어두운 패널, 흰색 아이콘, 파란 포인트로 통일하고 슬로건/페이지 점/축척 장식을 제거했다.
- 전경 아래에 UBackgroundBlur(강도 8)와 반투명 틴트(알파 0.28)를 배치한다. AR 카메라는 기존 세션을 그대로 사용한다.
- 뒤로가기는 화면 왼쪽 상단 (16,16)에 112×112 UMG 단위로 고정한다. 지도 확대/축소와 독립적이다.
- 원본 렉시, 말풍선, 박물관 제목, 구역별 색상, 네온 선, 화석/광물 그림을 사용한다.
- UMG 이미지 영역 브러시로 아트워크를 표시하고 실제 버튼과 스크롤 입력을 연결한다. 원본 PNG를 다시 그리거나 화석 그림을 한글 기호로 대체하지 않는다.
- 시안에 포함된 가짜 상태 표시줄은 제외하고, 실제 축척이 아닌 5 m 표시는 안내도 표기로 처리한다.

## 좌표 분리
- 이 화면은 **일러스트 목적지 선택 지도**다. 이미지 좌표는 터치 판정에만 사용한다.
- 1~13번은 `FNavDestinations::DisplayNumber`로 실제 서버 노드 ID에 연결한다.
- 실제 길찾기 좌표/거리/벽/경로는 museum_final 그래프를 유지한다. 이미지에서 임의의 실측 거리나 AR 위치를 계산하지 않는다.
- 숨긴 기존 MapView는 실시간 graph/pose/route setter 호환을 위해 유지한다.
- 서버 데이터가 없으면 그림은 보여도 목적지 선택은 실행하지 않는다. 비어 있는 곳을 눌러도 닫히지 않는다.

## 재현
`Scripts/import_museum_reference.py`를 Unreal Python commandlet으로 실행하면 원본 PNG를 UI 텍스처로 가져온다. `/Game/UI` 폴더는 기존 패키징 설정에서 항상 포함된다.

원본 화면의 지도 그림은 사용자가 제공한 시각 기준이며, 현장 AR 정합이나 전시물 실측 위치를 새로 검증했다는 의미가 아니다.

## 배경 분리 생성 기록

- 도구: imagegen 스킬의 내장 image_gen, background-extraction 편집.
- 입력: 사용자 원본 이미지. 결과: 941×1671 RGBA, 알파 0~255, 완전 투명 픽셀 506,004개. 원본은 별도 보존했다.
- 최종 프롬프트: “Use case: background-extraction. Edit target: attached 941x1672 museum navigation UI. Produce a genuinely transparent RGBA PNG UI overlay. Remove ONLY the photographic museum/hall/bokeh background around and behind the UI. Preserve the exact composition, coordinates, canvas aspect ratio, Korean text, all 13 numbered exhibits, room boundaries, neon outlines, original robot Lexi at upper left, speech panel, title, footer text, dots and bottom cards. Do not redesign or rearrange anything. Keep the floor-plan panel from x50 y365 to x890 y1200 and the bottom specimen cards intact as foreground UI, and preserve robot silhouette and speech bubble. All outside background including spaces between robot, heading, map and cards MUST be actual alpha transparent, not black, not checkerboard painted into image. Remove fake top phone status icons/time. Keep UI foreground as close to the original pixels as possible. This asset will be composited over a real live camera, do not generate replacement scenery.”
- 가져오기: `Scripts/import_museum_foreground.py`.

## 최종 단순화 편집

### 렉시 독립 스프라이트

- 내장 image_gen 편집. 저장 경로: `Asset/NavReference/T_LexiClassic.png`. 가져오기: `Scripts/import_lexi_classic.py`.
- 프롬프트: “Extract ONLY the original waving Lexi robot at the TOP LEFT of this image as a standalone transparent RGBA PNG sprite, tightly framed with 5% padding. No UI, no speech bubble, no text, no museum map. Keep precisely its recognizable design: rounded white silver head, dark glass face, two cyan blue eyes, smile, blue antenna orb, waving left-side hand and upper body. User wants the original robot with only a SLIGHT classic graphic treatment: gently reduce glossy reflections and neon bloom, subtly simplified smooth shading like a polished classic game mascot. Retain volume and original identity; NOT pixel art, NOT flat vector, NOT a redesign. White/silver and subdued blue palette, crisp clean edges, restrained lighting, friendly original expression. Upper body bust with clean finished lower edge. True transparent alpha background, no painted checkerboard or black backdrop.”

- 도구: 내장 image_gen. 입력: 사용자 원본. 저장: `Asset/NavReference/T_MuseumSimple.png`. 가져오기: `Scripts/import_museum_simple.py`.
- 최종 프롬프트: “Edit target: attached museum navigation UI. Create a restrained, simple production app foreground PNG with genuine transparent background. The user finds this much too flashy and wants consistency with a calm dark navy mobile app. Keep exact canvas aspect ratio and EXACT positions/room outline geometry of floor plan and all numbered 1-13 exhibit markers. Preserve all Korean destination names. Replace neon glow, electric halos, vivid gradients, sparkles, shiny materials and thick illuminated borders with thin muted blue-gray lines, flat semiopaque dark navy rooms, quiet white text, and small muted blue accent markers. Fossil/mineral illustrations become simple small clear monochrome pictograms in the SAME center locations. Floor plan stays at x50 y365 through x890 y1200 relative to original 941x1672. Keep Lexi in top left as a small modest friendly robot without glow, speech panel remains at original location but use plain rounded charcoal panel with the text 도착지를 골라주세요! and smaller 원하는 전시물을 선택하세요. The only title is 경주 자연사 박물관 centered above map, remove all English slogans and decorative footer, pagination dots, time/status icons, North/5m decoration. Bottom 3 featured cards stay at x55..329, x336..609, x615..887 and y1232..1488, but become simple dark rounded rectangles with thin subtle border, small white pictogram and label 자수정, 삼엽충, 아르켈론 and their numbers 1,3,5. Remove all decorative card prose, glow and photographic renders. All spaces outside the room plan, robot, speech bubble, title and cards must be actual transparent alpha. No photographic background. No checkerboard painted into the image. No layout changes in floor plan: exact walls, all 13 original marker positions mandatory. Overall aesthetic: quiet, functional, restrained museum wayfinding app.”
