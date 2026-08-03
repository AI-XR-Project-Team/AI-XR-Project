#pragma once

#include "CoreMinimal.h"

/** SSE 프레임 하나. `event:` 와 `data:` 줄을 담는다. */
struct FSseEvent
{
	FString Event;
	FString Data;
};

/**
 * Server-Sent Events 스트림 파서.
 *
 * UObject 가 아닌 순수 클래스다. HTTP·게임스레드와 분리해 두면 파싱 규칙만
 * 따로 검증할 수 있다.
 *
 * **바이트 단위로 버퍼링하는 이유**: 서버 응답은 한글이라 UTF-8 멀티바이트다.
 * 수신 청크는 임의 지점에서 잘리므로, 도착한 바이트를 그때그때 문자열로 바꾸면
 * 멀티바이트 문자가 중간에 갈라져 깨진다. 프레임 구분자(\n\n)는 ASCII 라
 * 바이트 상태로 안전하게 찾을 수 있으므로, **완성된 프레임만** 문자열로 바꾼다.
 *
 * 사용법:
 *     Parser.AppendBytes(Ptr, Length);   // 수신 스레드에서
 *     FSseEvent Event;
 *     while (Parser.NextEvent(Event)) { ... }
 */
class FSseParser
{
public:
	/** 수신한 원시 바이트를 버퍼에 덧붙인다. */
	void AppendBytes(const void* Data, int64 Count);

	/**
	 * 완성된 프레임이 있으면 하나 꺼낸다.
	 *
	 * @return 꺼낼 프레임이 있으면 true. 버퍼에 아직 완결되지 않은 조각만
	 *         남아 있으면 false(다음 수신을 기다린다).
	 */
	bool NextEvent(FSseEvent& OutEvent);

	/** 새 요청을 시작할 때 이전 잔여 데이터를 비운다. */
	void Reset();

private:
	/**
	 * 프레임 경계(빈 줄)를 찾는다.
	 *
	 * @param OutFrameEnd     프레임 본문의 끝 위치(구분자 제외)
	 * @param OutNextStart    다음 프레임의 시작 위치(구분자 다음)
	 * @return 경계를 찾았으면 true
	 */
	bool FindFrameBoundary(int32& OutFrameEnd, int32& OutNextStart) const;

	/** 완성된 프레임 텍스트를 event/data 로 해석한다. */
	static FSseEvent ParseFrame(const FString& Frame);

	TArray<uint8> Buffer;
};
