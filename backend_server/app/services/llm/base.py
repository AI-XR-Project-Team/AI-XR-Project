"""LLM Provider 추상 계층.

벤더를 바꾸거나 추가할 때는 이 모듈의 `LlmClient` 를 구현한 어댑터 파일을
추가하고 `factory._PROVIDERS` 에 한 줄 등록하기만 하면 된다.
도슨트 서비스(`app.services.docent`)와 라우터는 수정하지 않는다.

인터페이스는 의도적으로 `generate()` 하나만 둔다. 스트리밍·툴콜·멀티턴은
실제 요구가 생길 때 추가한다(현재 도슨트는 단발 질의응답이다).
"""
from abc import ABC, abstractmethod


class LlmError(Exception):
    """LLM 호출 실패(네트워크·타임아웃·인증·응답 파싱 등)를 나타내는 공통 예외.

    벤더별 어댑터는 자신의 SDK 예외를 이 타입으로 감싸서 raise 해야 한다.
    도슨트 서비스는 이 예외를 잡아 DB 의 사전 작성 해설로 폴백한다.
    """


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
