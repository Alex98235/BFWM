#include "exec_cache.h"

#include "../logging/logger.h"

#include <cstddef>
#include <cstdint>
#include <handleapi.h>
#include <minwindef.h>
#include <namedpipeapi.h>
#include <processthreadsapi.h>
#include <string>
#include <synchapi.h>
#include <utility>
#include <vector>
#include <windef.h>
#include <windows.h>
#include <winnt.h>

namespace {

enum {
   MAX_OUTPUT_BYTES = 64 * 1024, ///< per-child stdout capture cap
   MAX_CONCURRENT_CHILDREN = 16, ///< global in-flight cap
   MAX_ENTRIES = 64,             ///< distinct keys retained
   READ_CHUNK = 4096,            ///< pipe read granularity
};

constexpr uint64_t DEFAULT_TIMEOUT_MS = 5000;

/**
 * @brief RAII owner for a Win32 HANDLE.
 *
 * Move-only: moving transfers ownership and nulls the source, so an ExecChild
 * can be moved into a vector without ever double-closing.
 */
class ScopedHandle {
 public:
   ScopedHandle() = default;
   explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
   ~ScopedHandle() { reset(); }
   ScopedHandle(const ScopedHandle &) = delete;
   auto operator=(const ScopedHandle &) -> ScopedHandle & = delete;
   ScopedHandle(ScopedHandle &&other) noexcept : handle_(other.handle_) {
      other.handle_ = nullptr;
   }
   auto operator=(ScopedHandle &&other) noexcept -> ScopedHandle & {
      if (this != &other) {
         reset();
         handle_ = other.handle_;
         other.handle_ = nullptr;
      }
      return *this;
   }
   [[nodiscard]] auto get() const -> HANDLE { return handle_; }
   void reset(HANDLE handle = nullptr) {
      if (handle_ != nullptr)
         CloseHandle(handle_);
      handle_ = handle;
   }
   explicit operator bool() const { return handle_ != nullptr; }

 private:
   HANDLE handle_ = nullptr;
};

/**
 * @brief A cached key.
 *
 * Field groups:
 *   - `value` / `updated_ms` / `has_value`: the last completed stdout and when
 *     it was recorded (drives the freshness/TTL gate).
 *   - `last_attempt_ms` / `attempted`: a *failed* start timestamp (retry
 *     backoff). Cap rejection never sets these.
 *   - `in_flight`: a child is currently running for this key (coalescing).
 */
struct ExecEntry {
   std::string key;
   std::string value;
   uint64_t updated_ms = 0;
   uint64_t last_attempt_ms = 0;
   bool attempted = false;
   bool has_value = false;
   bool in_flight = false;
};

/**
 * @brief One running child.
 *
 * Both handles are RAII-owned and every member is nothrow-movable, so moving
 * an ExecChild (into the children vector or the harvest survivor vector) never
 * leaks and never double-closes. An empty `key` marks a fire-and-forget spawn.
 */
struct ExecChild {
   ScopedHandle process;
   ScopedHandle pipe;
   std::string collected;
   uint64_t started_ms = 0;
   std::string key;
};

} // namespace

/// The opaque cache (defined here; header only forward-declares it).
struct ExecCache {
   std::vector<ExecEntry> entries;
   std::vector<ExecChild> children;
   uint64_t timeout_ms = DEFAULT_TIMEOUT_MS;
   uint64_t start_attempts = 0; ///< diagnostic counter
};

namespace {

auto Utf8ToWide(const std::string &utf8) -> std::wstring {
   if (utf8.empty())
      return {};
   // MB_ERR_INVALID_CHARS: an invalid byte sequence fails the call (we then
   // treat the argument as empty) instead of silently substituting U+FFFD.
   int const len =
       MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
                           static_cast<int>(utf8.size()), nullptr, 0);
   if (len <= 0)
      return {};
   std::wstring wide(static_cast<size_t>(len), L'\0');
   MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
                       static_cast<int>(utf8.size()), wide.data(), len);
   return wide;
}

auto FindEntry(ExecCache *cache, const std::string &key) -> ExecEntry * {
   for (ExecEntry &entry : cache->entries) {
      if (entry.key == key)
         return &entry;
   }
   return nullptr;
}

auto GetOrCreateEntry(ExecCache *cache, const std::string &key) -> ExecEntry * {
   if (ExecEntry *existing = FindEntry(cache, key))
      return existing;
   if (cache->entries.size() >= MAX_ENTRIES)
      return nullptr;
   cache->entries.push_back(ExecEntry{});
   cache->entries.back().key = key;
   return &cache->entries.back();
}

