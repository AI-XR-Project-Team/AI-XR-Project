"""API 키 없이 동작하는 개발/테스트용 LLM 구현.

벤더가 확정되기 전에도 프롬프트 조립 · API 계약 · 폴백 · Swagger 를
끝까지 검증할 수 있게 한다.
"""
from app.services.llm.base import LlmClient, LlmError


class MockLlmClient(LlmClient):
    """실제 생성 대신 프롬프트를 결정론적으로 되돌려주는 구현.

    - 응답 앞에 `[MOCK]` 을 붙여 실제 LLM 응답과 혼동되지 않게 한다.
    - system_prompt 요약을 함께 실어, 어떤 DB 컨텍스트가 조립됐는지
      Swagger 에서 눈으로 확인할 수 있게 한다.
    """

    _PREFIX = "[MOCK]"
    _MAX_CONTEXT_CHARS = 300

    def generate(self, system_prompt: str, user_prompt: str) -> str:
        context_digest = " ".join(system_prompt.split())[: self._MAX_CONTEXT_CHARS]
        return (
            f"{self._PREFIX} \"{user_prompt}\" 에 대한 도슨트 응답입니다.\n"
            f"(조립된 컨텍스트 앞부분: {context_digest} …)"
        )


class FailingLlmClient(LlmClient):
    """항상 실패하는 구현 — 폴백 경로 수동 검증용.

    `.env` 에서 `LLM_PROVIDER=failing` 으로 바꾸기만 하면 코드 수정 없이
    "LLM 장애 시 docent_text 폴백" 동작을 Swagger 에서 재현할 수 있다.
    """

    def generate(self, system_prompt: str, user_prompt: str) -> str:
        raise LlmError("FailingLlmClient: 폴백 검증용으로 항상 실패한다")
