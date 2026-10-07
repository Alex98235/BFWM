/**
 * @file crash_dump.h
 * @brief Crash dump facility using Vectored Exception Handling.
 *
 * Installs a VEH that intercepts all unhandled exceptions and writes a
 * detailed report (exception code, registers, call stack) to stderr
 * before the process terminates.
 *
 * Usage:
 * @code
 *    CrashDumpInit();  // call once at startup
 * @endcode
 *
 * To translate an offset to a source location (MINGW):
 *   1. Build with a linker map file:
 *      target_link_options(BFWMWM PRIVATE -Wl,-Map=BFWMwm.map)
 *   2. Look up <Offset> in the map to find the nearest function symbol,
 *      or pass the address to `addr2line -e BFWMWM.exe`.
 */

#ifndef BFWM_CRASH_DUMP_H
#define BFWM_CRASH_DUMP_H

#include <windows.h>

/**
 * @brief Install the Vectored Exception Handler.
 *
 * Must be called from the main thread before any risky initialisation.
 * Captures the module base address so crash offsets are stable across
 * ASLR runs.
 */
void CrashDumpInit();

#endif
