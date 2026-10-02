# Wi-Fi 데모 런북 (USB 없이 실기기 시연)

폰과 노트북을 **같은 공유기**에 두고, USB 케이블 없이 도슨트와 내비게이션을
돌리는 절차다. 2026-08-23 실기기(갤럭시 S25+)에서 전 경로를 검증했다.

USB(adb reverse) 방식은 `docs/server-local-setup.md` 를, UE 빌드·패키징은
`Documents/RUNBOOK-docent-chat.md` 를 본다. 이 문서는 **Wi-Fi 로 붙이는 것**만 다룬다.

> **이 브랜치(`feature/wifi`)를 `develop`/`main` 에 머지하지 말 것.**
> `DefaultGame.ini` 의 IP·UUID 가 서버를 띄우는 PC 에 종속된다. 머지하면
> 팀원 각자의 로컬 UUID 가 덮여 도슨트·내비가 **조용히** 실패한다.

---

## 0. 전제

| 항목 | 확인 |
|---|---|
| Docker Desktop | 실행 중이어야 한다. 미설치면 `winget install --id Docker.DockerDesktop -e` (UAC 승인 필요) |
| Python | 3.11~3.14 에서 동작 확인 |
| 폰과 PC | **같은 공유기**의 같은 대역 |
| 방화벽 | 8000 인바운드 허용 규칙 (아래 4번) |

작업 디렉터리는 `backend_server/` 기준이다.

---

## 1. 서버 PC 의 LAN IP 확인

**매번 확인한다.** DHCP 라 재부팅하거나 공유기가 바뀌면 달라진다.

```powershell
Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.InterfaceAlias -eq 'Wi-Fi' }
```

> **가상 어댑터를 고르지 말 것.** VirtualBox/WSL 이 깔려 있으면 `192.168.56.1`
> 같은 주소가 같이 나오는데, 폰에서는 절대 안 붙는다. `InterfaceAlias` 가
> `Wi-Fi` 인 것을 쓴다.

이 IP 가 바뀌면 **APK 를 다시 패키징**해야 한다(`DefaultGame.ini` 에 박히므로).
급할 때 재패키징을 피하는 우회는 6번에 있다.

---

## 2. DB 띄우기

```bash
docker compose up -d
docker compose ps          # STATUS 가 healthy 여야 한다 (10~20초)
```

테이블 11개를 확인한다.

```bash
docker exec dino_ar_db psql -U dino -d dino_ar -c "\dt"
```

> `docker compose down -v` 는 쓰지 말 것. `-v` 가 볼륨을 지워 시드와 대화
> 기록이 전부 날아간다. 컨테이너만 내리려면 옵션 없이 `docker compose down`.

---

## 3. 환경변수와 의존성

```bash
cp .env.example .env
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

`.env` 에서 LLM 을 고른다.

```ini
LLM_PROVIDER=gemini
LLM_API_KEY=<AI Studio 에서 발급한 키>
LLM_MODEL=gemini-3.1-flash-lite
```

**모델은 `gemini-3.1-flash-lite` 를 쓴다.** 실측 근거:

| 모델 | 응답 시간 |
|---|---|
| `gemini-3.6-flash` | 4.4s / 10.7s / 12.2s — `LLM_TIMEOUT_SEC=10` 을 수시로 넘어 폴백 |
| `gemini-3.1-flash-lite` | 1.0s / 0.9s / 1.1s — 안정 |

`3.6-flash` 로 두면 챗 경로에서 `"답변을 지금 생성하지 못했습니다"` 가 그대로
화면에 뜬다. 챗 폴백은 도슨트 해설이 아니라 에러 문구다.

키가 안 먹으면 붙여넣기 사고를 먼저 의심한다. 실제로 끝에 두 글자가 더 붙어
401 이 났던 적이 있다. 키만 따로 검증할 수 있다:

```bash
curl -s -o nul -w "%{http_code}" -H "x-goog-api-key: <키>" ^
  https://generativelanguage.googleapis.com/v1beta/models
```
`200` 이면 유효하다.

---

## 4. 방화벽 열기 (최초 1회, 관리자 권한)

```powershell
New-NetFirewallRule -DisplayName "TimeMachineAR FastAPI 8000" `
  -Direction Inbound -Protocol TCP -LocalPort 8000 -Action Allow -Profile Any
```

`-Profile Any` 가 중요하다. 공유기가 **공용 네트워크**로 잡히는 경우가 많은데
`Private` 만 허용하면 폰에서 안 붙는다.

이미 있는지 확인:

```powershell
Get-NetFirewallRule -DisplayName "TimeMachineAR FastAPI 8000"
```

---

## 5. 시드 넣기

