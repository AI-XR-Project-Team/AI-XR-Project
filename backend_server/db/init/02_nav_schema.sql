-- =====================================================================
-- 공룡 박물관 AR — 실내 네비게이션 스키마 (마커 QR + 경로 그래프)
-- 컨테이너(postgres:16) 최초 기동 시 /docker-entrypoint-initdb.d 에서
-- 01_schema.sql 다음으로 자동 실행됨. 기존 볼륨엔 자동 적용되지 않으므로
-- 이미 DB가 있으면 수동 적용:  psql "$DATABASE_URL" -f db/init/02_nav_schema.sql
-- (모든 DDL 이 IF NOT EXISTS 라 재실행 안전)
-- 좌표 규약: UE5 Z-up, 단위 cm. heading: +X축 기준 CCW 도(°).
-- =====================================================================

-- gen_random_uuid() 보장 (01_schema.sql 과 중복 안전)
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- ---------------------------------------------------------------------
-- map_spaces : 지도 공간 (단층 = 1 행). 좌표계 원점·축 정의를 서술로 보관.
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS map_spaces (
    id           UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    name         VARCHAR(200) NOT NULL,
    origin_note  TEXT,                                    -- 원점·축 정의 서술
    coord_system VARCHAR(50)  NOT NULL DEFAULT 'ue5_zup_cm',
    created_at   TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- nav_nodes : 경로 그래프 노드 (분기·출입구·목적지·경유). 좌표 UE5 Z-up cm.
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS nav_nodes (
    id        UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    map_id    UUID         NOT NULL
              REFERENCES map_spaces(id) ON DELETE CASCADE,
    pos_x_cm  NUMERIC(8,2) NOT NULL,                      -- UE5 X (cm)
    pos_y_cm  NUMERIC(8,2) NOT NULL,                      -- UE5 Y (cm)
    pos_z_cm  NUMERIC(8,2) NOT NULL,                      -- UE5 Z(up) (cm)
    node_type VARCHAR(20)  NOT NULL DEFAULT 'waypoint',   -- junction|waypoint|exhibit|entrance|facility
    label     VARCHAR(200)
);

-- ---------------------------------------------------------------------
-- nav_edges : 경로 그래프 엣지 (노드 연결·거리·양방향). distance NULL 이면
-- 시드/그래프 빌드 시 노드 좌표로 자동 계산.
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS nav_edges (
    id            UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    map_id        UUID         NOT NULL
                  REFERENCES map_spaces(id) ON DELETE CASCADE,
    from_node_id  UUID         NOT NULL
                  REFERENCES nav_nodes(id) ON DELETE CASCADE,
    to_node_id    UUID         NOT NULL
                  REFERENCES nav_nodes(id) ON DELETE CASCADE,
    distance_cm   NUMERIC(9,2),                           -- NULL=좌표로 자동 계산
    bidirectional BOOLEAN      NOT NULL DEFAULT TRUE,
    accessible    BOOLEAN      NOT NULL DEFAULT TRUE       -- false=계단 등, 휠체어 경로 제외용
);

-- ---------------------------------------------------------------------
-- markers : QR 마커 (측위 기준점). code = QR 에 담긴 문자열, 앱이 디코드해 조회.
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS markers (
    id          UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    map_id      UUID         NOT NULL
                REFERENCES map_spaces(id) ON DELETE CASCADE,
    code        VARCHAR(100) NOT NULL UNIQUE,             -- QR 문자열 (조회 키)
    marker_type VARCHAR(20)  NOT NULL DEFAULT 'qr',       -- qr (MVP) | image (후순위)
    pos_x_cm    NUMERIC(8,2) NOT NULL,                    -- UE5 X (cm)
    pos_y_cm    NUMERIC(8,2) NOT NULL,                    -- UE5 Y (cm)
    pos_z_cm    NUMERIC(8,2) NOT NULL,                    -- UE5 Z(up) (cm)
    heading_deg NUMERIC(6,2) NOT NULL DEFAULT 0,          -- 마커 정면 방향(+X 기준 CCW °)
    note        VARCHAR(200)
);

-- ---------------------------------------------------------------------
-- exhibits.nav_node_id : 전시물 ↔ 그래프 노드 매핑 (도착 목적지 연결).
-- 노드 삭제 시 매핑만 해제(SET NULL), 전시물은 보존.
-- ---------------------------------------------------------------------
ALTER TABLE exhibits
    ADD COLUMN IF NOT EXISTS nav_node_id UUID
    REFERENCES nav_nodes(id) ON DELETE SET NULL;

-- 조회 성능용 인덱스 (map_id 기준 필터가 잦음)
CREATE INDEX IF NOT EXISTS idx_nav_nodes_map ON nav_nodes(map_id);
CREATE INDEX IF NOT EXISTS idx_nav_edges_map ON nav_edges(map_id);
CREATE INDEX IF NOT EXISTS idx_markers_map   ON markers(map_id);
