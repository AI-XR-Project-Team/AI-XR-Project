#include "SseParser.h"

void FSseParser::AppendBytes(const void* Data, int64 Count)
{
	if (Data == nullptr || Count <= 0)
	{
		return;
	}
	Buffer.Append(static_cast<const uint8*>(Data), static_cast<int32>(Count));
}

void FSseParser::Reset()
{
	Buffer.Reset();
}

bool FSseParser::FindFrameBoundary(int32& OutFrameEnd, int32& OutNextStart) const
{
	// 프레임은 빈 줄로 끝난다. 서버는 \n\n 을 보내지만 SSE 규격상 \r\n\r\n 도
	// 유효하므로 둘 다 본다.
	const int32 Num = Buffer.Num();
	for (int32 i = 0; i + 1 < Num; ++i)
	{
		if (Buffer[i] == '\n' && Buffer[i + 1] == '\n')
		{
			OutFrameEnd = i;
			OutNextStart = i + 2;
			return true;
		}

		if (i + 3 < Num &&
			Buffer[i] == '\r' && Buffer[i + 1] == '\n' &&
			Buffer[i + 2] == '\r' && Buffer[i + 3] == '\n')
		{
			OutFrameEnd = i;
			OutNextStart = i + 4;
			return true;
		}
	}
	return false;
}

bool FSseParser::NextEvent(FSseEvent& OutEvent)
{
	// 주석(keep-alive)만 있는 프레임은 건너뛴다. 호출부가 빈 이벤트를 처리하지
	// 않아도 되게 하기 위함이다. 주석이 연달아 와도 스택이 자라지 않도록 반복문
	// 으로 처리한다.
	for (;;)
	{
		int32 FrameEnd = 0;
		int32 NextStart = 0;
		if (!FindFrameBoundary(FrameEnd, NextStart))
		{
			return false;
		}

		// 여기서 처음으로 문자열이 된다. 프레임이 완결됐으므로 멀티바이트
		// 문자가 중간에 갈리지 않는다.
		FString Frame;
		if (FrameEnd > 0)
		{
			TArray<uint8> FrameBytes;
			FrameBytes.Append(Buffer.GetData(), FrameEnd);
			FrameBytes.Add(0);  // UTF8_TO_TCHAR 는 널 종료 문자열을 기대한다
			Frame = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(FrameBytes.GetData()));
		}

		Buffer.RemoveAt(0, NextStart, EAllowShrinking::No);

		OutEvent = ParseFrame(Frame);
		if (!OutEvent.Event.IsEmpty() || !OutEvent.Data.IsEmpty())
		{
			return true;
		}
	}
}

FSseEvent FSseParser::ParseFrame(const FString& Frame)
{
	FSseEvent Result;

	TArray<FString> Lines;
	Frame.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);

	TArray<FString> DataLines;
	for (FString Line : Lines)
	{
		Line.RemoveFromEnd(TEXT("\r"));

		// ':' 로 시작하는 줄은 주석이다. 서버가 연결 유지용으로 보낼 수 있다.
		if (Line.StartsWith(TEXT(":")))
		{
			continue;
		}

		int32 ColonIndex = INDEX_NONE;
		if (!Line.FindChar(TEXT(':'), ColonIndex))
		{
			continue;
		}

		const FString Field = Line.Left(ColonIndex);
		FString Value = Line.Mid(ColonIndex + 1);
		// 규격상 콜론 뒤 공백 하나는 구분자이므로 값에서 뺀다.
		Value.RemoveFromStart(TEXT(" "));

		if (Field == TEXT("event"))
		{
			Result.Event = Value;
		}
		else if (Field == TEXT("data"))
		{
			// data 는 여러 줄일 수 있고, 그 경우 개행으로 이어 붙인다.
			DataLines.Add(Value);
		}
	}

	Result.Data = FString::Join(DataLines, TEXT("\n"));
	return Result;
}
