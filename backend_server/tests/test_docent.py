"""AI 도슨트 — 프롬프트 조립 / 폴백 / 엔드포인트 스모크."""
import pytest

from app.services.docent import (
    DEFAULT_QUESTION,
    ask_docent,
    build_docent_prompt,
    build_fallback_answer,
)
from app.services.llm.base import LlmClient, LlmError
from app.services.llm.factory import _PROVIDERS, get_llm_client
from app.services.llm.mock import FailingLlmClient, MockLlmClient
from tests.conftest import MISSING_POI_ID, POI_ID


class RecordingLlmClient(LlmClient):
    """전달받은 프롬프트를 기록하는 대역."""

    def __init__(self, answer="테스트 응답"):
        self.answer = answer
        self.system_prompt = None
        self.user_prompt = None

    def generate(self, system_prompt, user_prompt):
        self.system_prompt = system_prompt
        self.user_prompt = user_prompt
        return self.answer


# --- 프롬프트 조립 -----------------------------------------------------

def test_프롬프트에_DB_컨텍스트가_모두_포함된다(poi, exhibit, dinosaur):
    prompt = build_docent_prompt(poi, exhibit, dinosaur)

    assert exhibit.label in prompt
    assert exhibit.anchor_hint in prompt
    assert dinosaur.name_ko in prompt
    assert dinosaur.name_sci in prompt
    assert dinosaur.period in prompt
    assert dinosaur.ai_prompt_context in prompt
    assert poi.part_name in prompt
    assert poi.docent_text in prompt


def test_공룡_정보가_없어도_프롬프트가_생성된다(poi, exhibit):
    prompt = build_docent_prompt(poi, exhibit, None)

    assert exhibit.label in prompt
    assert poi.part_name in prompt


def test_선택_필드가_비어도_프롬프트가_생성된다(poi, exhibit, dinosaur):
    exhibit.anchor_hint = None
    dinosaur.period = None
    dinosaur.length_m = None
    dinosaur.ai_prompt_context = None
    poi.docent_text = None

    prompt = build_docent_prompt(poi, exhibit, dinosaur)

    assert exhibit.label in prompt
    assert poi.part_name in prompt


# --- 폴백 --------------------------------------------------------------

def test_폴백은_docent_text를_반환한다(poi):
    assert build_fallback_answer(poi) == poi.docent_text


def test_docent_text가_없으면_부위명으로_폴백한다(poi):
    poi.docent_text = None
    assert poi.part_name in build_fallback_answer(poi)


def test_LLM_실패시_폴백한다(poi, exhibit, dinosaur):
    result = ask_docent(poi, exhibit, dinosaur, None, FailingLlmClient())

    assert result.source == "fallback"
    assert result.answer == poi.docent_text


def test_LLM이_LlmError가_아닌_예외를_던져도_폴백한다(poi, exhibit, dinosaur):
    class BrokenLlmClient(LlmClient):
        def generate(self, system_prompt, user_prompt):
            raise RuntimeError("어댑터 버그")

    result = ask_docent(poi, exhibit, dinosaur, None, BrokenLlmClient())

    assert result.source == "fallback"
    assert result.answer == poi.docent_text


def test_LLM이_빈_응답을_주면_폴백한다(poi, exhibit, dinosaur):
    result = ask_docent(poi, exhibit, dinosaur, None, RecordingLlmClient(answer="   "))

    assert result.source == "fallback"
    assert result.answer == poi.docent_text


# --- 질문 처리 ---------------------------------------------------------

def test_질문이_없으면_기본_요청문장을_쓴다(poi, exhibit, dinosaur):
    llm = RecordingLlmClient()
    result = ask_docent(poi, exhibit, dinosaur, None, llm)

    assert llm.user_prompt == DEFAULT_QUESTION
    assert result.source == "llm"


def test_질문이_있으면_그대로_전달한다(poi, exhibit, dinosaur):
    llm = RecordingLlmClient()
    ask_docent(poi, exhibit, dinosaur, "이 공룡은 무엇을 먹었나요?", llm)

    assert llm.user_prompt == "이 공룡은 무엇을 먹었나요?"


def test_공백뿐인_질문은_기본_요청문장으로_대체된다(poi, exhibit, dinosaur):
    llm = RecordingLlmClient()
    ask_docent(poi, exhibit, dinosaur, "   ", llm)

    assert llm.user_prompt == DEFAULT_QUESTION


# --- LLM 팩토리 --------------------------------------------------------

def test_mock_provider는_키_없이_생성된다(monkeypatch):
    from app.core import config

    get_llm_client.cache_clear()
    monkeypatch.setattr(config.settings, "LLM_PROVIDER", "mock")
    monkeypatch.setattr(config.settings, "LLM_API_KEY", "")
    assert isinstance(get_llm_client(), MockLlmClient)
    get_llm_client.cache_clear()


def test_지원하지_않는_provider는_에러다(monkeypatch):
    from app.core import config

    get_llm_client.cache_clear()
    monkeypatch.setattr(config.settings, "LLM_PROVIDER", "nonexistent")
    with pytest.raises(ValueError, match="지원하지 않는 LLM_PROVIDER"):
        get_llm_client()
    get_llm_client.cache_clear()


def test_등록된_provider_목록():
    assert set(_PROVIDERS) == {"mock", "failing", "gemini"}


