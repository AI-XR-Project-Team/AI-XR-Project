# 서버 로컬 실행 가이드

내 PC 에서 백엔드(FastAPI + PostgreSQL)를 띄우는 순서다. 위에서부터 그대로 따라가면 된다.

이 문서는 **서버만** 다룬다. UE5 빌드·안드로이드 툴체인·패키징은
`Documents/RUNBOOK-docent-chat.md` 를 본다.

---

## 준비물

| 항목 | 비고 |
|---|---|
| Docker Desktop | PostgreSQL 컨테이너용. 실행 중이어야 한다 |
| Python 3.11 | 3.11.9 에서 검증했다. requirements 에 버전 고정이 없어 3.10~3.12 면 대체로 된다 |
| Gemini API 키 | **선택.** 없어도 `mock` 으로 UI·스트리밍까지 다 확인된다 |

작업 디렉터리는 전부 `backend_server/` 기준이다.

```bash
cd backend_server
```

---

## 1. .env 만들기

```bash
cp .env.example .env
```

`.env` 에서 LLM 을 고른다. 다른 값은 건드릴 필요 없다.

| 목적 | 설정 |
|---|---|
| 키 없이 UI·스트리밍만 확인 | `LLM_PROVIDER=mock` (기본값) |
| 실제 응답을 받고 싶다 | `LLM_PROVIDER=gemini` + `LLM_API_KEY=<발급받은 키>` |

키는 https://aistudio.google.com/apikey 에서 받는다. 무료 티어가 있고 카드 등록이 필요 없다.

> **키를 `.env` 밖으로 내보내지 말 것.** `.env` 는 `.gitignore` 대상이지만
> `.env.example` 은 커밋된다. 거기에 키를 적으면 저장소에 그대로 올라간다.
> 슬랙·이슈·스크린샷에 붙여 넣는 것도 마찬가지다. 키는 각자 발급해서 쓴다.

---

## 2. DB 띄우기

```bash
docker compose up -d
docker compose ps
```

`STATUS` 가 `healthy` 가 될 때까지 10~20초 걸린다. `health: starting` 이면 아직이다.

테이블이 생겼는지 본다. 7개여야 한다.

```bash
docker exec dino_ar_db psql -U dino -d dino_ar -c "\dt"
```

```
users, dinosaurs, exhibits, pois, view_logs, chat_sessions, chat_messages
```

`chat_sessions` 가 없다면, **이 기능이 생기기 전에 만든 DB 볼륨이 이미 있는 경우**다.
`db/init/*.sql` 은 볼륨이 처음 만들어질 때 한 번만 실행되기 때문이다. 그때만 이걸 돌린다.

```bash
.venv\Scripts\python.exe scripts\apply_chat_schema.py
```

---

## 3. 가상환경과 의존성

```bash
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

---

## 4. 시드 데이터 넣기

```bash
.venv\Scripts\python.exe scripts\seed.py       # 공룡 1종 + 전시물 1개 + POI 3개
.venv\Scripts\python.exe scripts\seed_nav.py   # 네비게이션 맵·노드·마커
```

둘 다 멱등이라 여러 번 돌려도 중복되지 않는다.

---

## 5. 서버 기동

붙이는 방식에 따라 host 가 다르다.

```bash
# 실기기를 USB(adb reverse)로 붙일 때 — 권장
.venv\Scripts\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8000

# 같은 공유기의 다른 기기에서 LAN 으로 붙을 때
.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

이 창은 켜 둔 채로 둔다. 끄면 서버가 내려간다.

---

## 6. 동작 확인

```bash
curl http://127.0.0.1:8000/health       # {"status":"ok"}
curl http://127.0.0.1:8000/dinosaurs    # 공룡 목록
```

**`/health` 만 보고 정상이라고 판단하면 안 된다.** `/health` 는 DB 를 건드리지 않는다.
`/dinosaurs` 까지 나와야 DB 경로가 살아 있는 것이다. 여기서 500 이 나면 2번으로 돌아간다.

API 문서는 http://127.0.0.1:8000/docs 에서 볼 수 있다.

---

## ⚠️ 전시물 UUID 는 PC 마다 다르다

`scripts/seed.py` 가 UUID 를 고정하지 않아 **머신마다 전시물 ID 가 다르게 생긴다.**
클라이언트에 박혀 있는 값은 만든 사람 PC 의 것이라, 그대로 두면 도슨트 대화와
정보 카드가 서버에 붙지 못한다.

