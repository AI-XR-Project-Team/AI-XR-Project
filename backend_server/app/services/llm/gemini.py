"""Google Gemini API 어댑터.

`LlmClient` 구현체. Google AI Studio 의 무료 티어로 동작한다.
API 키는 `.env` 의 `LLM_API_KEY` 로만 주입하며, `.env` 는 .gitignore 대상이라
저장소에 올라가지 않는다.

지연 특성 (실측 기반):
    도슨트 KPI 는 응답 2초 이내(통합 문서 §3 핵심 지표)다. 실측 결과
    `gemini-3.6-flash` + `thinking_level=MINIMAL` 이 약 1.5초로 이를 만족한다.
    - thinking 을 MINIMAL 로 낮춘다. 도슨트 답변은 주어진 배경지식을 3~4문장으로
      풀어쓰는 단순 생성이라 깊은 추론이 필요 없고, LOW 만 돼도 4초대로 뛴다.
    - SDK 자동 재시도를 끈다. 재시도는 지연을 배로 늘리는데, 우리에겐
      `pois.docent_text` 라는 즉시 응답 가능한 폴백이 이미 있다.
    - 타임아웃/에러/차단/쿼터초과는 모두 LlmError 로 변환 → 서비스 계층이 폴백.

주의 — thinking 파라미터는 모델 세대별로 다르다:
    Gemini 3.x 는 `thinking_level`(MINIMAL/LOW/MEDIUM/HIGH)을 쓴다.
    2.5 계열의 `thinking_budget=0` 을 3.x 에 보내면 400 INVALID_ARGUMENT 다.
"""
import logging

from google import genai
from google.genai import errors, types

from app.services.llm.base import LlmClient, LlmError

logger = logging.getLogger(__name__)

# 응답이 잘렸거나 안전 필터에 걸린 경우. 정상 종료는 STOP 뿐이다.
_OK_FINISH_REASONS = {types.FinishReason.STOP, types.FinishReason.MAX_TOKENS}

# Gemini API 는 10초 미만의 deadline 을 거부한다.
#   400 INVALID_ARGUMENT "Manually set deadline 4s is too short."
# 즉 이 값은 KPI 가 아니라 '응답이 멎었을 때의 상한'이다. 정상 지연은 ~1.5초.
_MIN_API_TIMEOUT_SEC = 10.0


class GeminiLlmClient(LlmClient):
    """Gemini `generateContent` 로 도슨트 응답을 생성한다."""

    def __init__(
        self,
        api_key: str,
        model: str,
        timeout_sec: float,
        max_tokens: int,
        thinking_level: str,
    ):
        if not api_key:
            raise ValueError(
                "LLM_PROVIDER=gemini 인데 LLM_API_KEY 가 비어 있다. "
                "backend_server/.env 에 키를 넣어라(.env 는 커밋되지 않는다)."
            )
        try:
            level = types.ThinkingLevel[thinking_level.strip().upper()]
        except KeyError:
            raise ValueError(
                f"지원하지 않는 LLM_THINKING_LEVEL: {thinking_level!r} "
                f"(사용 가능: minimal, low, medium, high)"
            ) from None

        if timeout_sec < _MIN_API_TIMEOUT_SEC:
            logger.warning(
                "LLM_TIMEOUT_SEC=%.1f 는 Gemini 최소 deadline(%.0f초) 미만이라 "
                "%.0f초로 올린다. 정상 응답은 ~1.5초이므로 KPI 에는 영향이 없다.",
                timeout_sec, _MIN_API_TIMEOUT_SEC, _MIN_API_TIMEOUT_SEC,
            )
            timeout_sec = _MIN_API_TIMEOUT_SEC

        self._model = model
        self._client = genai.Client(
            api_key=api_key,
            http_options=types.HttpOptions(
                timeout=int(timeout_sec * 1000),  # SDK 는 밀리초를 받는다
                retry_options=types.HttpRetryOptions(attempts=1),  # 재시도 없음
            ),
        )
        self._config = types.GenerateContentConfig(
            max_output_tokens=max_tokens,
            thinking_config=types.ThinkingConfig(thinking_level=level),
        )

    def generate(self, system_prompt: str, user_prompt: str) -> str:
        # system_instruction 은 요청마다 달라지므로(POI별 컨텍스트) 여기서 채운다.
        config = self._config.model_copy(update={"system_instruction": system_prompt})

        try:
            response = self._client.models.generate_content(
                model=self._model,
                contents=user_prompt,
                config=config,
            )
        except errors.APIError as exc:
            # 인증/쿼터/서버오류를 한 타입으로 모아 폴백에 넘긴다.
            raise LlmError(f"Gemini 호출 실패: {type(exc).__name__}: {exc}") from exc
        except Exception as exc:  # 타임아웃 등 SDK 가 APIError 로 안 감싸는 경우
            raise LlmError(f"Gemini 호출 실패: {type(exc).__name__}: {exc}") from exc

        self._raise_if_blocked(response)

        answer = (response.text or "").strip()
        if not answer:
            raise LlmError("Gemini 가 빈 응답을 반환했다")
        return answer

    @staticmethod
    def _raise_if_blocked(response: types.GenerateContentResponse) -> None:
        """안전 필터 차단 등 비정상 종료를 LlmError 로 올린다.

        Gemini 는 차단 시 예외가 아니라 candidates 가 비거나
        finish_reason 이 SAFETY/PROHIBITED_CONTENT 로 온다. text 를 바로
        읽으면 None 이라 원인을 알 수 없으므로 여기서 먼저 판별한다.
        """
        feedback = response.prompt_feedback
        if feedback is not None and feedback.block_reason is not None:
            raise LlmError(f"Gemini 가 프롬프트를 차단했다: {feedback.block_reason}")

        if not response.candidates:
            raise LlmError("Gemini 응답에 candidates 가 없다")

        reason = response.candidates[0].finish_reason
        if reason is not None and reason not in _OK_FINISH_REASONS:
            raise LlmError(f"Gemini 가 응답을 중단했다: {reason}")
        if reason == types.FinishReason.MAX_TOKENS:
            logger.warning("Gemini 응답이 max_output_tokens 에서 잘렸다.")
