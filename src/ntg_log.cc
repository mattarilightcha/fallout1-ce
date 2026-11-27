// nettoge.com: debug log and crash report. See ntg_log.h.
#include "ntg_log.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dbghelp.h>
#endif

#include "game/gconfig.h"
#include "game/version.h"
#include "platform_compat.h"
#include "plib/gnw/debug.h"

namespace fallout {

#define NTG_LOG_FILE "ntg_debug.log"
#define NTG_LOG_PREV "ntg_debug.prev.log"
#define NTG_DUMP_FILE "ntg_crash.dmp"

// Stop repeating one problem line after this many times (then every 500th).
#define NTG_REPEAT_FIRST 5
#define NTG_REPEAT_EVERY 500
#define NTG_REPEAT_KEYS 4096

static FILE* g_fp = NULL;
#ifdef NTG_SHOOT
static int g_level = NTG_LOG_INFO;
#else
static int g_level = NTG_LOG_ERR;
#endif
static bool g_in_log = false;
static std::chrono::steady_clock::time_point g_start;
static std::map<std::string, int>* g_seen = NULL;

static char g_ctx_map[64] = "";
static char g_ctx_script[64] = "";
static int g_ctx_action = -1;
static char g_ctx_msg[128] = "";

// debug_printf sends pieces; a line is written when its newline arrives.
static char g_sink[1024];
static size_t g_sink_len = 0;

static const char* const action_names[] = {
    "no_p_proc", "start", "spatial_p_proc", "description_p_proc", "pickup_p_proc",
    "drop_p_proc", "use_p_proc", "use_obj_on_p_proc", "use_skill_on_p_proc",
    "use_ad_on_p_proc", "use_disad_on_p_proc", "talk_p_proc", "critter_p_proc", "combat_p_proc",
    "damage_p_proc", "map_enter_p_proc", "map_exit_p_proc", "create_p_proc",
    "destroy_p_proc", "barter_init_p_proc", "barter_p_proc", "look_at_p_proc",
    "timed_event_p_proc", "map_update_p_proc"
};

static const char* action_name(int action)
{
    if (action >= 0 && action < (int)(sizeof(action_names) / sizeof(action_names[0]))) {
        return action_names[action];
    }
    return "?";
}

static double elapsed()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

static void write_line(int level, const char* category, const char* text)
{
    if (g_fp == NULL) {
        return;
    }

    static const char marks[] = "?EIT";
    fprintf(g_fp, "[%9.3f] %c %-6s %s\n", elapsed(), marks[level >= 1 && level <= 3 ? level : 0], category, text);
    fflush(g_fp);
}

// true = write this line. Problem lines that repeat are counted instead.
static bool repeat_gate(const char* category, const char* text, char* note, size_t note_size)
{
    note[0] = '\0';
    if (g_seen == NULL) {
        return true;
    }

    std::string key = std::string(category) + "|" + text;
    auto it = g_seen->find(key);
    if (it == g_seen->end()) {
        if (g_seen->size() < NTG_REPEAT_KEYS) {
            (*g_seen)[key] = 1;
        }
        return true;
    }

    int count = ++(it->second);
    if (count <= NTG_REPEAT_FIRST) {
        return true;
    }
    if (count == NTG_REPEAT_FIRST + 1) {
        snprintf(note, note_size, "  (repeats; from here every %dth time is written)", NTG_REPEAT_EVERY);
        return true;
    }
    if (count % NTG_REPEAT_EVERY == 0) {
        snprintf(note, note_size, "  (x%d)", count);
        return true;
    }
    return false;
}

static void emit(int level, const char* category, char* text)
{
    // one entry = one line
    for (char* p = text; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            *p = ' ';
        }
    }

    if (level == NTG_LOG_ERR) {
        char note[80];
        if (!repeat_gate(category, text, note, sizeof(note))) {
            return;
        }
        if (note[0] != '\0') {
            std::string with_note = std::string(text) + note;
            write_line(level, category, with_note.c_str());
            return;
        }
    }

    write_line(level, category, text);
}

int ntg_log_level()
{
    return g_level;
}

void ntg_log(int level, const char* category, const char* format, ...)
{
    if (level > g_level || g_fp == NULL || g_in_log) {
        return;
    }

    g_in_log = true;

    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    emit(level, category, text);

    g_in_log = false;
}

void ntg_log_ctx_map(const char* name)
{
    snprintf(g_ctx_map, sizeof(g_ctx_map), "%s", name != NULL ? name : "");
}

