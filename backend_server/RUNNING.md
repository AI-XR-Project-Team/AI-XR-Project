# 백엔드 서버 가동 가이드

AI 도슨트 · 네비게이션 FastAPI 서버(`backend_server`)를 로컬에서 띄우고, 실기기(폰)
앱이 붙게 하는 절차. 발표·시연 당일 이 문서만 보고 그대로 따라 하면 된다.

---

## 0. 사전 준비 (최초 1회)

- **PostgreSQL** 이 로컬에서 돌고 있어야 한다. 기본 접속 정보(코드 기본값):
  `postgresql+psycopg2://dino:dino_dev_pw@localhost:5432/dino_ar`
  스키마(테이블)는 `feature/db-postgres-setup` 이 진실 소스다. DB·테이블이 아직
  없으면 그 절차로 먼저 만든다. 맵/전시물 UUID 는 `gen_random_uuid()` 로 생성된다.
- **파이썬 가상환경** `.venv` (리포에 이미 있음). 없으면:
  ```bash
  cd backend_server
  python3 -m venv .venv
  .venv/bin/pip install -r requirements.txt
  ```
- **`.env`** (`.gitignore` 대상 — 커밋 금지). LLM 을 실제로 쓰려면:
  ```
  LLM_PROVIDER=gemini
  LLM_API_KEY=<Google AI Studio 키>
  ```
  생략하면 `LLM_PROVIDER=mock` 으로 떠서 API 키 없이도 계약·화면 검증은 된다
  (도슨트가 실제 해설 대신 `[MOCK]` 응답을 준다). 기본 모델은 `gemini-3.6-flash`.

---

## 1. 서버 기동

```bash
cd backend_server
.venv/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

- **`--host 0.0.0.0` 이 필수다.** 안 주면 `127.0.0.1` 만 리슨해서 **폰에서 안 붙는다**
  (서버는 뜨는데 앱만 "연결 실패"로 보이는 전형적 원인).
- 기동 확인: 로그에 `Application startup complete.` / `Uvicorn running on http://0.0.0.0:8000`.
- 헬스 체크(같은 PC): <http://localhost:8000/health>, API 문서: <http://localhost:8000/docs>.

---

## 2. 시드 데이터 (초기 적재 · 재시드)

전시물·공룡·POI:
```bash
cd backend_server
.venv/bin/python scripts/seed.py
```
맵·노드·엣지·마커·전시물배치(네비):
```bash
.venv/bin/python scripts/seed_nav.py
```
- 둘 다 **자연키 기준 멱등**(이름·라벨·code)이라 여러 번 돌려도 안전하다.
- **재시드해도 앱을 다시 빌드할 필요 없다.** 도슨트는 전시물을 UUID 가 아니라
  안정 키(`dinosaurs.model_asset_key`, 예 `trex_full_skeleton`)로 조회하도록
  고쳤기 때문이다. (예전엔 앱에 exhibit UUID 를 박아 둬서, 재시드로 UUID 가
  바뀌면 도슨트가 "연결 실패 / 어떤 공룡인지 모름"으로 깨졌다. 자세한 건 §5.)

---

## 3. 폰(앱)이 이 서버에 붙게 하기

앱(`TimeMachineAR`)은 `TimeMachineAR/Config/DefaultGame.ini` 의 `ServerBaseUrl` 로
서버를 찾는다. **폰과 이 PC 가 같은 Wi‑Fi 여야 하고, PC 의 LAN IP 를 넣어야 한다.**

1. 이 PC 의 IP 확인 (macOS):
   ```bash
   ipconfig getifaddr en0
   ```
2. `DefaultGame.ini` 두 섹션의 값을 그 IP 로 (따옴표째로 — 따옴표 없으면 UE 파서가
   `//` 뒤를 주석 처리해 값이 `http:` 가 된다):
   ```
   [/Script/TimeMachineAR.DocentClient]
   ServerBaseUrl="http://<이 PC IP>:8000"
   ```
   NavClient 섹션은 비워 두면 위 값으로 폴백한다(IP 는 한 군데만 고치면 됨).
