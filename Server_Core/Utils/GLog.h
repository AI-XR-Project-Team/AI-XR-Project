#pragma once

// ============================================================
//  Utils/GLog.h
//  역할: 멀티스레드 안전 콘솔 로거 (Header-only)
//
//  사용법:
//    GLog::Info("Connected: userId={}", userId);
//    GLog::Warn("Packet too short: {} bytes", len);
//    GLog::Error("WSARecvFrom failed: {}", err);
//    GLog::Debug("SeqNum={} PosX={:.2f}", seq, px);
// ============================================================

#include <iostream>
#include <format>
#include <chrono>
#include <mutex>
#include <string_view>
#include <source_location>

class GLog
{
public:
    // ── 로그 레벨 ──────────────────────────────────
    enum class Level : uint8_t
    {
        Debug = 0,
        Info,
        Warn,
        Error
    };

    // ── Public 로깅 API ────────────────────────────

    template <typename... Args>
    static void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        Log(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        Log(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        Log(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Debug(std::format_string<Args...> fmt, Args&&... args)
    {
#ifdef _DEBUG
        Log(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
#endif
    }

    // ── 최소 출력 레벨 설정 ───────────────────────
    static void SetMinLevel(Level level) noexcept
    {
        s_MinLevel = level;
    }

private:
    GLog() = delete;   // 인스턴스화 금지

    // ── 핵심 출력 함수 ─────────────────────────────
    static void Log(Level level, const std::string& message)
    {
        if (level < s_MinLevel) return;

        // 현재 시각 포맷팅 [HH:MM:SS]
        const auto now       = std::chrono::system_clock::now();
        const auto localTime = std::chrono::zoned_time{
            std::chrono::current_zone(), now
        };
        const std::string timeStr = std::format("{:%H:%M:%S}", localTime.get_local_time());
        // 소수점 이하 제거 (초까지만 표시)
        const std::string timeTag = timeStr.substr(0, 8);

        const std::string_view levelTag = LevelToString(level);
        const std::string      fullLine = std::format("[{}] [{}] {}\n",
                                                       timeTag, levelTag, message);

        // ── 스레드 안전 출력 ──────────────────────
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (level == Level::Error)
        {
            std::cerr << fullLine;
        }
        else
        {
            std::cout << fullLine;
        }
    }

    static constexpr std::string_view LevelToString(Level level) noexcept
    {
        switch (level)
        {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO ";
            case Level::Warn:  return "WARN ";
            case Level::Error: return "ERROR";
            default:           return "?????";
        }
    }

    // ── inline static 멤버 (C++17~, 헤더 단독 링크 가능) ──
    inline static std::mutex s_Mutex{};
    inline static Level      s_MinLevel{ Level::Debug };
};
