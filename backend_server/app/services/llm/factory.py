"""`LLM_PROVIDER` 설정값으로 LLM 클라이언트 구현을 고르는 팩토리.

벤더를 추가할 때 수정 지점은 이 파일의 `_PROVIDERS` 한 줄뿐이다.
    "openai": _build_openai,   # app/services/llm/openai.py 에 어댑터 추가 후
"""
from functools import lru_cache
from typing import Callable, Dict

from app.core.config import settings
from app.services.llm.base import LlmClient
from app.services.llm.gemini import GeminiLlmClient
from app.services.llm.mock import FailingLlmClient, MockLlmClient


def _build_gemini() -> LlmClient:
    return GeminiLlmClient(
        api_key=settings.LLM_API_KEY,
        model=settings.LLM_MODEL,
        timeout_sec=settings.LLM_TIMEOUT_SEC,
        max_tokens=settings.LLM_MAX_TOKENS,
        thinking_level=settings.LLM_THINKING_LEVEL,
    )


_PROVIDERS: Dict[str, Callable[[], LlmClient]] = {
    "mock": MockLlmClient,
    "failing": FailingLlmClient,
    "gemini": _build_gemini,
}


@lru_cache(maxsize=1)
def get_llm_client() -> LlmClient:
    """설정에 맞는 LLM 클라이언트를 반환한다(프로세스당 1회 생성).

    FastAPI 라우터에서 `Depends(get_llm_client)` 로 주입하며,
    테스트는 `app.dependency_overrides` 로 교체하거나
    `get_llm_client.cache_clear()` 후 설정을 바꾼다.

    Raises:
        ValueError: 지원하지 않는 `LLM_PROVIDER` 값인 경우.
    """
    provider = settings.LLM_PROVIDER.strip().lower()
    build = _PROVIDERS.get(provider)
    if build is None:
        raise ValueError(
            f"지원하지 않는 LLM_PROVIDER: {settings.LLM_PROVIDER!r} "
            f"(사용 가능: {', '.join(sorted(_PROVIDERS))})"
        )
    return build()