/// Read whatever is currently buffered without blocking. Returns when the pipe
/// has no data ready, is broken, or the capture cap is reached.
void DrainAvailable(ExecChild &child) {
   if (!child.pipe)
      return;
   for (;;) {
      if (child.collected.size() >= MAX_OUTPUT_BYTES)
         return;
      DWORD available = 0;
      if (PeekNamedPipe(child.pipe.get(), nullptr, 0, nullptr, &available,
                        nullptr) == 0)
         return;
      if (available == 0)
         return;

      // NOLINTNEXTLINE(modernize-avoid-c-arrays)
      char buffer[READ_CHUNK];
      DWORD const want = (available < READ_CHUNK) ? available : READ_CHUNK;
      DWORD got = 0;
      if ((ReadFile(child.pipe.get(), buffer, want, &got, nullptr) == 0) ||
          (got == 0))
         return;
      size_t const room = MAX_OUTPUT_BYTES - child.collected.size();
      child.collected.append(buffer, (got < room) ? got : room);
   }
}

enum class StartResult { Started, CapReached, Failed };

/// Start `argv` with captured stdout, associating the result with `key`
/// (empty = discard). Never blocks. Only the handles the child needs are
/// inherited (see the handle list below).
auto StartChild(ExecCache *cache, const std::string &key,
                const std::vector<std::string> &argv, uint64_t now)
    -> StartResult {
   if (argv.empty() || argv[0].empty())
      return StartResult::Failed;
   cache->start_attempts++;
   if (cache->children.size() >= MAX_CONCURRENT_CHILDREN)
      return StartResult::CapReached;

   SECURITY_ATTRIBUTES security = {};
   security.nLength = sizeof(security);
   security.bInheritHandle = TRUE;

   HANDLE read_raw = nullptr;
   HANDLE write_raw = nullptr;
   if (CreatePipe(&read_raw, &write_raw, &security, 0) == 0)
      return StartResult::Failed;
   ScopedHandle read_end(read_raw);
   ScopedHandle write_end(write_raw);

   // The child must not inherit the read end. (The handle list below already
   // excludes it on the normal path; this also covers the rare fallback where
   // the attribute list cannot be built.) Failure closes both ends via RAII.
   if (SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0) == 0) {
      Debug("exec_cache: SetHandleInformation failed (error %lu)",
            static_cast<unsigned long>(GetLastError()));
      return StartResult::Failed;
   }

   // stdin and stderr go to NUL: the captured data channel is stdout-only.
   ScopedHandle const nul_in(CreateFileW(L"NUL", GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         &security, OPEN_EXISTING, 0, nullptr));
   ScopedHandle const nul_err(
       CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                   &security, OPEN_EXISTING, 0, nullptr));

   std::wstring const command = BuildCommandLine(argv);
   std::vector<wchar_t> mutable_command(command.begin(), command.end());
   mutable_command.push_back(L'\0');

   // Restrict inheritance to exactly the three handles the child needs,
   // instead of every inheritable handle in the process.
   // NOLINTNEXTLINE(modernize-avoid-c-arrays)
   HANDLE inherited[] = {write_end.get(), nul_in.get(), nul_err.get()};
   SIZE_T attribute_size = 0;
   (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
   std::vector<char> attribute_buffer(attribute_size);
   auto *attributes =
       reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
   bool attribute_ok = InitializeProcThreadAttributeList(attributes, 1, 0,
                                                         &attribute_size) != 0;
   if (attribute_ok) {
      attribute_ok = UpdateProcThreadAttribute(
                         attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                         static_cast<void *>(inherited), sizeof(inherited),
                         nullptr, nullptr) != 0;
   }

   STARTUPINFOEXW startup = {};
   startup.StartupInfo.cb =
       attribute_ok ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW);
   startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
   startup.StartupInfo.wShowWindow = SW_HIDE;
   startup.StartupInfo.hStdInput = nul_in.get();
   startup.StartupInfo.hStdOutput = write_end.get();
   startup.StartupInfo.hStdError = nul_err.get();
   startup.lpAttributeList = attribute_ok ? attributes : nullptr;

   DWORD const flags =
       CREATE_NO_WINDOW | (attribute_ok ? EXTENDED_STARTUPINFO_PRESENT : 0);

   PROCESS_INFORMATION info = {};
   BOOL const created =
       CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                      flags, nullptr, nullptr, &startup.StartupInfo, &info);

   if (attribute_ok)
      DeleteProcThreadAttributeList(attributes);

   // Close our copy of the write end so the pipe reports EOF once the child
   // (and any grandchildren) exit.
   write_end.reset();

   if (created == 0) {
      Debug("exec_cache: CreateProcessW failed (error %lu)",
            static_cast<unsigned long>(GetLastError()));
      return StartResult::Failed;
   }
   ScopedHandle const thread_handle(info.hThread);

   ExecChild child;
   child.process = ScopedHandle(info.hProcess);
   child.pipe = std::move(read_end);
   child.started_ms = now;
   child.key = key;
   cache->children.push_back(std::move(child));
   return StartResult::Started;
}

