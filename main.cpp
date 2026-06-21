// ============================================================================
//  Bye zapret
//  Консольная утилита для остановки и удаления службы zapret, завершения
//  процесса winws.exe, выгрузки драйверов WinDivert, очистки DNS/ARP-кэша
//  и финального сброса сетевого стека (IP/Winsock).
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#ifdef UNICODE
#undef UNICODE
#endif
#ifdef _UNICODE
#undef _UNICODE
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace zc {

// ============================================================================
// Конфигурация запуска
// ============================================================================
struct AppConfig {
    bool showCube  = true;
    bool fastMode  = false;
};
AppConfig g_config;

void PrintUsage() {
    std::cout <<
        "Bye Zapret - удаление zapret/WinDivert \n\n"
        "Параметры:\n"
        "  --no-cube            отключить анимацию куба\n"
        "  --fast               сократить искусственные задержки анимации\n"
        "  --help               показать эту справку\n";
}

void ParseArguments(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--no-cube") {
            g_config.showCube = false;
        } else if (arg == "--fast") {
            g_config.fastMode = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        }
    }
}

inline int Scaled(int milliseconds) {
    return g_config.fastMode ? std::max(10, milliseconds / 3) : milliseconds;
}

namespace Color {
    const std::string Red     = "\x1b[91m";
    const std::string Green   = "\x1b[92m";
    const std::string Yellow  = "\x1b[93m";
    const std::string Cyan    = "\x1b[96m";
    const std::string Magenta = "\x1b[95m";
    const std::string Gray    = "\x1b[90m";
    const std::string White   = "\x1b[97m";
    const std::string Bold    = "\x1b[1m";
    const std::string Reset   = "\x1b[0m\x1b[35m";
}

// ============================================================================
// Unicode-символы
// ============================================================================
namespace UI {
    const std::string TL = "┌";
    const std::string TR = "┐";
    const std::string BL = "└";
    const std::string BR = "┘";
    const std::string H  = "─";
    const std::string V  = "│";

    const std::string OK   = "✓";
    const std::string FAIL = "✗";
    const std::string WARN = "⚡";
    const std::string INFO = "ℹ";
    const std::string DOT  = "●";
}

class ConsoleUI {
public:
    static const int TextCol     = 0;
    static const int CubeCol     = 55;
    static const int CubeRowBase = 3;
    static const int CubeWidth   = 20;
    static const int CubeHeight  = 10;

    void Init() {
        handle_ = GetStdHandle(STD_OUTPUT_HANDLE);

        DWORD mode = 0;
        GetConsoleMode(handle_, &mode);
        SetConsoleMode(handle_, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // УВЕЛИЧЕН РАЗМЕР ОКНА: 80x40 (было 80x30) - теперь всё помещается
        COORD bufSize{80, 40};
        SetConsoleScreenBufferSize(handle_, bufSize);
        SMALL_RECT winRect{0, 0, 79, 39};
        SetConsoleWindowInfo(handle_, TRUE, &winRect);

        SetConsoleTitleA("bye zapret by d3fpy");
        SetConsoleTextAttribute(handle_, 0x05);

        HideCursor(true);
    }

    void HideCursor(bool hide) {
        CONSOLE_CURSOR_INFO info;
        GetConsoleCursorInfo(handle_, &info);
        info.bVisible = hide ? FALSE : TRUE;
        SetConsoleCursorInfo(handle_, &info);
    }

    void SetCursorPos(int x, int y) {
        COORD c{static_cast<SHORT>(x), static_cast<SHORT>(y)};
        SetConsoleCursorPosition(handle_, c);
    }

    void PrintLine(const std::string& text = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        SetCursorPos(TextCol, row_);
        std::cout << text << "\x1b[K";
        std::cout.flush();
        ++row_;
    }

    void PrintAt(int row, const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetCursorPos(TextCol, row);
        std::cout << text << "\x1b[K";
        std::cout.flush();
    }

    int CurrentRow() const { return row_; }
    std::mutex& Mutex() { return mutex_; }

private:
    HANDLE     handle_ = nullptr;
    std::mutex mutex_;
    int        row_ = 0;
};

class CubeAnimation {
public:
    explicit CubeAnimation(ConsoleUI& console) : console_(console) {}