자기 값을 확인한다.

```bash
docker exec dino_ar_db psql -U dino -d dino_ar -c "SELECT id, label FROM exhibits;"
```

그 값을 **두 군데** 에 넣는다. 한 곳만 고치면 다른 쪽이 조용히 실패한다.

| 파일 | 위치 |
|---|---|
| `Content/UI/Docent/WBP_DocentChat` | 클래스 디폴트 → `Docent\|Chat` → **Exhibit Id** |
| `Content/UI/DinoCard/DA_Dino_TRex` | **Dino\|도슨트** → **Exhibit Id** |

> 이 값은 **커밋하지 않는 편이 낫다.** 커밋하면 다음 사람이 또 자기 값으로 바꿔야 하고,
> `.uasset` 이라 충돌이 나면 손으로 합칠 수가 없다. 로컬에서만 바꿔 쓰고,
> 커밋 전에 `git checkout` 으로 되돌리는 것을 권한다.
>
> AR 마커 코드로 전시물을 조회해 이 하드코딩을 없애는 것은 후속 작업이다.

---

## 7. 실기기(폰)에서 붙기

폰의 `127.0.0.1:8000` 을 PC 로 터널링한다. 방화벽·IP·공유기를 신경 쓰지 않아도 된다.

```bash
adb reverse tcp:8000 tcp:8000
adb reverse --list                      # tcp:8000 tcp:8000 이 보이면 됨
```

이 방식이면 `TimeMachineAR/Config/DefaultGame.ini` 의 `ServerBaseUrl` 을
`http://127.0.0.1:8000` 그대로 두면 된다.

> **USB 를 뽑거나 폰을 재부팅하면 사라진다.** 매번 다시 걸어야 한다.
> 앱은 뜨는데 서버에 못 붙으면 이것부터 의심한다.

앱 로그로 확인한다.

```bash
adb logcat -s UE | findstr LogDocent
```

```
LogDocent: 도슨트 클라이언트 초기화. 서버=http://127.0.0.1:8000 device=android-...
LogDocent: [chat] 세션 준비됨: <uuid>
```

---

## 서버 끄기

```
uvicorn 창에서 Ctrl+C
docker compose stop
```

**`docker compose down -v` 는 쓰지 말 것.** `-v` 는 `dino_pgdata` 볼륨을 지운다.
시드와 대화 기록이 전부 날아가고 4번부터 다시 해야 한다.
컨테이너만 정리하려면 `docker compose down` (옵션 없이)을 쓴다.

---

## 자주 겪는 문제

| 증상 | 원인과 조치 |
|---|---|
| `failed to connect to the docker API` | Docker Desktop 이 안 떠 있다. 실행하고 30초~1분 기다린다 |
| `/health` 는 되는데 `/dinosaurs` 가 500 | DB 컨테이너가 안 떠 있거나 시드가 안 들어갔다. 2번 → 4번 |
| `\dt` 에 `chat_sessions` 가 없다 | 오래된 볼륨이다. `apply_chat_schema.py` 실행 (2번 참조) |
| 포트 5432 가 이미 쓰인다 | PC 에 PostgreSQL 이 따로 설치돼 있다. 그걸 끄거나 `docker-compose.yml` 의 포트를 바꾼다 |
| LLM 응답이 항상 폴백 문구 | `LLM_PROVIDER=gemini` 인데 키가 비었거나 만료됐다. `scripts\check_llm.py` 로 DB 없이 키만 따로 확인할 수 있다 |
| 앱은 뜨는데 서버에 못 붙는다 | `adb reverse` 가 풀렸다. 7번을 다시 건다 |
| 대화는 되는데 공룡 내용이 엉뚱하다 | 전시물 UUID 가 자기 PC 값이 아니다. 위 ⚠️ 절 참조 |

---

## 더 볼 것

| 문서 | 내용 |
|---|---|
| `Documents/RUNBOOK-docent-chat.md` | UE5 빌드, 안드로이드 툴체인, 패키징까지 전체 파이프라인 |
| `docs/nav-server-integration-guide.md` | 네비게이션 API 계약과 좌표 규약 |
| `backend_server/docs/specs/` | 기능별 설계 문서 |