void ntg_log_ctx_script(const char* name, int action)
{
    snprintf(g_ctx_script, sizeof(g_ctx_script), "%s", name != NULL ? name : "");
    g_ctx_action = action;
    if (g_level >= NTG_LOG_TRACE) {
        ntg_log(NTG_LOG_TRACE, "script", "%s %s(%d)", g_ctx_script, action_name(action), action);
    }
}

const char* ntg_log_ctx_script_name()
{
    return g_ctx_script;
}

void ntg_log_ctx_msg(const char* path)
{
    snprintf(g_ctx_msg, sizeof(g_ctx_msg), "%s", path != NULL ? path : "");
}

static void write_context()
{
    char text[512];
    snprintf(text, sizeof(text), "context: map=%s script=%s %s(%d) last message file=%s",
        g_ctx_map[0] != '\0' ? g_ctx_map : "-",
        g_ctx_script[0] != '\0' ? g_ctx_script : "-",
        action_name(g_ctx_action), g_ctx_action,
        g_ctx_msg[0] != '\0' ? g_ctx_msg : "-");
    write_line(NTG_LOG_ERR, "crash", text);
}

// The engine's own messages (debug_printf). Nothing listened to them before.
static int sink_is_problem(const char* text)
{
    static const char* const words[] = {
        "rror", "ERROR", "can't", "Can't", "cannot", "Cannot", "fail", "Fail", "FAIL",
        "nable to", "issing", "not found", "Not found", "Out of memory", "overflow",
        "underflow", "Bad ", "bad ", "nvalid", "couldn't", "Couldn't", "WARNING", "arning"
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strstr(text, words[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void sink_flush()
{
    g_sink[g_sink_len] = '\0';
    g_sink_len = 0;

    char* p = g_sink;
    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        return;
    }

    int level = sink_is_problem(p) ? NTG_LOG_ERR : NTG_LOG_INFO;
    if (level > g_level || g_fp == NULL || g_in_log) {
        return;
    }

    g_in_log = true;
    if (level == NTG_LOG_ERR && g_ctx_script[0] != '\0') {
        char text[1200];
        snprintf(text, sizeof(text), "%s   [script %s %s, map %s]", p, g_ctx_script, action_name(g_ctx_action), g_ctx_map[0] != '\0' ? g_ctx_map : "-");
        emit(level, "engine", text);
    } else {
        emit(level, "engine", p);
    }
    g_in_log = false;
}

static int sink(char* string)
{
    if (string == NULL) {
        return -1;
    }

    // The engine writes "\nmessage": the newline in front ends the piece before it.
    for (const char* p = string; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r') {
            if (g_sink_len != 0) {
                sink_flush();
            }
            continue;
        }
        if (g_sink_len + 1 < sizeof(g_sink)) {
            g_sink[g_sink_len++] = *p;
        } else {
            sink_flush();
        }
    }

    // A piece that ends a sentence and has no newline after it would wait for
    // the next message; write it now.
    size_t n = strlen(string);
    if (n != 0 && g_sink_len != 0 && (string[n - 1] == '!' || string[n - 1] == '.')) {
        sink_flush();
    }

    return 0;
}

#ifdef _WIN32
static const char* exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    case 0xE06D7363:
        return "C++ exception";
    case 0xC0000409:
        return "STACK_BUFFER_OVERRUN / fast fail";
    default:
        return "?";
    }
}

static void write_stack(CONTEXT* context_in)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    CONTEXT context = *context_in;

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    bool symbols = SymInitialize(process, NULL, TRUE) != FALSE;

    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));
    DWORD machine;
#if defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#elif defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#else
    return;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    DWORD64 base = (DWORD64)(uintptr_t)GetModuleHandleA(NULL);

    for (int depth = 0; depth < 48; depth++) {
        if (!StackWalk64(machine, process, thread, &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }

        char text[700];
        char name[300] = "?";
        char where[300] = "";
        DWORD64 addr = frame.AddrPC.Offset;

        if (symbols) {
            char buffer[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
            memset(buffer, 0, sizeof(buffer));
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, addr, &displacement, symbol)) {
                snprintf(name, sizeof(name), "%s+0x%X", symbol->Name, (unsigned)displacement);
            }

            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymGetLineFromAddr64(process, addr, &line_displacement, &line)) {
                snprintf(where, sizeof(where), "  %s:%lu", line.FileName, line.LineNumber);
            }
        }

        snprintf(text, sizeof(text), "#%02d 0x%08llX (exe+0x%llX) %s%s", depth,
            (unsigned long long)addr, (unsigned long long)(addr - base), name, where);
        write_line(NTG_LOG_ERR, "crash", text);
    }
}