    void Start() {
        running_ = true;
        thread_ = std::thread([this] { Loop(); });
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        Clear();
    }

private:
    struct Vec3 { double x, y, z; };
    static const int Width  = ConsoleUI::CubeWidth;
    static const int Height = ConsoleUI::CubeHeight;

    void Loop() {
        double angle = 0.0;
        while (running_.load()) {
            DrawFrame(angle);
            angle += 0.18;
            std::this_thread::sleep_for(std::chrono::milliseconds(Scaled(90)));
        }
    }

    static void PlotEdge(char buf[Height][Width], int colorBuf[Height][Width],
                         int x0, int y0, int x1, int y1, int colorNear) {
        int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
        int err = dx + dy;
        while (true) {
            if (x0 >= 0 && x0 < Width && y0 >= 0 && y0 < Height) {
                buf[y0][x0] = '#';
                colorBuf[y0][x0] = colorNear;
            }
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    static void ComputeFrame(double angle, char buf[Height][Width], int colorBuf[Height][Width]) {
        static const Vec3 verts[8] = {
            {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
            {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
        };
        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
        };

        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x) { buf[y][x] = ' '; colorBuf[y][x] = 0; }

        const double ay = angle;
        const double ax = angle * 0.6;

        int    sxArr[8], syArr[8];
        double depth[8];

        for (int i = 0; i < 8; ++i) {
            double x1 =  verts[i].x * std::cos(ay) + verts[i].z * std::sin(ay);
            double z1 = -verts[i].x * std::sin(ay) + verts[i].z * std::cos(ay);
            double y1 =  verts[i].y;

            double y2 = y1 * std::cos(ax) - z1 * std::sin(ax);
            double z2 = y1 * std::sin(ax) + z1 * std::cos(ax);
            double x2 = x1;

            double persp = 1.0 / (3.0 - z2);
            sxArr[i] = Width  / 2 + static_cast<int>(std::lround(x2 * 7.0 * persp * 2.0));
            syArr[i] = Height / 2 + static_cast<int>(std::lround(y2 * 3.5 * persp * 2.0));
            depth[i] = z2;
        }

        for (const auto& e : edges) {
            const double avgDepth = (depth[e[0]] + depth[e[1]]) / 2.0;
            const int color = (avgDepth > 0.0) ? 1 : 2;
            PlotEdge(buf, colorBuf, sxArr[e[0]], syArr[e[0]], sxArr[e[1]], syArr[e[1]], color);
        }
    }

    void DrawFrame(double angle) {
        static char buf[Height][Width];
        static int  colorBuf[Height][Width];
        ComputeFrame(angle, buf, colorBuf);

        std::lock_guard<std::mutex> lock(console_.Mutex());
        for (int y = 0; y < Height; ++y) {
            console_.SetCursorPos(ConsoleUI::CubeCol, ConsoleUI::CubeRowBase + y);
            std::string line;
            for (int x = 0; x < Width; ++x) {
                if (buf[y][x] == ' ') { line += ' '; continue; }
                line += (colorBuf[y][x] == 1) ? Color::Cyan : Color::Gray;
                line += buf[y][x];
                line += Color::Reset;
            }
            std::cout << line << "\x1b[K";
        }
        std::cout.flush();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(console_.Mutex());
        for (int i = 0; i < Height; ++i) {
            console_.SetCursorPos(ConsoleUI::CubeCol, ConsoleUI::CubeRowBase + i);
            std::cout << std::string(Width, ' ');
        }
        std::cout.flush();
    }

    ConsoleUI&        console_;
    std::atomic<bool> running_{false};
    std::thread       thread_;
};

// ============================================================================
// Системные проверки
// ============================================================================
namespace System {
    inline bool IsAdmin() {
        return std::system("net session >nul 2>&1") == 0;
    }
    inline bool ServiceExists(const std::string& serviceName) {
        const std::string cmd =
            "sc query \"" + serviceName + "\" 2>nul | findstr /I \"SERVICE_NAME\" >nul";
        return std::system(cmd.c_str()) == 0;
    }
    inline bool IsProcessRunning(const std::string& processName) {
        const std::string cmd =
            "tasklist /FI \"IMAGENAME eq " + processName + "\" 2>nul | find /I \"" + processName + "\" >nul";
        return std::system(cmd.c_str()) == 0;
    }
    inline bool IsServiceRunning(const std::string& serviceName) {
        const std::string cmd =
            "sc query \"" + serviceName + "\" 2>nul | findstr /I \"RUNNING\" >nul";
        return std::system(cmd.c_str()) == 0;
    }
}

class TaskRunner {
public:
    explicit TaskRunner(ConsoleUI& console) : console_(console) {}

    void Run(const std::string& label, const std::string& cmd, int spinnerSteps = 3) {
        const int row = console_.CurrentRow();
        console_.PrintLine("  " + Color::Yellow + UI::WARN + Color::Reset + " " + label);

        const std::string fullCmd = cmd + " >nul 2>&1";

        std::atomic<bool> done{false};
        std::thread cmdThread([&done, &fullCmd]() {
            std::system(fullCmd.c_str());
            done = true;
        });

        const char frames[] = {'|', '/', '-', '\\'};
        int frameIdx = 0;
        int totalSteps = spinnerSteps * 4;
        for (int i = 0; i < totalSteps && !done.load(); ++i) {
            std::lock_guard<std::mutex> lock(console_.Mutex());
            console_.SetCursorPos(4, row);
            std::cout << "[" << frames[frameIdx] << "]";
            std::cout.flush();
            frameIdx = (frameIdx + 1) % 4;
            std::this_thread::sleep_for(std::chrono::milliseconds(Scaled(150)));
        }

        cmdThread.join();

        const std::string finalLine = "  " + Color::Green + UI::OK + Color::Reset + " " + label;
        console_.PrintAt(row, finalLine);
    }

private:
    ConsoleUI& console_;
};

// ============================================================================
// Шаги очистки
// ============================================================================
namespace Cleanup {
    inline void RemoveZapretService(ConsoleUI& console, TaskRunner& tasks) {
        if (System::ServiceExists("zapret")) {
            tasks.Run(" Останавливаю службу zapret", "net stop zapret", 2);
            tasks.Run(" Удаляю службу zapret", "sc delete zapret", 2);
        } else {
            console.PrintLine("  " + Color::Gray + UI::FAIL + " zapret не установлен" + Color::Reset);
        }
    }

    inline void KillWinwsProcess(ConsoleUI& console, TaskRunner& tasks) {
        if (System::IsProcessRunning("winws.exe")) {
            tasks.Run("Завершаю winws.exe", "taskkill /IM winws.exe /F", 2);
        } else {
            console.PrintLine("  " + Color::Gray + UI::FAIL + " winws.exe не запущен" + Color::Reset);
        }
    }

    inline void RemoveWinDivertDrivers(ConsoleUI& console, TaskRunner& tasks) {
        if (System::ServiceExists("WinDivert") || System::ServiceExists("WinDivert14")) {
            tasks.Run("Выгружаю WinDivert", "net stop WinDivert && sc delete WinDivert", 2);
            tasks.Run("Выгружаю WinDivert14", "net stop WinDivert14 && sc delete WinDivert14", 2);
        } else {
            console.PrintLine("  " + Color::Gray + UI::FAIL + " WinDivert не установлен" + Color::Reset);
        }
    }

    inline void FlushDnsAndArp(TaskRunner& tasks) {
        tasks.Run("Очищаю кэш DNS и ARP", "ipconfig /flushdns && arp -d *", 2);
    }

    inline void ResetNetworkStack(TaskRunner& tasks) {
        tasks.Run("Сброс IP-стека", "netsh int ip reset", 3);
        tasks.Run("Сброс Winsock", "netsh winsock reset", 3);
    }

    struct StatusResult {
        std::string name;
        bool        clean;
    };

    inline std::vector<StatusResult> CheckAllStatuses() {
        std::vector<StatusResult> results;

        bool zapretClean = !System::ServiceExists("zapret") || !System::IsServiceRunning("zapret");
        results.push_back({"zapret service", zapretClean});

        bool winDivertClean = (!System::ServiceExists("WinDivert") || !System::IsServiceRunning("WinDivert"))
                           && (!System::ServiceExists("WinDivert14") || !System::IsServiceRunning("WinDivert14"));
        results.push_back({"WinDivert driver", winDivertClean});

        bool winwsClean = !System::IsProcessRunning("winws.exe");
        results.push_back({"Bypass (winws.exe)", winwsClean});

        return results;
    }

    inline void PrintStatusReport(ConsoleUI& console) {
        auto results = CheckAllStatuses();

        console.PrintLine("");
        console.PrintLine("  " + Color::Cyan + UI::TL + std::string(42, UI::H[0]) + UI::TR + Color::Reset);
        console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "    " + Color::Bold + Color::White + "STATUS REPORT" + Color::Reset + "                           " + Color::Cyan + UI::V + Color::Reset);
        console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "                                            " + Color::Cyan + UI::V + Color::Reset);

        for (const auto& r : results) {
            std::string indicator;
            if (r.clean) {
                indicator = Color::Green + UI::OK + " STOPPED" + Color::Reset;
            } else {
                indicator = Color::Red + UI::FAIL + " ACTIVE " + Color::Reset;
            }

            std::string name = r.name;
            while (name.length() < 22) name += " ";

            console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "  " + indicator + "  " + Color::Gray + name + Color::Reset + " " + Color::Cyan + UI::V + Color::Reset);
        }

