#include "crash_report.hpp"
#include "branding.hpp"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>
#endif

namespace dune3d {

#ifdef _WIN32
namespace {
// filled in at start: there's no telling what still works once something crashed
wchar_t crash_dir[MAX_PATH];

const char *describe(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "invalid memory access";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "division by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR:
        return "page error";
    case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN, also abort() / a failed check
        return "stopped by a failed check";
    default:
        return "error";
    }
}

// "libgtk-4-1.dll+0x123abc"
std::string where(const void *address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(address), &module)
        || !module) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%p", address);
        return buf;
    }
    char name[MAX_PATH] = {};
    GetModuleFileNameA(module, name, MAX_PATH);
    std::string n = name;
    if (auto slash = n.find_last_of("\\/"); slash != std::string::npos)
        n = n.substr(slash + 1);
    char buf[64];
    snprintf(buf, sizeof(buf), "+0x%llx",
             static_cast<unsigned long long>(reinterpret_cast<const char *>(address)
                                             - reinterpret_cast<const char *>(module)));
    return n + buf;
}

void report(const std::string &what)
{
    // where the calls came from, innermost first
    std::string trace;
    void *frames[48];
    const auto n = CaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < n; i++)
        trace += "  " + where(frames[i]) + "\r\n";

    time_t now = time(nullptr);
    tm t;
    localtime_s(&t, &now);
    wchar_t file[MAX_PATH + 64];
    swprintf(file, MAX_PATH + 64, L"%ls\\crash-%04d%02d%02d-%02d%02d%02d.txt", crash_dir, t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    bool saved = false;
    if (FILE *f = _wfopen(file, L"wb")) {
        fprintf(f, "%s %s crashed\r\n\r\n%s\r\n\r\nCalls:\r\n%s", app_name, app_version, what.c_str(),
                trace.c_str());
        fclose(f);
        saved = true;
    }

    std::string text = std::string(app_name) + " ran into an error and has to close.\n\n" + what + "\n\n";
    text += "Unsaved work is backed up every few minutes and offered again at the next start.\n\n";
    if (saved) {
        char path[MAX_PATH + 64];
        WideCharToMultiByte(CP_UTF8, 0, file, -1, path, sizeof(path), nullptr, nullptr);
        text += std::string("The details are in:\n") + path + "\n\n";
    }
    text += "Open the problem report page, to tell us about it?";
    wchar_t wtext[4096];
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext, 4096);
    wchar_t wtitle[128];
    MultiByteToWideChar(CP_UTF8, 0, app_name, -1, wtitle, 128);
    if (MessageBoxW(nullptr, wtext, wtitle, MB_YESNO | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND) == IDYES)
        ShellExecuteA(nullptr, "open", issues_url, nullptr, nullptr, SW_SHOWNORMAL);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS *ep)
{
    static bool once = false;
    if (once)
        return EXCEPTION_CONTINUE_SEARCH;
    once = true;
    const auto code = ep->ExceptionRecord->ExceptionCode;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s (0x%08lx) at ", describe(code), static_cast<unsigned long>(code));
    report(buf + where(ep->ExceptionRecord->ExceptionAddress));
    return EXCEPTION_EXECUTE_HANDLER; // ends the process without Windows' own dialog
}

void on_terminate()
{
    std::string what = "A C++ exception nobody caught";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        }
        catch (const std::exception &e) {
            what += ": " + std::string(e.what());
        }
        catch (...) {
        }
    }
    report(what);
    _exit(3);
}
} // namespace

void install_crash_report(const std::filesystem::path &dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    wcsncpy(crash_dir, dir.wstring().c_str(), MAX_PATH - 1);
    SetUnhandledExceptionFilter(on_exception);
    std::set_terminate(on_terminate);
}
#else
void install_crash_report(const std::filesystem::path &)
{
}
#endif

} // namespace dune3d