`docs/server-local-setup.md` 4번을 그대로 따른다. **`seeds/04_dinos_extra.sql`
을 빠뜨리지 말 것** — 이걸 안 넣으면 티라노 외 3종이 404 로 조용히 실패한다.

넣고 나면 전시물 UUID 를 확인한다.

```bash
docker exec dino_ar_db psql -U dino -d dino_ar ^
  -c "SELECT d.name_ko, e.id FROM exhibits e JOIN dinosaurs d ON d.id=e.dinosaur_id;"
docker exec dino_ar_db psql -U dino -d dino_ar -c "SELECT id, name FROM map_spaces;"
```

### UUID 가 안 맞을 때

시드가 UUID 를 고정하지 않아 **PC 마다 값이 다르다.** 클라이언트에는 만든
사람 PC 의 값이 박혀 있다. 맞추는 방향이 두 가지인데, 상황에 따라 고른다.

| 상황 | 방법 |
|---|---|
| 재패키징할 수 있다 | 에셋/ini 를 DB 값으로 고치고 다시 빌드 (정석) |
| 재패키징 못 한다 | **DB 를 에셋 값에 맞춘다** — 자식 행이 없으면 `UPDATE exhibits SET id=<에셋값> WHERE id=<DB값>` |

POI·대화 세션이 딸린 전시물은 `UPDATE` 하면 FK 가 막는다(`ON UPDATE CASCADE`
가 없다). 그때는 **같은 내용의 행을 에셋 UUID 로 하나 더 만들고 POI 를 복사**한다.
둘 다 남겨 두면 구버전 APK 와 신버전 APK 가 **모두** 동작한다.

에셋에 박힌 값은 이렇게 꺼낸다(`.uasset` 안에 평문으로 들어 있다).

```bash
grep -aoiE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' \
  TimeMachineAR/Content/UI/DinoCard/DA_Dino_TRex.uasset | sort -u
```

---

## 6. 클라이언트 설정

`TimeMachineAR/Config/DefaultGame.ini`:

```ini
[/Script/TimeMachineAR.DocentClient]
ServerBaseUrl="http://<서버 PC LAN IP>:8000"

[/Script/TimeMachineAR.NavClient]
; ServerBaseUrl 은 비워 둔다 — DocentClient 주소로 폴백한다(IP 한 곳만 고치면 됨)
DefaultMapId=<map_spaces 의 대상 맵 UUID>
```

> **따옴표는 필수다.** UE 설정 파서는 따옴표 밖의 `//` 를 주석 시작으로 보고
> 뒤를 버린다. 따옴표가 없으면 이 값이 `http:` 가 되고, 그러면 코드가
> `127.0.0.1:8000` 로 폴백해 **USB 없이는 안 붙는다.**

바꿨으면 **다시 패키징해야** APK 에 들어간다.

### 재패키징 없이 급히 덮어쓰기

UE 설정 계층에서 `Saved/Config` 가 `DefaultGame.ini` 를 이긴다. 이 성질을 쓰면
빌드 없이 주소·맵 UUID 를 바꿀 수 있다. 폰에 아래 내용으로 `Game.ini` 를 넣는다.

```ini
[/Script/TimeMachineAR.DocentClient]
ServerBaseUrl="http://192.168.0.100:8000"
RequestTimeoutSec=15.0

[/Script/TimeMachineAR.NavClient]
ServerBaseUrl="http://192.168.0.100:8000"
DefaultMapId=<맵 UUID>
```

```powershell
adb push Game.ini /sdcard/Android/data/com.YourCompany.TimeMachineAR/files/UnrealGame/TimeMachineAR/TimeMachineAR/Saved/Config/Android/Game.ini
adb shell am force-stop com.YourCompany.TimeMachineAR
```

- 재부팅해도 남지만 **앱을 재설치하면 사라진다.**
- **전시물 UUID 는 이걸로 못 고친다.** `.uasset` 안에 있어 ini 계층 밖이다.
  그건 5번의 DB 정렬로 처리한다.
- Git Bash 에서 `adb push` 하면 `/sdcard` 를 Windows 경로로 바꿔 버린다.
  PowerShell 을 쓰거나 `MSYS_NO_PATHCONV=1` 을 붙인다.

---

## 7. 서버 기동

```bash
.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

**`--host 0.0.0.0` 이 핵심이다.** 생략하면 `127.0.0.1` 만 리슨해서 폰에서 못 붙는다.
이 창은 켜 둔 채로 둔다.

### 띄운 직후 반드시 워밍업한다

LLM 클라이언트는 첫 호출에서 생성되고, 그 한 번이 **20초 이상** 걸린다.
앱의 `RequestTimeoutSec` 이 15초라 **데모의 첫 질문이 그대로 실패한다.**
프로세스마다 한 번뿐이니 미리 태워 없앤다.

```bash
curl -s -X POST http://127.0.0.1:8000/docent/sessions ^
  -H "Content-Type: application/json" -d "{}" > nul