        console.PrintLine("  " + Color::Cyan + UI::BL + std::string(42, UI::H[0]) + UI::BR + Color::Reset);
    }
}

} // namespace zc

int main(int argc, char** argv) {
    using namespace zc;
    ParseArguments(argc, argv);

    ConsoleUI console;
    console.Init();

    if (!System::IsAdmin()) {
        console.PrintLine("");
        console.PrintLine("  " + Color::Red + UI::FAIL + " Запустите утилиту от имени администратора!" + Color::Reset);
        console.PrintLine("");
        std::cout.flush();
        Sleep(Scaled(4000));
        return 1;
    }


    console.PrintLine("");
    console.PrintLine("  " + Color::Cyan + UI::TL + std::string(50, UI::H[0]) + UI::TR + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "                                                " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "      " + Color::Bold + Color::White + "bye zapret" + Color::Reset + "  ·  powered by d3fpy           " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "      " + Color::Gray + "github.com/d3fpy" + Color::Reset + "                          " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "                                                " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::BL + std::string(50, UI::H[0]) + UI::BR + Color::Reset);
    console.PrintLine("");

    CubeAnimation cube(console);
    if (g_config.showCube) cube.Start();

    console.PrintLine("  " + Color::Cyan + UI::INFO + Color::Reset + " " + Color::White + "Очистка трафика от winws.exe" + Color::Reset);
    console.PrintLine("");
    console.PrintLine("  " + Color::Yellow + UI::WARN + Color::Reset + " " + Color::Yellow + "Начинается очистка служб..." + Color::Reset);
    console.PrintLine("");

    // Очистка
    TaskRunner tasks(console);
    Cleanup::RemoveZapretService(console, tasks);
    Cleanup::KillWinwsProcess(console, tasks);
    Cleanup::RemoveWinDivertDrivers(console, tasks);
    Cleanup::FlushDnsAndArp(tasks);

    console.PrintLine("");
    console.PrintLine("  " + Color::Yellow + UI::WARN + Color::Reset + " " + Color::Yellow + "Сброс сетевого стека..." + Color::Reset);
    console.PrintLine("");
    Cleanup::ResetNetworkStack(tasks);

    // Проверка статусов
    Cleanup::PrintStatusReport(console);

    console.PrintLine("");
    console.PrintLine("  " + Color::Cyan + UI::TL + std::string(50, UI::H[0]) + UI::TR + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "                                                " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "   " + Color::Green + UI::OK + Color::Reset + " " + Color::White + "Готово!" + Color::Reset + "                                   " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "   " + Color::Gray + "Проблемы? Пишите в Issues." + Color::Reset + "              " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::V + Color::Reset + "                                                " + Color::Cyan + UI::V + Color::Reset);
    console.PrintLine("  " + Color::Cyan + UI::BL + std::string(50, UI::H[0]) + UI::BR + Color::Reset);
    console.PrintLine("");
    console.PrintLine("Нажмите Enter чтобы выйти...");
    std::cout.flush();
    std::cin.get();
    ExitProcess(0);
}