3. `DefaultGame.ini` 의 `DefaultMapId` 는 이제 **머신과 무관하게 고정**이다.
   `seed_nav.py` 가 `map_key` 로부터 결정적으로 유도하기(uuid5) 때문에, 어느 PC 에서
   시드해도 `neuti4f`(느티나무 4층 확장) 맵은 항상 아래 UUID 다:
   ```
   [/Script/TimeMachineAR.NavClient]
   DefaultMapId=385f15c8-bf2c-58ec-a2d7-8db2aa34ac0b
   ```
   따라서 팀원은 이 값을 손댈 필요가 없다(예전엔 `gen_random_uuid()` 라 머신마다
   달라 여기 값이 남의 DB 것이면 네비가 전부 404 로 죽었다). 값을 직접 확인하려면
   `SELECT id, name FROM map_spaces;`. 다른 맵의 고정 UUID:
   `floor1=1cdb729e-0e7f-555c-99cf-6b83eb973e7b`,
   `neuti4=341e5556-333e-5dc1-b43b-5df649a830bd`.
4. **IP 를 바꿨으면 앱을 재빌드·재설치해야 한다** (APK 안에 쿡되어 들어간다).
   빌드 레시피는 별도 문서/메모 참고.
5. cleartext HTTP 는 앱 전역으로 허용돼 있다(`DefaultEngine.ini`
   `usesCleartextTraffic='true'`) — 별도 도메인 화이트리스트 불필요.

---

## 4. 안 붙을 때 체크리스트 (폰에서 "연결 실패")

증상별로 위에서부터 확인한다. **네비는 되는데 도슨트만 안 되면 §5 로.**

1. 서버가 `--host 0.0.0.0` 으로 떠 있나? (`127.0.0.1` 이면 폰이 못 붙는다)
2. 폰과 PC 가 같은 Wi‑Fi 인가? PC 방화벽이 8000 을 막지 않나?
3. `DefaultGame.ini` 의 IP 가 **지금** PC IP 와 같나? (DHCP 로 IP 가 바뀌면 깨진다)
   IP 를 바꿨다면 앱 재빌드했나?
4. 폰 브라우저로 `http://<PC IP>:8000/docs` 가 열리나? (열리면 네트워크는 정상,
   문제는 앱 설정)
5. USB 로 연결돼 있으면 `adb reverse tcp:8000 tcp:8000` 로 우회 가능.

---

## 5. 도슨트만 안 될 때 — 전시물 키(exhibit_key)

도슨트는 세션을 열 때 전시물을 **안정 키**로 지정한다(앱이 `exhibit_key` 를 보냄,
서버가 `dinosaurs.model_asset_key` 로 해석). 현재 키:

| 공룡 | model_asset_key |
|---|---|
| 티라노사우루스 | `trex_full_skeleton` |
| 트리케라톱스 | `triceratops_full_skeleton` |
| 안킬로사우루스 | `ankylosaurus_full_skeleton` |
| 브라키오사우루스 | `brachiosaurus_full_skeleton` |

- 서버가 이 키를 못 찾으면 세션 생성이 **404** 다. 즉 시드의 `model_asset_key` 와
  앱 DataAsset(`DA_Dino_*`)의 `ExhibitKey` 가 **글자까지 같아야** 한다.
- 빠른 점검(같은 PC에서):
  ```bash
  curl -s -X POST http://localhost:8000/docent/sessions \
    -H 'Content-Type: application/json' \
    -d '{"device_uuid":"test","exhibit_key":"trex_full_skeleton"}'
  ```
  정상이면 `{"session_id":"…","exhibit_id":"…"}` 로 `exhibit_id` 가 채워진다.
  `exhibit_id":null` 이면 **서버가 옛 코드**다 → 재기동(§1)해서 새 코드로 올린다.
  `404` 면 시드가 안 됐거나 키가 다른 것 → §2 재시드 또는 키 확인.

---

## 참고: 자주 쓰는 값

- DB: `dino_ar` (user `dino` / pw `dino_dev_pw`, localhost:5432)
- 포트: `8000`, LLM: `gemini-3.6-flash` (`.env` 에서 `LLM_PROVIDER=gemini`)
- 테스트: `.venv/bin/python -m pytest`