curl -s -X POST http://127.0.0.1:8000/docent/chat ^
  -H "Content-Type: application/json" ^
  -d "{\"session_id\":\"<위에서 받은 id>\",\"message\":\"안녕\"}" > nul
```

두 번째 호출이 1초 안쪽이면 준비된 것이다.

> 시드를 바꿨거나 코드를 받았으면 **uvicorn 을 재시동**한다. 네비 그래프가
> 프로세스 메모리에 캐시돼 재시동 없이는 반영되지 않는다. 재시동하면 콜드스타트도
> 다시 생기니 워밍업을 또 해 준다.

---

## 8. 검증 (순서대로, 앞이 실패하면 뒤는 볼 것 없다)

**8-1. PC 자신**

```bash
curl http://127.0.0.1:8000/health       # {"status":"ok"}
curl http://127.0.0.1:8000/dinosaurs    # 4종
```

`/health` 는 DB 를 안 건드린다. `/dinosaurs` 까지 나와야 DB 경로가 살아 있는 것이다.

**8-2. LAN 주소로**

```bash
curl http://<LAN IP>:8000/health
```

여기서 실패하면 `--host 0.0.0.0` 이거나 방화벽이다.

**8-3. 폰에서** — 가장 중요하다. 브라우저로 `http://<LAN IP>:8000/health` 를 연다.
`{"status":"ok"}` 가 나와야 한다.

실패하면 공유기의 **AP isolation(단말 간 격리)** 을 의심한다. PC 쪽을 아무리
고쳐도 안 된다. adb 가 있으면 이렇게도 확인된다.

```powershell
adb shell "curl -s -m 8 http://<LAN IP>:8000/health"
```

**8-4. 앱에서**

```powershell
adb reverse --list    # 비어 있어야 한다. 남아 있으면 USB 로 새서 검증이 무효다
adb logcat -c
adb shell monkey -p com.YourCompany.TimeMachineAR -c android.intent.category.LAUNCHER 1
adb logcat -d -s UE | findstr "LogNav LogDocent"
```

성공하면 이렇게 나온다.

```
LogNav: NavClient 초기화. 서버=http://192.168.x.x:8000 mapId=<맵 UUID>
LogNav: [1/4] GET /maps/{id} ... OK
LogNav: [2/4] GET /markers ... OK (5건)
LogNav: [3/4] GET /destinations ... OK (13건)
LogNav: [4/4] POST /navigation/route ... OK
LogNav: === 스모크 테스트 결과: 4/4 성공 ===
LogDocent: [chat] 완료 source=llm ttft=962ms total=1168ms
```

`source=llm` 이어야 한다. `source=fallback` 이면 LLM 이 실패해 DB 사전 해설로
떨어진 것이다(키·모델·타임아웃 확인).

---

## 서버 끄기

```
uvicorn 창에서 Ctrl+C
docker compose stop
```

---

## 자주 겪는 문제

| 증상 | 원인과 조치 |
|---|---|
| 앱 로그에 `http://127.0.0.1:8000` 이 찍힌다 | APK 가 옛 ini 로 빌드됐다. 커밋·푸시가 됐는지부터 확인한다(`git add` 만 하고 커밋을 빠뜨린 적이 있다). 급하면 6번의 `Game.ini` 우회 |
| 앱 로그의 맵 UUID 가 DB 와 다르다 | 위와 같은 원인. `DefaultMapId` 가 APK 에 박혀 있다 |
| `POST /docent/sessions` 404 | 전시물 UUID 불일치. 5번의 UUID 정렬 |
| `/health` 는 되는데 `/dinosaurs` 500 | DB 컨테이너가 없거나 시드 미투입 |
| 티라노만 되고 나머지 3종이 404 | `seeds/04_dinos_extra.sql` 미적용 |
| 첫 질문만 실패하고 두 번째부터 된다 | LLM 콜드스타트. 7번의 워밍업 |
| `source=fallback` 만 나온다 | 키 무효(401) 또는 모델이 느려 타임아웃. 3번 |
| 폰 브라우저로도 `/health` 가 안 열린다 | 공유기 AP isolation. 공유기 설정을 봐야 한다 |
| 패키징 PC 에서 uasset 이 텍스트로 보인다 | `git lfs pull` 을 빠뜨렸다 |
| 한글 시드가 깨져 들어간다 | `psql < 파일` 파이프 대신 `docker cp` + `psql -f` |
