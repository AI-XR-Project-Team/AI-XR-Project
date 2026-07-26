"""도슨트 테스트용 공용 픽스처.

PostgreSQL 없이 돌도록 `get_db` 를 인메모리 스텁 세션으로 교체한다.
`load_poi_context` 가 `db.get(Model, id)` 만 사용하므로 스텁으로 충분하다.
"""
import uuid
from decimal import Decimal

import pytest
from fastapi.testclient import TestClient

from app.core.db import get_db
from app.main import app
from app.models.dinosaur import Dinosaur
from app.models.exhibit import Exhibit
from app.models.poi import Poi
from app.services.llm.factory import get_llm_client

DINOSAUR_ID = uuid.UUID("11111111-1111-1111-1111-111111111111")
EXHIBIT_ID = uuid.UUID("22222222-2222-2222-2222-222222222222")
POI_ID = uuid.UUID("33333333-3333-3333-3333-333333333333")
MISSING_POI_ID = uuid.UUID("99999999-9999-9999-9999-999999999999")


class StubSession:
    """`db.get(Model, pk)` 만 지원하는 최소 세션 대역."""

    def __init__(self, rows):
        self._rows = rows

    def get(self, model, pk):
        return self._rows.get((model, pk))


@pytest.fixture
def dinosaur():
    return Dinosaur(
        id=DINOSAUR_ID,
        name_ko="티라노사우루스",
        name_sci="Tyrannosaurus rex",
        period="백악기 후기",
        length_m=Decimal("12.3"),
        model_asset_key="trex_full_skeleton",
        ai_prompt_context="티라노사우루스는 백악기 후기 북아메리카의 최상위 포식자였다.",
    )


@pytest.fixture
def exhibit():
    return Exhibit(
        id=EXHIBIT_ID,
        dinosaur_id=DINOSAUR_ID,
        label="1관 티라노 전신골격",
        anchor_hint="중앙 홀 바닥 마커 A1 기준 정렬",
    )


@pytest.fixture
def poi():
    return Poi(
        id=POI_ID,
        exhibit_id=EXHIBIT_ID,
        part_name="두개골",
        pos_x_cm=Decimal("0"),
        pos_y_cm=Decimal("0"),
        pos_z_cm=Decimal("180"),
        docent_text="길이 약 1.5m의 거대한 두개골입니다.",
    )


@pytest.fixture
def client(poi, exhibit, dinosaur):
    """스텁 DB 가 주입된 TestClient. LLM 은 설정 기본값(mock)을 쓴다."""
    rows = {
        (Poi, POI_ID): poi,
        (Exhibit, EXHIBIT_ID): exhibit,
        (Dinosaur, DINOSAUR_ID): dinosaur,
    }
    app.dependency_overrides[get_db] = lambda: StubSession(rows)
    yield TestClient(app)
    app.dependency_overrides.clear()
    get_llm_client.cache_clear()
