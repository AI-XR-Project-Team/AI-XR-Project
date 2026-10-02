"""LLM Provider 추상 계층.

벤더를 바꾸거나 추가할 때는 이 모듈의 `LlmClient` 를 구현한 어댑터 파일을
추가하고 `factory._PROVIDERS` 에 한 줄 등록하기만 하면 된다.
도슨트 서비스(`app.services.docent`)와 라우터는 수정하지 않는다.

인터페이스는 두 개다:
    generate()  — 단발 질의응답. 기존 `POST /docent/ask` 가 쓴다.
    stream()    — 멀티턴 + 토큰 스트리밍. 챗봇이 쓴다.

`stream()` 에는 기본 구현이 있어서, 스트리밍을 지원하지 않는 어댑터는
아무것도 하지 않아도 된다(응답 전체를 한 덩어리로 yield 한다). 덕분에
"벤더 추가 = 파일 1개 + 팩토리 한 줄" 이라는 기존 약속이 유지된다.
"""
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Iterator, List, Literal, Sequence


class LlmError(Exception):
    """LLM 호출 실패(네트워크·타임아웃·인증·응답 파싱 등)를 나타내는 공통 예외.

    벤더별 어댑터는 자신의 SDK 예외를 이 타입으로 감싸서 raise 해야 한다.
    도슨트 서비스는 이 예외를 잡아 DB 의 사전 작성 해설로 폴백한다.
    """


@dataclass(frozen=True)
class ChatTurn:
    """대화 한 턴. 벤더 중립 표현이다.

    Gemini 는 assistant 를 "model" 로 부르는 등 표기가 제각각이라,
    변환은 각 어댑터가 담당하고 서비스 계층은 이 형태만 다룬다.
    """

    role: Literal["user", "assistant"]
    content: str


class LlmClient(ABC):
    """도슨트 응답을 생성하는 LLM 클라이언트."""

    @abstractmethod
    def generate(self, system_prompt: str, user_prompt: str) -> str:
        """system/user 프롬프트를 받아 응답 본문을 반환한다.

        Args:
            system_prompt: 도슨트 페르소나 + DB 에서 조립한 배경 컨텍스트.
            user_prompt: 관람객 질문(없으면 기본 해설 요청 문장).

        Returns:
            응답 본문 문자열.

        Raises:
            LlmError: 호출에 실패한 경우.
        """

    def stream(
        self,
        system_prompt: str,
        history: Sequence[ChatTurn],
        user_prompt: str,
    ) -> Iterator[str]:
        """대화 맥락을 반영한 응답을 조각(chunk) 단위로 흘려보낸다.

        기본 구현은 `generate()` 결과를 한 덩어리로 내보낸다. 스트리밍을
        지원하는 어댑터만 이 메서드를 재정의하면 된다.

        주의: 첫 chunk 를 내보낸 뒤에 실패하면 이미 클라이언트에 일부 텍스트가
        전달된 상태다. 그 경우의 처리(부분 응답 유지 + 에러 이벤트)는 호출부인
        `app.services.docent` 가 담당한다.

        Args:
            system_prompt: 도슨트 페르소나 + 배경 컨텍스트.
            history: 이전 대화 턴들(오래된 것부터). 현재 질문은 포함하지 않는다.
            user_prompt: 이번 관람객 질문.

        Yields:
            응답 본문 조각. 이어 붙이면 전체 응답이 된다.

        Raises:
            LlmError: 호출에 실패한 경우.
        """
        # 기본 구현은 히스토리를 프롬프트에 녹여 단발 호출로 처리한다.
        merged = _merge_history_into_prompt(history, user_prompt)
        yield self.generate(system_prompt, merged)


def _merge_history_into_prompt(
    history: Sequence[ChatTurn], user_prompt: str
) -> str:
    """멀티턴을 지원하지 않는 어댑터용으로 대화를 한 문자열에 담는다.

    벤더 네이티브 멀티턴보다 품질이 떨어지지만, Mock 같은 구현이 후속 질문
    맥락을 아예 잃는 것보다는 낫다.
    """
    if not history:
        return user_prompt

    lines: List[str] = ["[이전 대화]"]
    for turn in history:
        speaker = "관람객" if turn.role == "user" else "도슨트"
        lines.append(f"{speaker}: {turn.content}")
    lines += ["", f"관람객: {user_prompt}"]
    return "\n".join(lines)
