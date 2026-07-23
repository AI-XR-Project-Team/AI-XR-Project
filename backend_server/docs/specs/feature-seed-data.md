# Feature Spec: Seed 데이터 (기기 없이 API 검증용)

- **Branch**: `feature/seed-data`
- **Status**: Implemented  <!-- Draft | Approved | Implemented | Merged -->
- **Author**: 현우
- **관련 이슈/태스크**: `docs/tasks/task-2-week1-python-server.md` Step 3
- **선행**: `feature/db-postgres-setup`, `feature/fastapi-skeleton`

## 1. 목표 / 배경

Quest 3 기기와 실제 관람 데이터가 없는 상태에서 API가 실제 값을 반환하는지 검증하려면 최소한의 가상 데이터가 필요하다. 티라노사우루스 1종 + 전시물 1개 + POI 3개를 삽입하는 시드 스크립트를 만들어, `GET /dinosaurs` 등이 빈 배열이 아니라 실제 JSON을 돌려주게 한다. 전시물은 Task 1의 3D 프린트 티라노 골격과 매칭한다.

## 2. 범위

- 포함(In scope):
  - `backend_server/scripts/seed.py` — 멱등(idempotent) 시드 스크립트
  - 데이터: 공룡 1종(티라노), 전시물 1개, POI 3개(두개골/갈비뼈/대퇴골)
  - 재실행 시 중복 삽입 방지(existence check 또는 upsert)
- 제외(Out of scope):
  - view_logs 더미 로그(통계 기능은 2주차)
  - 다수 공룡/전시물 대량 시드
  - 프로덕션 시드 파이프라인

## 3. 설계

### 파일 / 모듈 구조
```
backend_server/
  scripts/
    seed.py            # SessionLocal 로 DB 접속 → 시드 삽입 (app.models 재사용)
```

### 인터페이스
- 실행: `python -m scripts.seed` (또는 `python scripts/seed.py`), `backend_server/` 를 작업 디렉터리로.
- 삽입 데이터:
  - **dinosaurs** 1건: `name_ko=티라노사우루스`, `name_sci=Tyrannosaurus rex`, `period=백악기 후기`, `length_m=12.3`, `model_asset_key=trex_full_skeleton`, `ai_prompt_context=`(티라노 생태·서식·먹이 등 Gemini 도슨트용 배경 문단)
  - **exhibits** 1건: `label=1관 티라노 전신골격`, `anchor_hint=`(공간 정합용 마커 힌트), `dinosaur_id`→위 티라노
  - **pois** 3건: `두개골 / 갈비뼈 / 대퇴골`, 각 `pos_x_cm/pos_y_cm/pos_z_cm` 임의 cm 값(UE5 Z-up), `docent_text` 기본 해설 1~2문장
- 멱등성: `device_uuid`/`name_sci`/`label`+`part_name` 등 자연키로 존재 여부 확인 후 없을 때만 삽입.
- 외부 의존성: `feature/fastapi-skeleton` 의 `app.models`, `core.db.SessionLocal` 재사용(중복 정의 금지).

### 데이터 흐름
`seed.py` → `SessionLocal()` 세션 열기 → 티라노 존재 확인 → 없으면 dinosaur → exhibit → pois 순서로 FK 연결 삽입 → `commit()`. 이후 `GET /dinosaurs` 호출 시 티라노 JSON 반환.

## 4. 완료 조건 (Acceptance Criteria)

- [x] `python scripts/seed.py` 실행 시 티라노 1종 + 전시물 1개 + POI 3개가 삽입된다
- [x] 스크립트를 2번 실행해도 중복 행이 생기지 않는다(멱등)
- [x] `GET /dinosaurs` 가 티라노사우루스 1건을 포함한 JSON을 반환한다
- [x] `GET /exhibits/{id}` 로 "1관 티라노 전신골격" 과 POI 3개가 조회된다
- [x] POI 좌표가 UE5 Z-up, cm 규약을 따른다

### 검증 메모 (2026-07-18, 실행 검증 완료)

`backend_server/.venv` 로 실행 중 DB(Colima)에 대해 검증:
- 1회차: dinosaur/exhibit/poi 3 모두 `[+] 삽입`. 2회차: 모두 `[=] 건너뜀` → 행 수 `dinosaurs=1 / exhibits=1 / pois=3` (중복 없음, 멱등 확인).
- `GET /dinosaurs` → 티라노사우루스(Tyrannosaurus rex / 백악기 후기) 1건.
- `GET /exhibits/{id}` → "1관 티라노 전신골격" + POI 3개(두개골 z=180 / 갈비뼈 z=120 / 대퇴골 z=70, UE5 Z-up cm).
> POI 좌표·`docent_text`·`ai_prompt_context` 는 임시 문안 → Task 1 실측/팀 확인 후 갱신 예정(§7).

## 5. 작업 분해 (Task Breakdown)

- [ ] `scripts/seed.py` 작성 (모델 재사용, 자연키 존재 확인 + 삽입)
- [ ] 티라노 `ai_prompt_context` / POI `docent_text` 문안 작성
- [ ] 멱등성 확인 (2회 실행 테스트)
- [ ] Swagger/Postman 으로 `GET /dinosaurs`, `GET /exhibits/{id}` 검증
- [ ] (선택) `pytest` 로 시드 후 조회 통합 테스트

## 6. 테스트 방법

- 로컬: DB + FastAPI 기동 상태에서 `python scripts/seed.py` 실행 → 재실행해 중복 없음 확인 → Swagger `/docs` 또는 Postman 으로 `GET /dinosaurs`(티라노 확인), `GET /exhibits/{id}`(전시물+POI 3개 확인).
- 멀티플레이 / Quest 3 검증 여부: 해당 없음. 시드가 Quest 3 실측 데이터를 대체함.

## 7. 리스크 / 열린 질문

- POI 좌표는 임시 값 → Task 1의 실제 3D 프린트 골격 실측 후 갱신 필요(2주차).
- `ai_prompt_context` / `docent_text` 초안은 사실 검증이 필요할 수 있음(공룡 생태 정보) → 팀 확인.
- 모델 import 경로는 `feature/fastapi-skeleton` 구조 확정에 의존.
