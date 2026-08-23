"""챗봇 대화 테스트.

기존 `conftest.client` 는 `db.get()` 만 지원하는 스텁 세션이라 대화 저장을
검증할 수 없다. 여기서는 SQLite 인메모리로 실제 테이블을 만들어 INSERT 와
정렬 조회까지 통과시킨다(이 개발 환경에 Docker 가 없어 PostgreSQL 을 쓸 수
없다).
"""
import datetime
import json
import uuid
from decimal import Decimal
from typing import List, Tuple

import pytest
from fastapi.testclient import TestClient
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker
from sqlalchemy.pool import StaticPool

from app.core.db import Base, get_db
from app.main import app
from app.models.dinosaur import Dinosaur
from app.models.exhibit import Exhibit
from app.models.poi import Poi
from app.services.docent import build_chat_prompt
from app.services.llm.factory import get_llm_client
from app.services.llm.mock import FailingLlmClient, MockLlmClient

# 주의: 테스트 UUID 에 반드시 16진 문자(a-f)를 섞는다.
#
# SQLAlchemy 는 SQLite 에도 컬럼 타입명을 그대로 "UUID" 로 내보내는데, SQLite 는
# 모르는 타입명에 NUMERIC 친화성을 준다. 그래서 "2222...2222" 처럼 숫자만 있는
# UUID 는 저장 시 숫자로 강제 변환되고, 읽을 때 float 이 돌아와
# `AttributeError: 'float' object has no attribute 'replace'` 로 터진다.
# 문자가 하나라도 있으면 SQLite 가 변환하지 못해 텍스트로 남는다.
# 운영 DB(PostgreSQL)는 네이티브 UUID 타입이라 이 문제가 없다.
DINOSAUR_ID = uuid.UUID("d1a05a11-1111-4111-8111-111111111111")
EXHIBIT_ID = uuid.UUID("e5b1b12a-2222-4222-8222-222222222222")
POI_ID = uuid.UUID("b01d0c33-3333-4333-8333-333333333333")
MISSING_ID = uuid.UUID("ffffffff-9999-4999-8999-999999999999")

NOW = datetime.datetime.now(datetime.timezone.utc)


@pytest.fixture
def db_factory(monkeypatch):
    """SQLite 인메모리 DB + 시드. 세션 팩토리를 돌려준다.

    StaticPool + 단일 커넥션이라야 인메모리 DB 가 세션 간에 공유된다.
    라우터가 사후 저장에 쓰는 SessionLocal 도 이 팩토리로 바꿔치기한다.
    """
    engine = create_engine(
        "sqlite://",
        connect_args={"check_same_thread": False},
        poolclass=StaticPool,
    )
    Base.metadata.create_all(engine)
    factory = sessionmaker(bind=engine, autoflush=False, autocommit=False)

    seed = factory()
    seed.add_all(
        [
            Dinosaur(
                id=DINOSAUR_ID,
                name_ko="티라노사우루스",
                name_sci="Tyrannosaurus rex",
                period="백악기 후기",
                length_m=Decimal("12.3"),
                ai_prompt_context="티라노사우루스는 백악기 후기의 최상위 포식자였다.",
                created_at=NOW,
            ),
            Exhibit(
                id=EXHIBIT_ID,
                dinosaur_id=DINOSAUR_ID,
                label="1관 티라노 전신골격",
                anchor_hint="중앙 홀 마커 A1",
                created_at=NOW,
            ),
            Poi(
                id=POI_ID,
                exhibit_id=EXHIBIT_ID,
                part_name="두개골",
                pos_x_cm=Decimal("0"),
                pos_y_cm=Decimal("0"),
                pos_z_cm=Decimal("180"),
                docent_text="길이 약 1.5m의 거대한 두개골입니다.",
            ),
        ]
    )
    seed.commit()
    seed.close()

    # 라우터의 사후 저장은 주입된 세션이 아니라 SessionLocal 을 직접 연다.
    monkeypatch.setattr("app.routers.chat.SessionLocal", factory)
    return factory


@pytest.fixture
def client(db_factory):
    def override_get_db():
        db = db_factory()
        try:
            yield db
        finally:
            db.close()

    app.dependency_overrides[get_db] = override_get_db
    app.dependency_overrides[get_llm_client] = MockLlmClient
    yield TestClient(app)
    app.dependency_overrides.clear()
    get_llm_client.cache_clear()


def make_session(client, **kwargs) -> uuid.UUID:
    body = {"device_uuid": "android-test", "exhibit_id": str(EXHIBIT_ID)}
    body.update(kwargs)
    res = client.post("/docent/sessions", json=body)
    assert res.status_code == 200, res.text
    return uuid.UUID(res.json()["session_id"])


