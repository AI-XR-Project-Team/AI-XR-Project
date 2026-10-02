// ============================================================
//  main.cpp
//  역할: CoreServer 진입점 - IOCPServer 인스턴스 생성 및 구동
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <atomic>
#include <memory>

#include "Network/IOCPServer.h"
#include "Utils/GLog.h"

// ──────────────────────────────────────────────
//  전역 서버 인스턴스 (Ctrl+C 핸들러에서 접근)
// ──────────────────────────────────────────────
static std::unique_ptr<IOCPServer> g_pServer;
static std::atomic<bool>          g_bInterrupted{ false };

// ──────────────────────────────────────────────
//  Ctrl+C / 프로세스 종료 핸들러
// ──────────────────────────────────────────────
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            GLog::Warn("Interrupt received. Initiating graceful shutdown...");
            g_bInterrupted.store(true, std::memory_order_release);
            if (g_pServer)
                g_pServer->Shutdown();
            return TRUE;  // TRUE: OS가 기본 핸들러를 호출하지 않음
        default:
            return FALSE;
    }
}

// ──────────────────────────────────────────────
//  진입점
// ──────────────────────────────────────────────
int main()
{
    // Windows 콘솔 인코딩을 UTF-8로 설정 (한글 깨짐 방지)
    setvbuf(stdout, nullptr, _IONBF, 0);

    GLog::Info("╔══════════════════════════════════════════╗");
    GLog::Info("║       AI-XR Core Server  v0.1.0          ║");
    GLog::Info("║   IOCP UDP Server  |  Port {}          ║", SERVER_PORT);
    GLog::Info("╚══════════════════════════════════════════╝");

    // ── Ctrl+C 핸들러 등록 ────────────────────
    if (!::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
    {
        GLog::Error("[Main] Failed to set console ctrl handler.");
        return -1;
    }

    // ── 서버 인스턴스 생성 & 시작 ─────────────
    g_pServer = std::make_unique<IOCPServer>();

    if (!g_pServer->Start(SERVER_PORT))
    {
        GLog::Error("[Main] Server failed to start. Exiting.");
        return -1;
    }

    // ── Main 스레드: 수신 루프 / Dispatcher 감독 ──
    // Worker 스레드들이 IOCP 큐에서 완료 패킷을 처리하는 동안,
    // Main 스레드는 RunRecvLoop 에서 1Hz 주기로 헬스 체크 등을 담당합니다.
    g_pServer->RunRecvLoop();

    // ── 정상 종료 경로 ─────────────────────────
    // Ctrl+C 핸들러에서 Shutdown() 을 호출하므로
    // RunRecvLoop 반환 후 추가 정리가 필요하면 여기에 작성합니다.
    GLog::Info("[Main] Server exited cleanly.");
    return 0;
}