-- =====================================================================
-- 공룡 박물관 AR — 네비게이션 확장 맵(4단계): map_spaces.outline_json 컬럼
-- 컨테이너(postgres:16) 최초 기동 시 /docker-entrypoint-initdb.d 에서
-- 02_nav_schema.sql 다음으로 자동 실행됨. 기존 볼륨엔 자동 적용되지 않으므로
-- 이미 DB가 있으면 수동 적용:
--   python scripts/apply_nav_outline.py
--   # 또는  psql "$DATABASE_URL" -f db/init/03_nav_outline.sql
-- (모든 DDL 이 IF NOT EXISTS 라 재실행 안전)
-- =====================================================================

-- 벽 외곽선(outline) + 내부 구조물(obstacles) 을 JSON 문자열로 보관.
-- {"outline": [[x_cm, y_cm], ...], "obstacles": [{"x0","y0","x1","y1"}, ...]}
-- 전체 미니맵(GET /maps/{id}/graph)이 한 번에 꺼내 쓴다. NULL 이면 빈 배열로 취급.
ALTER TABLE map_spaces
    ADD COLUMN IF NOT EXISTS outline_json TEXT;