def parse_sse(text: str) -> List[Tuple[str, dict]]:
    """SSE 본문을 (event, data) 목록으로 파싱한다.

    UE5 클라이언트가 구현할 파서와 같은 규약을 따른다. 프레임은 빈 줄로
    구분되고 data 는 한 줄짜리 JSON 이다.
    """
    frames = []
    for block in text.split("\n\n"):
        block = block.strip()
        if not block:
            continue
        event = None
        data = None
        for line in block.split("\n"):
            if line.startswith("event: "):
                event = line[len("event: ") :]
            elif line.startswith("data: "):
                data = json.loads(line[len("data: ") :])
        assert event is not None, f"event 없는 프레임: {block!r}"
        frames.append((event, data))
    return frames


# --------------------------------------------------------------------- 세션


def test_create_session(client):
    res = client.post("/docent/sessions", json={"device_uuid": "android-1"})
    assert res.status_code == 200
    assert uuid.UUID(res.json()["session_id"])


def test_create_session_with_unknown_exhibit_404(client):
    res = client.post("/docent/sessions", json={"exhibit_id": str(MISSING_ID)})
    assert res.status_code == 404


# ----------------------------------------------------------- 비스트리밍 대화


def test_chat_returns_answer_and_persists(client):
    session_id = make_session(client)
    res = client.post(
        "/docent/chat", json={"session_id": str(session_id), "message": "뭘 먹었나요?"}
    )
    assert res.status_code == 200, res.text
    body = res.json()
    assert body["source"] == "llm"
    assert body["answer"]
    assert body["message_id"] > 0

    history = client.get(f"/docent/sessions/{session_id}/messages").json()["messages"]
    assert [m["role"] for m in history] == ["user", "assistant"]
    assert history[0]["content"] == "뭘 먹었나요?"


def test_chat_carries_history_into_next_turn(client):
    """두 번째 질문에는 이전 대화가 프롬프트에 실려야 한다."""
    session_id = make_session(client)
    client.post("/docent/chat", json={"session_id": str(session_id), "message": "첫 질문"})
    res = client.post(
        "/docent/chat", json={"session_id": str(session_id), "message": "두 번째 질문"}
    )
    # MockLlmClient 는 히스토리가 있으면 턴 수를 응답에 적어 준다.
    assert "이전 대화 2턴 반영" in res.json()["answer"]


def test_chat_with_poi_records_that_poi(client):
    session_id = make_session(client)
    res = client.post(
        "/docent/chat",
        json={
            "session_id": str(session_id),
            "message": "설명해줘",
            "poi_id": str(POI_ID),
        },
    )
    assert res.status_code == 200

    history = client.get(f"/docent/sessions/{session_id}/messages").json()["messages"]
    # 포커스는 턴마다 달라지므로 세션이 아니라 메시지에 기록된다.
    assert history[0]["poi_id"] == str(POI_ID)


def test_chat_without_poi_records_null_poi(client):
    session_id = make_session(client)
    res = client.post(
        "/docent/chat", json={"session_id": str(session_id), "message": "꼬리는?"}
    )
    assert res.status_code == 200

    history = client.get(f"/docent/sessions/{session_id}/messages").json()["messages"]
    assert history[0]["poi_id"] is None


# ------------------------------------------------------- 프롬프트 조립(단위)
#
# 프롬프트 내용은 엔드포인트 응답으로 검증하지 않는다. MockLlmClient 는
# 컨텍스트를 300자에서 잘라 echo 하므로 뒤쪽 내용이 보이지 않는다.


def _poi(part_name: str, docent_text: str = None) -> Poi:
    return Poi(
        id=uuid.uuid4(),
        exhibit_id=EXHIBIT_ID,
        part_name=part_name,
        pos_x_cm=Decimal("0"),
        pos_y_cm=Decimal("0"),
        pos_z_cm=Decimal("0"),
        docent_text=docent_text,
    )


def test_prompt_with_poi_focuses_that_part():
    exhibit = Exhibit(id=EXHIBIT_ID, dinosaur_id=DINOSAUR_ID, label="1관")
    skull = _poi("두개골", "약 1.5m입니다.")
    prompt = build_chat_prompt(exhibit, None, [skull, _poi("꼬리")], skull)

    assert "관람객이 보고 있는 부위" in prompt
    assert "두개골" in prompt
    assert "약 1.5m입니다." in prompt
    # 부위가 지정됐으면 전체 목록은 넣지 않는다(프롬프트를 짧게 유지).
    assert "이 전시물의 주요 부위" not in prompt


def test_prompt_without_poi_lists_all_pois():
    """부위 미지정이면 목록을 실어 말로만 지목한 부위도 답할 수 있게 한다."""
    exhibit = Exhibit(id=EXHIBIT_ID, dinosaur_id=DINOSAUR_ID, label="1관")
    prompt = build_chat_prompt(exhibit, None, [_poi("두개골"), _poi("꼬리")], None)

    assert "이 전시물의 주요 부위" in prompt
    assert "두개골" in prompt
    assert "꼬리" in prompt
    assert "관람객이 보고 있는 부위" not in prompt


