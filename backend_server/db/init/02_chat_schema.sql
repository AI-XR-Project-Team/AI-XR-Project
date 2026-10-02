-- =====================================================================
-- 공룡 박물관 AR — 챗봇 대화 스키마
--
-- 01_schema.sql 과 마찬가지로 컨테이너 최초 기동 시 자동 실행된다.
-- 다만 이미 볼륨이 있는 DB 에는 적용되지 않으므로, 그 경우
--   python scripts/apply_chat_schema.py
-- 로 수동 적용한다. 그래서 이 스크립트는 전부 IF NOT EXISTS 로 멱등하게 쓴다.
--
-- 기존 view_logs 는 (user_id, poi_id, viewed_at) 만 남겨 질문/응답 본문을
-- 담을 수 없다. feature-ai-docent-service §7 의 ai_docent_logs 를 대신한다.
-- =====================================================================

-- ---------------------------------------------------------------------
-- chat_sessions : 관람객 1명의 대화 1건
-- 로그인이 없으므로 device_uuid 로만 느슨하게 묶는다.
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS chat_sessions (
    id             UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    device_uuid    VARCHAR(128),
    -- 이 대화가 어느 전시물에 대한 것인지. 부위를 고르지 않은 자유대화에서
    -- 배경 컨텍스트를 결정하는 근거가 된다.
    exhibit_id     UUID         REFERENCES exhibits(id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ  NOT NULL DEFAULT now(),
    last_active_at TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------
-- chat_messages : 대화 한 턴 (user 질문과 assistant 응답이 각각 한 행)
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS chat_messages (
    id         BIGSERIAL    PRIMARY KEY,
    session_id UUID         NOT NULL
               REFERENCES chat_sessions(id) ON DELETE CASCADE,
    role       VARCHAR(16)  NOT NULL,           -- user | assistant
    content    TEXT         NOT NULL,
    -- 이 턴에서 관람객이 지목한 부위. 자유대화면 NULL.
    poi_id     UUID         REFERENCES pois(id),
    -- 아래는 assistant 행에만 채워지는 진단 정보.
    source     VARCHAR(16),                     -- llm | fallback
    -- 스트리밍의 체감 지표는 총 소요시간이 아니라 첫 토큰까지의 시간이다.
    ttft_ms    INT,
    total_ms   INT,
    created_at TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- 히스토리 조회는 항상 "세션의 메시지를 삽입 순으로" 읽는다.
-- created_at 은 같은 초에 여러 건이 들어오면 순서가 흔들려 정렬에 쓰지 않는다.
CREATE INDEX IF NOT EXISTS idx_chat_messages_session
    ON chat_messages(session_id, id);

-- 오래된 세션 정리·조회용.
CREATE INDEX IF NOT EXISTS idx_chat_sessions_device
    ON chat_sessions(device_uuid);