/// Finalize finished/hung children: drain, close handles, publish key results.
void HarvestAll(ExecCache *cache, uint64_t now) {
   if (cache->children.empty())
      return;

   std::vector<ExecChild> keep;
   keep.reserve(cache->children.size());
   for (ExecChild &child : cache->children) {
      if (!child.process) {
         // Defensive: nothing to monitor; drop it (RAII closes when the old
         // vector is destroyed by the swap below).
         continue;
      }

      DrainAvailable(child);

      DWORD const wait = WaitForSingleObject(child.process.get(), 0);
      bool terminal = (wait == WAIT_OBJECT_0);
      if (!terminal && (wait == WAIT_TIMEOUT)) {
         bool const capture_full = child.collected.size() >= MAX_OUTPUT_BYTES;
         bool const hung = (now - child.started_ms) > cache->timeout_ms;
         if (capture_full || hung) {
            // Terminate, then drain once more below. Race note: the child may
            // not have fully exited yet, but we finalize from the bytes already
            // buffered and close the handle; the OS tears the process down
            // asynchronously, which is fine because nothing reads it after.
            TerminateProcess(child.process.get(), 1);
            terminal = true;
         }
      } else if (!terminal) {
         // WAIT_FAILED / WAIT_ABANDONED: treat as terminal so the child does
         // not linger forever.
         terminal = true;
      }

      if (!terminal) {
         keep.push_back(std::move(child));
         continue;
      }

      DrainAvailable(child); // final drain (covers the terminate race)
      if (!child.key.empty()) {
         if (ExecEntry *entry = FindEntry(cache, child.key)) {
            entry->value = std::move(child.collected);
            entry->updated_ms = now;
            entry->has_value = true;
            entry->in_flight = false;
            entry->attempted = false;
         }
      }
      // child.process / child.pipe close when the old vector is destroyed.
   }
   cache->children.swap(keep);
}

} // namespace

auto BuildCommandLine(const std::vector<std::string> &argv) -> std::wstring {
   std::wstring command;
   for (size_t i = 0; i < argv.size(); i++) {
      if (i != 0)
         command += L' ';
      command += L'"';

      std::wstring const wide = Utf8ToWide(argv[i]);
      size_t backslashes = 0;
      for (wchar_t const c : wide) {
         if (c == L'\\') {
            backslashes++;
            continue;
         }
         if (c == L'"') {
            // Double the run of backslashes and escape the quote.
            command.append((backslashes * 2) + 1, L'\\');
            command += L'"';
            backslashes = 0;
            continue;
         }
         command.append(backslashes, L'\\');
         backslashes = 0;
         command += c;
      }
      // Double trailing backslashes so the closing quote is not escaped.
      command.append(backslashes * 2, L'\\');
      command += L'"';
   }
   return command;
}

auto ExecCacheCreate(uint64_t timeout_ms) -> ExecCache * {
   auto *cache = new ExecCache();
   if (timeout_ms > 0)
      cache->timeout_ms = timeout_ms;
   return cache;
}

void ExecCacheDestroy(ExecCache *cache) {
   if (cache == nullptr)
      return;
   for (ExecChild const &child : cache->children) {
      if (child.process)
         TerminateProcess(child.process.get(), 1);
      // Handles close when the vectors are destroyed by `delete cache`.
   }
   delete cache;
}

auto ExecCacheGet(ExecCache *cache, const std::vector<std::string> &argv,
                  const std::string &key, uint64_t ttl_ms)
    -> const std::string * {
   if ((cache == nullptr) || key.empty() || argv.empty() || argv[0].empty())
      return nullptr;

   uint64_t const now = GetTickCount64();
   HarvestAll(cache, now);

   ExecEntry *entry = GetOrCreateEntry(cache, key);
   if (entry == nullptr)
      return nullptr; // entry cap reached: behave as "no data", no crash

   bool const fresh = entry->has_value && ((now - entry->updated_ms) < ttl_ms);
   bool const backoff =
       entry->attempted && ((now - entry->last_attempt_ms) < ttl_ms);
   if (!fresh && !entry->in_flight && !backoff) {
      switch (StartChild(cache, key, argv, now)) {
      case StartResult::Started:
         entry->in_flight = true;
         entry->attempted = false;
         break;
      case StartResult::CapReached:
         // Leave the entry completely untouched: retry on the next call (no
         // fabricated value, no backoff).
         break;
      case StartResult::Failed:
         // Genuine failure: stamp the attempt so the key doesn't hot-loop for
         // one TTL, but do NOT claim a value: exec_cache stays nil until a run
         // succeeds.
         entry->attempted = true;
         entry->last_attempt_ms = now;
         break;
      }
   }
   return entry->has_value ? &entry->value : nullptr;
}

void ExecCacheSpawn(ExecCache *cache, const std::vector<std::string> &argv) {
   if ((cache == nullptr) || argv.empty() || argv[0].empty())
      return;
   uint64_t const now = GetTickCount64();
   HarvestAll(cache, now);
   (void)StartChild(cache, std::string{}, argv, now);
}

auto ExecCacheInFlightCount(const ExecCache *cache) -> size_t {
   return (cache != nullptr) ? cache->children.size() : 0;
}

auto ExecCacheEntryCount(const ExecCache *cache) -> size_t {
   return (cache != nullptr) ? cache->entries.size() : 0;
}

auto ExecCacheStartAttempts(const ExecCache *cache) -> uint64_t {
   return (cache != nullptr) ? cache->start_attempts : 0;
}