def test_chat_unknown_session_404(client):
    res = client.post(
        "/docent/chat", json={"session_id": str(MISSING_ID), "message": "안녕"}
    )
    assert res.status_code == 404


def test_chat_unknown_poi_404(client):
    session_id = make_session(client)
    res = client.post(
        "/docent/chat",
        json={"session_id": str(session_id), "message": "안녕", "poi_id": str(MISSING_ID)},
    )
    assert res.status_code == 404


def test_chat_without_any_exhibit_is_general_chat(client):
    """세션에도 요청에도 전시물이 없으면 전시물 없는 일반 대화로 답한다.

    마커를 인식하기 전에도 도슨트와 이야기할 수 있어야 한다. 예전에는 대화
    대상을 정할 수 없다며 400 을 냈지만, 클라이언트가 전시물 없이 세션을 여는
    경로(일반 AI 챗)가 생겨 이제는 정상 응답이어야 한다.
    """
    res = client.post("/docent/sessions", json={"device_uuid": "android-2"})
    session_id = res.json()["session_id"]
    res = client.post("/docent/chat", json={"session_id": session_id, "message": "안녕"})
    assert res.status_code == 200
    assert res.json()["answer"]


def test_chat_falls_back_when_llm_fails(client):
    app.dependency_overrides[get_llm_client] = FailingLlmClient
    session_id = make_session(client)
    res = client.post(
        "/docent/chat",
        json={"session_id": str(session_id), "message": "설명", "poi_id": str(POI_ID)},
    )
    assert res.status_code == 200
    body = res.json()
    assert body["source"] == "fallback"
    # POI 가 지정됐으므로 그 부위의 사전 해설이 폴백이 된다.
    assert body["answer"] == "길이 약 1.5m의 거대한 두개골입니다."


# ------------------------------------------------------------------ SSE


def test_stream_emits_meta_delta_done(client):
    session_id = make_session(client)
    res = client.post(
        "/docent/chat/stream",
        json={"session_id": str(session_id), "message": "설명해줘"},
    )
    assert res.status_code == 200
    assert res.headers["content-type"].startswith("text/event-stream")

    frames = parse_sse(res.text)
    kinds = [e for e, _ in frames]
    assert kinds[0] == "meta"
    assert kinds[-1] == "done"
    assert kinds.count("delta") > 1, "여러 조각으로 나뉘어야 스트리밍 경로가 검증된다"

    done = frames[-1][1]
    assert done["source"] == "llm"
    assert done["ttft_ms"] <= done["total_ms"]
    assert done["message_id"] > 0


def test_stream_deltas_reassemble_into_stored_answer(client):
    """조각을 이어 붙인 결과가 저장된 응답과 같아야 한다."""
    session_id = make_session(client)
    res = client.post(
        "/docent/chat/stream",
        json={"session_id": str(session_id), "message": "설명해줘"},
    )
    joined = "".join(d["text"] for e, d in parse_sse(res.text) if e == "delta")

    history = client.get(f"/docent/sessions/{session_id}/messages").json()["messages"]
    assert history[-1]["role"] == "assistant"
    assert history[-1]["content"] == joined.strip()


def test_stream_data_is_single_line_even_with_newlines(client):
    """응답에 개행이 있어도 SSE 프레임이 깨지면 안 된다.

    MockLlmClient 의 응답에는 개행이 들어 있다. data 를 JSON 으로 감싸지
    않으면 여기서 프레임이 쪼개져 파싱이 실패한다.
    """
    session_id = make_session(client)
    res = client.post(
        "/docent/chat/stream",
        json={"session_id": str(session_id), "message": "설명해줘"},
    )
    for line in res.text.split("\n"):
        if line.startswith("data: "):
            json.loads(line[len("data: ") :])  # 각 data 는 그 자체로 완전한 JSON

    joined = "".join(d["text"] for e, d in parse_sse(res.text) if e == "delta")
    assert "\n" in joined, "개행이 포함된 응답이어야 이 테스트가 의미를 갖는다"


def test_stream_falls_back_before_first_chunk(client):
    """첫 조각 전에 실패하면 폴백을 정상 응답처럼 흘려보낸다."""
    app.dependency_overrides[get_llm_client] = FailingLlmClient
    session_id = make_session(client)
    res = client.post(
        "/docent/chat/stream",
        json={"session_id": str(session_id), "message": "설명", "poi_id": str(POI_ID)},
    )
    assert res.status_code == 200

    frames = parse_sse(res.text)
    kinds = [e for e, _ in frames]
    assert "error" not in kinds, "조각 전 실패는 오류가 아니라 폴백으로 처리한다"
    assert frames[-1][0] == "done"
    assert frames[-1][1]["source"] == "fallback"

    joined = "".join(d["text"] for e, d in frames if e == "delta")
    assert joined == "길이 약 1.5m의 거대한 두개골입니다."


def test_history_unknown_session_404(client):
    assert client.get(f"/docent/sessions/{MISSING_ID}/messages").status_code == 404