def test_gemini_provider는_키가_없으면_생성시_에러다(monkeypatch):
    """키 누락을 폴백으로 숨기지 않고 생성 시점에 드러낸다."""
    from app.core import config

    get_llm_client.cache_clear()
    monkeypatch.setattr(config.settings, "LLM_PROVIDER", "gemini")
    monkeypatch.setattr(config.settings, "LLM_API_KEY", "")
    with pytest.raises(ValueError, match="LLM_API_KEY"):
        get_llm_client()
    get_llm_client.cache_clear()


def test_GeminiLlmClient는_호출실패를_LlmError로_감싼다(poi, exhibit, dinosaur):
    """네트워크/타임아웃 실패가 500이 아니라 폴백으로 이어지는지 확인."""
    from app.services.llm.gemini import GeminiLlmClient

    client = GeminiLlmClient(
        # 실제 키 형식을 흉내내지 않는다 — 시크릿 스캐너 오탐 방지
        api_key="dummy-key-for-failure-test",
        model="gemini-3.6-flash",
        timeout_sec=10.0,
        max_tokens=1024,
        thinking_level="minimal",
    )
    result = ask_docent(poi, exhibit, dinosaur, None, client)

    assert result.source == "fallback"
    assert result.answer == poi.docent_text


def test_잘못된_thinking_level은_생성시_에러다():
    from app.services.llm.gemini import GeminiLlmClient

    with pytest.raises(ValueError, match="LLM_THINKING_LEVEL"):
        GeminiLlmClient(
            api_key="dummy-key",
            model="gemini-3.6-flash",
            timeout_sec=10.0,
            max_tokens=1024,
            thinking_level="turbo",
        )


def test_타임아웃이_API최소값_미만이면_올려서_생성된다():
    """Gemini 는 10초 미만 deadline 을 400 으로 거부한다 — 클램프해야 한다."""
    from app.services.llm.gemini import GeminiLlmClient

    client = GeminiLlmClient(
        api_key="dummy-key",
        model="gemini-3.6-flash",
        timeout_sec=4.0,  # 그대로 보내면 400 INVALID_ARGUMENT
        max_tokens=1024,
        thinking_level="minimal",
    )
    assert client._client._api_client._http_options.timeout == 10_000


def test_Gemini_안전차단은_LlmError로_올린다():
    """차단 시 text 가 None 이라 그냥 읽으면 원인을 알 수 없다 — 먼저 판별한다."""
    from google.genai import types

    from app.services.llm.gemini import GeminiLlmClient

    blocked = types.GenerateContentResponse(
        candidates=[types.Candidate(finish_reason=types.FinishReason.SAFETY)]
    )
    with pytest.raises(LlmError, match="중단"):
        GeminiLlmClient._raise_if_blocked(blocked)


def test_Gemini_정상종료는_통과한다():
    from google.genai import types

    from app.services.llm.gemini import GeminiLlmClient

    ok = types.GenerateContentResponse(
        candidates=[types.Candidate(finish_reason=types.FinishReason.STOP)]
    )
    GeminiLlmClient._raise_if_blocked(ok)  # 예외가 없어야 한다


def test_MockLlmClient는_컨텍스트를_반영한다():
    answer = MockLlmClient().generate("티라노사우루스 배경지식", "무엇을 먹었나요?")

    assert "[MOCK]" in answer
    assert "무엇을 먹었나요?" in answer
    assert "티라노사우루스" in answer


def test_FailingLlmClient는_LlmError를_던진다():
    with pytest.raises(LlmError):
        FailingLlmClient().generate("s", "u")


# --- 엔드포인트 --------------------------------------------------------

def test_도슨트_질의가_200을_반환한다(client, poi, exhibit):
    res = client.post("/docent/ask", json={"poi_id": str(POI_ID)})

    assert res.status_code == 200
    body = res.json()
    assert body["poi_id"] == str(POI_ID)
    assert body["exhibit_id"] == str(exhibit.id)
    assert body["source"] == "llm"
    assert body["answer"]
    assert body["elapsed_ms"] >= 0


def test_질문을_포함해_질의할_수_있다(client):
    res = client.post(
        "/docent/ask",
        json={
            "poi_id": str(POI_ID),
            "question": "이 공룡은 무엇을 먹었나요?",
            "device_uuid": "quest3-anonymous-0001",
        },
    )

    assert res.status_code == 200
    assert "이 공룡은 무엇을 먹었나요?" in res.json()["answer"]


def test_없는_POI는_404다(client):
    res = client.post("/docent/ask", json={"poi_id": str(MISSING_POI_ID)})

    assert res.status_code == 404
    assert res.json()["detail"] == "poi not found"


def test_poi_id가_UUID가_아니면_422다(client):
    res = client.post("/docent/ask", json={"poi_id": "not-a-uuid"})

    assert res.status_code == 422


def test_LLM_장애시에도_500이_아니라_폴백을_반환한다(client, poi):
    from app.main import app

    app.dependency_overrides[get_llm_client] = FailingLlmClient

    res = client.post("/docent/ask", json={"poi_id": str(POI_ID)})

    assert res.status_code == 200
    body = res.json()
    assert body["source"] == "fallback"
    assert body["answer"] == poi.docent_text


def test_swagger에_docent_엔드포인트가_문서화된다(client):
    schema = client.get("/openapi.json").json()
    operation = schema["paths"]["/docent/ask"]["post"]

    assert operation["tags"] == ["docent"]
    assert "404" in operation["responses"]
    assert "DocentAskRequest" in operation["requestBody"]["content"][
        "application/json"
    ]["schema"]["$ref"]
