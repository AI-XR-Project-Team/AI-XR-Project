# 네비게이션 서버 통합 가이드 (API 계약서)

> **대상**: 팀원2(앱↔서버 연결/통합) 및 네비게이션 기능 구현자.
> **목적**: 앱이 네비게이션 서버와 주고받는 **계약(엔드포인트·DTO·좌표 규약·호출 시퀀스)** 을 확정한다.
> 서버는 **측위 비종속** — 맵 좌표만 입출력한다. QR/ARCore/VPS 등 측위 방식은 서버가 모른다.

---

## 0. 핵심 원칙

- **경계 = 맵 좌표(UE5 Z-up 왼손, cm) + heading.** 이 좌표만 앱↔서버를 오간다.
- 앱은 "지금 맵의 (x,y) 위치, θ 방향을 보고 있다 → 목적지 N까지" 를 서버에 던지고, 서버는 "맵 좌표 경로 + 안내 문구" 를 돌려준다.
- **도착/이탈 판정은 앱**이 한다. 서버는 물으면 경로를 계산해줄 뿐. (이탈 감지=앱, 재계산=서버)

---

## 1. 좌표·단위 규약

| 항목 | 값 |
|---|---|
| 좌표계 | **UE5 Z-up 왼손** |
| 단위 | **cm** |
| 축 | +X, +Y 수평 / +Z 위 |
| heading | **+X축 기준 CCW(반시계) 도(°)** |
| 예 | `heading_deg=90` = +Y 방향을 봄 |

> 이 규약이 UE5 월드와 동일하므로, UE 앱에선 맵↔월드 변환이 단순하다(축 리맵 불필요).

---

## 2. 엔드포인트 (5개, 전부 익명·인증 없음)

Base URL: `http://<서버호스트>:<포트>/` (환경별. 예: 로컬 `http://<PC-IP>:8000/`)

### 2.1 `GET /maps/{id}` — 맵 메타

**응답**
```json
{ "id": "uuid", "name": "느티나무 4층 테스트 유닛",
  "coord_system": "ue5_zup_cm", "origin_note": "원점=... 설명" }
```

### 2.2 `GET /maps/{id}/markers` — 마커(QR) 목록

측위용. 각 마커의 code ↔ 맵 pose·heading. 앱은 QR에서 얻은 code로 여기서 pose를 찾는다.

**응답** (배열)
```json
[{ "code": "MARK-NEUTI4-START", "marker_type": "qr",
   "pos_x_cm": 130, "pos_y_cm": 145, "pos_z_cm": 0,
   "heading_deg": 76, "note": "바닥부착 출발점 ..." }]
```

### 2.3 `GET /maps/{id}/destinations` — 목적지 목록

목적지 선택 UI용.

**응답** (배열)
```json
[{ "node_id": "uuid", "label": "주방", "node_type": "facility" }]
```

### 2.4 `POST /navigation/route` — 최단 경로 + 안내

**요청**
```json
{ "map_id": "uuid",
  "from": { "pos_x_cm": 130, "pos_y_cm": 145, "pos_z_cm": 0, "heading_deg": 76 },
  "to":   { "node_id": "uuid" },
  "accessible_only": false }
```
- `from` : 현재 위치 pose(맵 좌표). **JSON 키는 `from`**. `heading_deg`는 선택(없으면 출발 회전 안내 생략).
- `to` : **`node_id`** 또는 좌표 3개(`pos_x_cm/y/z`) 중 하나. 둘 다 없으면 검증 오류.
- `accessible_only` : true면 계단 등 제외(접근성 경로).

**응답**
```json
{ "total_distance_cm": 1009.9,
  "waypoints": [
    { "node_id": "uuid", "pos_x_cm": 130, "pos_y_cm": 145, "pos_z_cm": 120, "node_type": "entrance" },
    { "node_id": "uuid", "pos_x_cm": 282, "pos_y_cm": 756, "pos_z_cm": 120, "node_type": "junction" },
    { "node_id": "uuid", "pos_x_cm": 651, "pos_y_cm": 664, "pos_z_cm": 120, "node_type": "facility" }
  ],
  "steps": [
    { "instruction": "앞으로 6m 직진하세요", "distance_cm": 629.6, "turn": "straight", "arrive": false },
    { "instruction": "우회전하세요",        "distance_cm": null,  "turn": "right",    "arrive": false },
    { "instruction": "앞으로 4m 직진하세요", "distance_cm": 380.3, "turn": "straight", "arrive": false },
    { "instruction": "편의시설에 도착했습니다", "distance_cm": null, "turn": null,     "arrive": true }
  ] }
```
- `waypoints` : 출발 스냅 노드 → 목적지, 순서대로. 앱은 이걸 월드로 변환해 경로/화살표 렌더.
- `steps` : 사람이 읽는 안내. `turn` ∈ `left|right|straight|null`(도착), `arrive`=도착이면 true.
- `distance_cm` : 직진 step의 이동 거리(회전/도착엔 null).

### 2.5 `POST /navigation/reroute` — 이탈 시 재계산

**요청/응답 스키마는 `/navigation/route`와 동일.** 앱이 이탈을 감지하면 새 `from`으로 호출.

---

## 3. 에러

| 코드 | 의미 |
|---|---|
| `404` | map_id 없음 / 이 맵에 네비 그래프 없음 / 목적지 노드가 이 맵에 없음 |
| `422` | 목적지까지 연결된 경로 없음 (`to` 검증 실패 포함) |

---

## 4. 권장 앱측 인터페이스 (연결 계층 시그니처)

팀원2는 아래 시그니처 뒤(HTTP)를 채우고, 기능 구현자는 앞(pose 생성·렌더)을 채운다. **이 경계가 연결↔기능의 계약.**

```
getMarkers(mapId)              -> List<Marker>
getDestinations(mapId)         -> List<Destination>
requestRoute(fromMapPose, to)  -> Route     // {total_distance_cm, waypoints, steps}
reroute(fromMapPose, to)       -> Route
```
- DTO 필드명은 위 §2 JSON과 **1:1**이어야 한다(서버 Pydantic 스키마 기준).
- `fromMapPose` = 앱의 측위·좌표변환이 만들어 넣는다(연결 계층은 값을 모른다).

---

## 5. 호출 시퀀스 (한 사이클)

```
[앱] 마커 스캔 → getMarkers 로 code의 맵 pose 조회 → 맵↔월드 변환 T 수립   (측위)
[앱] 목적지 선택 → getDestinations
[앱→서버] requestRoute(from=현재 맵 pose, to=목적지 node_id)
[서버→앱] waypoints + steps
[앱] waypoints를 T로 월드 변환 → 경로 렌더 + steps 표시
[앱] 걷는 중 카메라 위치 → T로 맵 좌표 → 도착/이탈 판정
[앱→서버] (이탈 시) reroute(from=새 맵 pose, to)
```

---

## 6. 서버 운영 참고 (연결과 무관하지만 알아두면 좋음)

- 기동: `uvicorn app.main:app --host 0.0.0.0 --port 8000`
- 시드 변경 반영: **DB 해당 맵 노드/엣지 삭제 → 재시드 → uvicorn 재시동(그래프 캐시 리셋)**. 노드 삭제-재삽입 시 **node_id(UUID)가 새로 생기므로**, 앱은 destinations를 다시 받아야 한다(캐시 갱신).
- 상세 구현: 서버 코드 `backend_server/app/routers/navigation.py`, `backend_server/app/schemas/navigation.py` 참조.
