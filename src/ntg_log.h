#ifndef NTG_LOG_H
#define NTG_LOG_H

// nettoge.com: debug log (ntg_debug.log next to the exe) and crash report.
//
// fallout.cfg  [debug] ntg_log=0..3   (environment NTG_LOG wins)
//   0 off, 1 problems only (default of the play build), 2 + what was loaded
//   (default of the shoot build), 3 + every file open and script call.
// The previous run is kept as ntg_debug.prev.log. A crash writes the call
// stack to the log and a minidump to ntg_crash.dmp.

namespace fallout {

enum {
    NTG_LOG_ERR = 1,
    NTG_LOG_INFO = 2,
    NTG_LOG_TRACE = 3,
};

// Opens the log, installs the crash handler and takes over debug_printf.
// Safe to call before anything else is ready.
void ntg_log_init(int argc, char** argv);

// Reads the level from the game config and writes the setup (config values,
// loose files that override the DAT files).
void ntg_log_config();

int ntg_log_level();
void ntg_log(int level, const char* category, const char* format, ...);

// What the game is doing now, written with a crash report.
void ntg_log_ctx_map(const char* name);
void ntg_log_ctx_script(const char* name, int action);
const char* ntg_log_ctx_script_name();
void ntg_log_ctx_msg(const char* path);

} // namespace fallout

#endif /* NTG_LOG_H */