static void write_dump(EXCEPTION_POINTERS* info)
{
    HANDLE file = CreateFileA(NTG_DUMP_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
        (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs),
        info != NULL ? &mei : NULL, NULL, NULL);
    CloseHandle(file);

    write_line(NTG_LOG_ERR, "crash", ok ? "minidump written: " NTG_DUMP_FILE : "minidump failed");
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS* info)
{
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (g_fp != NULL) {
        if (g_sink_len != 0) {
            g_in_log = false;
            sink_flush();
        }

        EXCEPTION_RECORD* record = info->ExceptionRecord;
        DWORD64 base = (DWORD64)(uintptr_t)GetModuleHandleA(NULL);
        char text[400];
        snprintf(text, sizeof(text), "*** CRASH *** exception 0x%08lX %s at 0x%p (exe+0x%llX)",
            record->ExceptionCode, exception_name(record->ExceptionCode), record->ExceptionAddress,
            (unsigned long long)((DWORD64)(uintptr_t)record->ExceptionAddress - base));
        write_line(NTG_LOG_ERR, "crash", text);

        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            snprintf(text, sizeof(text), "%s address 0x%p",
                record->ExceptionInformation[0] == 0 ? "read of" : (record->ExceptionInformation[0] == 1 ? "write to" : "execute at"),
                (void*)record->ExceptionInformation[1]);
            write_line(NTG_LOG_ERR, "crash", text);
        }

        write_context();
        write_stack(info->ContextRecord);
        write_dump(info);
        fflush(g_fp);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static void raise_for_report(const char* why)
{
    if (g_fp != NULL) {
        char text[200];
        snprintf(text, sizeof(text), "*** %s ***", why);
        write_line(NTG_LOG_ERR, "crash", text);
    }
    // Goes through crash_filter, which writes the stack and the dump.
    RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

static void on_abort(int)
{
    raise_for_report("abort()");
}

static void on_terminate()
{
    raise_for_report("std::terminate (uncaught C++ exception)");
    abort();
}

static void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    raise_for_report("C runtime: invalid parameter");
}

static void on_purecall()
{
    raise_for_report("pure virtual call");
}
#endif

static void on_exit_normal()
{
    if (g_fp != NULL) {
        if (g_sink_len != 0) {
            sink_flush();
        }
        write_line(NTG_LOG_ERR, "log", "exit (normal)");
        fclose(g_fp);
        g_fp = NULL;
    }
}

void ntg_log_init(int argc, char** argv)
{
    if (g_fp != NULL) {
        return;
    }

    g_start = std::chrono::steady_clock::now();

    const char* env = getenv("NTG_LOG");
    if (env != NULL && env[0] >= '0' && env[0] <= '3') {
        g_level = env[0] - '0';
    }

    remove(NTG_LOG_PREV);
    rename(NTG_LOG_FILE, NTG_LOG_PREV);

    g_fp = fopen(NTG_LOG_FILE, "w");
    if (g_fp == NULL) {
        return;
    }

    g_seen = new std::map<std::string, int>();

    char text[1024];
    char version[32];
    getverstr(version, sizeof(version));

    time_t now = time(NULL);
    char stamp[32] = "";
    struct tm* tm = localtime(&now);
    if (tm != NULL) {
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tm);
    }

#ifdef NTG_SHOOT
    const char* build = "shoot";
#else
    const char* build = "play";
#endif
    snprintf(text, sizeof(text), "nettoge.com Fallout CE (%s build, %s, built %s %s) started %s", build, version, __DATE__, __TIME__, stamp);
    write_line(NTG_LOG_ERR, "log", text);

    std::error_code ec;
    std::string cwd = std::filesystem::current_path(ec).string();
    snprintf(text, sizeof(text), "folder: %s", cwd.c_str());
    write_line(NTG_LOG_ERR, "log", text);

    std::string args;
    for (int i = 0; i < argc; i++) {
        args += argv[i];
        args += ' ';
    }
    snprintf(text, sizeof(text), "command line: %s", args.c_str());
    write_line(NTG_LOG_ERR, "log", text);

    debug_register_func(sink);
    atexit(on_exit_normal);

#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_filter);
    signal(SIGABRT, on_abort);
    std::set_terminate(on_terminate);
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_purecall_handler(on_purecall);
    // abort() must reach the handler instead of showing a dialog
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

static void log_file_info(const char* label, const char* path)
{
    if (path == NULL || path[0] == '\0') {
        ntg_log(NTG_LOG_ERR, "setup", "%s: (not set)", label);
        return;
    }

    std::error_code ec;
    std::filesystem::path p(path);
    if (std::filesystem::is_regular_file(p, ec)) {
        ntg_log(NTG_LOG_ERR, "setup", "%s: %s (%llu bytes)", label, path, (unsigned long long)std::filesystem::file_size(p, ec));
    } else if (std::filesystem::is_directory(p, ec)) {
        ntg_log(NTG_LOG_ERR, "setup", "%s: %s (folder)", label, path);
    } else {
        ntg_log(NTG_LOG_ERR, "setup", "%s: %s  ** not found **", label, path);
    }
}

// The loose files are what a mod adds or replaces. Counts always, each file
// from level 2.
static void log_loose_files(const char* root)
{
    if (root == NULL || root[0] == '\0') {
        return;
    }

    std::error_code ec;
    std::filesystem::path base(root);
    if (!std::filesystem::is_directory(base, ec)) {
        return;
    }

    std::map<std::string, int> counts;
    int total = 0;

    for (std::filesystem::recursive_directory_iterator it(base, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }

        std::filesystem::path rel = std::filesystem::relative(it->path(), base, ec);
        std::string rel_str = rel.generic_string();

        std::string top = rel.has_parent_path() ? rel.begin()->string() : std::string(".");
        for (auto& ch : top) {
            ch = (char)tolower((unsigned char)ch);
        }
        if (top == "savegame") {
            continue;
        }

        std::string ext = rel.extension().string();
        for (auto& ch : ext) {
            ch = (char)tolower((unsigned char)ch);
        }
        counts[top + "/*" + ext]++;
        total++;

        if (g_level >= NTG_LOG_INFO) {
            auto ftime = std::filesystem::last_write_time(it->path(), ec);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            time_t tt = std::chrono::system_clock::to_time_t(sctp);
            char stamp[32] = "";
            struct tm* tm = localtime(&tt);
            if (tm != NULL) {
                strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", tm);
            }
            ntg_log(NTG_LOG_INFO, "loose", "%s  %llu  %s", rel_str.c_str(), (unsigned long long)it->file_size(ec), stamp);
        }
    }

    std::string summary;
    for (auto& kv : counts) {
        summary += kv.first + "=" + std::to_string(kv.second) + " ";
    }
    ntg_log(NTG_LOG_ERR, "setup", "loose files under %s: %d  %s", root, total, summary.c_str());
}

void ntg_log_config()
{
    int level;
    if (getenv("NTG_LOG") == NULL && config_get_value(&game_config, GAME_CONFIG_DEBUG_KEY, "ntg_log", &level)) {
        if (level >= 0 && level <= 3) {
            g_level = level;
        }
    }

    if (g_fp == NULL) {
        return;
    }

    char text[64];
    snprintf(text, sizeof(text), "log level %d", g_level);
    write_line(NTG_LOG_ERR, "log", text);

    char* value = NULL;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_LANGUAGE_KEY, &value)) {
        ntg_log(NTG_LOG_ERR, "setup", "language: %s", value);
    }

    char* master_patches = NULL;
    char* critter_patches = NULL;
    value = NULL;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, &value)) {
        log_file_info("master_dat", value);
    }
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_PATCHES_KEY, &master_patches)) {
        log_file_info("master_patches", master_patches);
    }
    value = NULL;
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, &value)) {
        log_file_info("critter_dat", value);
    }
    if (config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_PATCHES_KEY, &critter_patches)) {
        log_file_info("critter_patches", critter_patches);
    }

#ifdef NTG_SHOOT
    // Test of the crash report: set NTG_LOG_CRASHTEST=1 (shoot build only).
    if (getenv("NTG_LOG_CRASHTEST") != NULL) {
        ntg_log_ctx_map("CRASHTEST.MAP");
        ntg_log_ctx_script("scripts/crashtest.int", 11);
        volatile int* nowhere = NULL;
        *nowhere = 1;
    }
#endif

    log_loose_files(master_patches);
    if (critter_patches != NULL && (master_patches == NULL || compat_stricmp(critter_patches, master_patches) != 0)) {
        log_loose_files(critter_patches);
    }
}

} // namespace fallout
