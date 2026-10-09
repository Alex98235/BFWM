/**
 * @file exec_cache.h
 * @brief Non-blocking child-process stdout cache (main-thread only).
 *
 * Custom indicators occasionally need data from an external CLI (a media
 * player, a status tool, ...). Running such a process synchronously would
 * stall the UI thread, so this module starts a child with redirected stdout,
 * polls it without ever blocking, and caches the last complete stdout per
 * caller-supplied key.
 *
 * Threading: the cache is owned by BFWMContext and used only from the main
 * thread. It deliberately touches no Lua state.
 */

#ifndef BFWM_EXEC_CACHE_H
#define BFWM_EXEC_CACHE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Opaque cache handle.
struct ExecCache;

/**
 * @brief Build a Windows command line from an argv vector.
 *
 * Every element is quoted and backslashes are escaped per the
 * CommandLineToArgvW / MSVC rules, so the resulting wide string round-trips
 * through CreateProcessW's command-line parser. The first element is the
 * executable (no shell interpretation).
 *
 * Exposed for unit testing.
 *
 * @param argv Program and arguments (UTF-8)
 * @return The quoted wide command line
 */
auto BuildCommandLine(const std::vector<std::string> &argv) -> std::wstring;

/**
 * @brief Create an empty cache.
 *
 * @param timeout_ms Hard per-child timeout; a child that outlives it is
 *                   terminated. Defaults to 5000 ms (tests pass a small value).
 * @return Cache (never nullptr; throws on allocation failure)
 */
auto ExecCacheCreate(uint64_t timeout_ms = 5000) -> ExecCache *;

/// Terminate and close every in-flight child, then free the cache.
void ExecCacheDestroy(ExecCache *cache);

/**
 * @brief Return the cached stdout for `key`, refreshing it non-blockingly.
 *
 * First harvests finished children. If `key` has a value younger than `ttl_ms`
 * it is returned as-is. Otherwise a child is started for `argv` (unless one is
 * already in flight for this key or the global concurrency cap is reached) and
 * the current cached value (possibly stale, possibly null) is returned
 * immediately.
 *
 * @param cache  The cache (nullptr => nullptr)
 * @param argv   Program and arguments (UTF-8, non-empty)
 * @param key    Cache key (non-empty)
 * @param ttl_ms Freshness window in milliseconds
 * @return Pointer to the cached string (owned by the cache), or nullptr
 */
auto ExecCacheGet(ExecCache *cache, const std::vector<std::string> &argv,
                  const std::string &key, uint64_t ttl_ms)
    -> const std::string *;

/**
 * @brief Fire-and-forget: start a child whose output is discarded.
 *
 * The child is still tracked and reaped by the next ExecCacheGet/Destroy so
 * handles are never leaked.
 *
 * @param cache The cache
 * @param argv  Program and arguments (UTF-8, non-empty)
 */
void ExecCacheSpawn(ExecCache *cache,
                    const std::vector<std::string> &argv);

// -- Diagnostics (for tests) ----------------------------------------------

/// Number of children currently in flight.
auto ExecCacheInFlightCount(const ExecCache *cache) -> size_t;

/// Number of distinct keys currently retained (capped).
auto ExecCacheEntryCount(const ExecCache *cache) -> size_t;

/// Number of StartChild attempts made (successes, failures, cap rejections).
auto ExecCacheStartAttempts(const ExecCache *cache) -> uint64_t;

#endif
