#include "cbelt.h"
#include <cstdlib>
#include <string>
#include <vector>
#include <windows.h>

#include "../../src/system/exec_cache.h"

CBELT_GROUP("exec_cache")

namespace {

auto WideToUtf8(const wchar_t *wide) -> std::string {
   if (wide == nullptr)
      return {};
   std::wstring const text(wide);
   if (text.empty())
      return {};
   int const len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
   if (len <= 0)
      return {};
   std::string out(static_cast<size_t>(len), '\0');
   WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                       out.data(), len, nullptr, nullptr);
   return out;
}

/// Parse the child's length-prefixed argv dump (see DumpArgv in test_main.cpp).
auto ParseEcho(const std::string &out, std::vector<std::string> *args) -> bool {
   size_t pos = 0;
   auto read_line = [&](std::string *line) -> bool {
      size_t const nl = out.find('\n', pos);
      if (nl == std::string::npos)
         return false;
      *line = out.substr(pos, nl - pos);
      pos = nl + 1;
      return true;
   };

   std::string line;
   if (!read_line(&line))
      return false;
   long long const count = std::atoll(line.c_str());
   if ((count < 0) || (count > 1024))
      return false;

   for (long long i = 0; i < count; i++) {
      if (!read_line(&line))
         return false;
      long long const len = std::atoll(line.c_str());
      if (len < 0)
         return false;
      if (pos + static_cast<size_t>(len) > out.size())
         return false;
      args->push_back(out.substr(pos, static_cast<size_t>(len)));
      pos += static_cast<size_t>(len);
      if ((pos >= out.size()) || (out[pos] != '\n'))
         return false;
      pos += 1;
   }
   return true;
}

/// Poll a key until it yields a value or the budget runs out.
auto PollValue(ExecCache *cache, const std::vector<std::string> &argv,
               const std::string &key, int iterations, const std::string **out)
    -> bool {
   for (int i = 0; i < iterations; i++) {
      const std::string *value = ExecCacheGet(cache, argv, key, 60000);
      if (value != nullptr) {
         if (out != nullptr)
            *out = value;
         return true;
      }
      Sleep(10);
   }
   return false;
}

} // namespace

/* =========================================================================
 * BuildCommandLine quoting (MSVC/CRT rules)
 * ========================================================================= */

CBELT_TEST(build_command_line_empty_vector) {
   cbelt_assert_true(BuildCommandLine({}).empty());
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_plain_args) {
   cbelt_assert(BuildCommandLine({"a", "b"}) == L"\"a\" \"b\"");
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_spaces) {
   cbelt_assert(BuildCommandLine({"hello world"}) == L"\"hello world\"");
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_empty_arg) {
   cbelt_assert(BuildCommandLine({""}) == L"\"\"");
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_embedded_quote) {
   /* say"hi -> "say\"hi" */
   cbelt_assert(BuildCommandLine({"say\"hi"}) == L"\"say\\\"hi\"");
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_trailing_backslashes) {
   /* C:\ -> "C:\\" */
   cbelt_assert(BuildCommandLine({"C:\\"}) == L"\"C:\\\\\"");
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_backslash_before_quote) {
   /* a\b"c -> "a\b\"c" */
   cbelt_assert(BuildCommandLine({"a\\b\"c"}) == L"\"a\\b\\\"c\"");
   return TEST_SUCCESS;
}

CBELT_TEST(build_command_line_round_trips_via_child_crt) {
   // The child is this very test executable; its C runtime parses the command
   // line we build with MSVC/CRT rules and dumps argv[2..] back to us. This is
   // the correct oracle (CommandLineToArgvW does NOT implement those rules).
   wchar_t module_path[MAX_PATH] = {};
   DWORD const path_len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
   cbelt_assert((path_len > 0) && (path_len < MAX_PATH));
   std::string const exe = WideToUtf8(module_path);
   cbelt_assert_false(exe.empty());

   std::vector<std::vector<std::string>> const cases = {
       {"a", "b"},
       {"hello world", "x"},
       {"say\"hi"},
       {"c:\\path with space\\"},
       {"trail\\"},
       {""},
       {"", "empty-before"},
       {"  leading and trailing  "},
       {"back\\slash\\\"quote\"end", "\\", "\\\\"},
   };

   int case_index = 0;
   for (const auto &args : cases) {
      ExecCache *cache = ExecCacheCreate(60000);
      std::vector<std::string> argv;
      argv.reserve(args.size() + 2);
      argv.push_back(exe);
      argv.push_back("--argv-echo");
      for (const auto &arg : args)
         argv.push_back(arg);

      std::string const key = "rt" + std::to_string(case_index++);
      const std::string *value = nullptr;
      cbelt_assert_true(PollValue(cache, argv, key, 500, &value));

      std::vector<std::string> recovered;
      cbelt_assert_true(ParseEcho(*value, &recovered));
      cbelt_assert_true(args.size() == recovered.size());
      for (size_t i = 0; i < recovered.size() && i < args.size(); i++)
         cbelt_assert_true(args[i] == recovered[i]);

      ExecCacheDestroy(cache);
   }
   return TEST_SUCCESS;
}

/* =========================================================================
 * Process smoke tests (bounded)
 * ========================================================================= */

CBELT_TEST(spawn_handles_space_and_empty_arg) {
   ExecCache *cache = ExecCacheCreate();
   std::vector<std::string> const argv = {"cmd.exe", "/c", "echo", "a b", ""};
   const std::string *value = nullptr;
   cbelt_assert_true(PollValue(cache, argv, "space", 300, &value));
   cbelt_assert_true(value->find("a b") != std::string::npos);
   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

CBELT_TEST(timeout_terminates_hung_child) {
   ExecCache *cache = ExecCacheCreate(200);
   std::vector<std::string> const argv = {"cmd.exe", "/c", "ping",
                                          "-n",      "10", "127.0.0.1"};
   const std::string *value = nullptr;
   cbelt_assert_true(PollValue(cache, argv, "to", 300, &value));
   cbelt_assert_true(0 == ExecCacheInFlightCount(cache));
   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

CBELT_TEST(stdout_cap_truncates) {
   ExecCache *cache = ExecCacheCreate(2000);
   /* Outputs ~4000 * 41 bytes (~160 KB) > the 64 KB cap. */
   std::vector<std::string> const argv = {
       "cmd.exe", "/c",
       "for /l %i in (1,1,4000) do @echo "
       "0123456789012345678901234567890123456789"};
   const std::string *value = nullptr;
   cbelt_assert_true(PollValue(cache, argv, "cap", 500, &value));
   cbelt_assert_true(value->size() <= 65536);
   cbelt_assert_true(0 == ExecCacheInFlightCount(cache));
   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

/* =========================================================================
 * State-machine edge cases
 * ========================================================================= */

CBELT_TEST(failed_start_returns_nil_and_backs_off) {
   ExecCache *cache = ExecCacheCreate(); /* default TTL used as backoff */
   std::vector<std::string> const argv = {"bfwm_no_such_executable_xyz.exe"};

   cbelt_assert_null(ExecCacheGet(cache, argv, "bad", 1000));
   uint64_t const attempts = ExecCacheStartAttempts(cache);
   cbelt_assert_true(1 == attempts);

   /* Immediate retry backs off: no second attempt, still nil. */
   cbelt_assert_null(ExecCacheGet(cache, argv, "bad", 1000));
   cbelt_assert_true(attempts == ExecCacheStartAttempts(cache));
   cbelt_assert_true(0 == ExecCacheInFlightCount(cache));

   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

CBELT_TEST(coalesces_same_key) {
   ExecCache *cache = ExecCacheCreate(60000);
   std::vector<std::string> const argv = {"cmd.exe", "/c", "ping",
                                          "-n",      "10", "127.0.0.1"};

   cbelt_assert_null(ExecCacheGet(cache, argv, "co", 0));
   cbelt_assert_true(1 == ExecCacheInFlightCount(cache));
   uint64_t const attempts = ExecCacheStartAttempts(cache);

   /* Second call while in flight must not start another child. */
   cbelt_assert_null(ExecCacheGet(cache, argv, "co", 0));
   cbelt_assert_true(1 == ExecCacheInFlightCount(cache));
   cbelt_assert_true(attempts == ExecCacheStartAttempts(cache));

   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

CBELT_TEST(cap_rejection_returns_nil_and_retries) {
   ExecCache *cache = ExecCacheCreate(60000);
   std::vector<std::string> const argv = {"cmd.exe", "/c", "ping",
                                          "-n",      "10", "127.0.0.1"};

   /* Fill the concurrency cap with distinct keys. */
   for (int i = 0; i < 16; i++)
      (void)ExecCacheGet(cache, argv, "cap" + std::to_string(i), 0);
   cbelt_assert_true(16 == ExecCacheInFlightCount(cache));
   uint64_t const attempts = ExecCacheStartAttempts(cache);

   /* A 17th key is cap-rejected: nil (no fabricated value) and it *retries*
    * on every call (unlike a genuine failure, which backs off). */
   cbelt_assert_null(ExecCacheGet(cache, argv, "cap17", 1000));
   cbelt_assert_null(ExecCacheGet(cache, argv, "cap17", 1000));
   cbelt_assert_true(attempts + 2 == ExecCacheStartAttempts(cache));

   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

CBELT_TEST(entry_cap_returns_nil_without_crash) {
   ExecCache *cache = ExecCacheCreate();
   std::vector<std::string> const argv = {"bfwm_no_such_executable_xyz.exe"};

   for (int i = 0; i < 64; i++)
      (void)ExecCacheGet(cache, argv, "k" + std::to_string(i), 1000);
   cbelt_assert_true(64 == ExecCacheEntryCount(cache));

   /* The 65th distinct key cannot be retained: nil, no crash, no growth. */
   cbelt_assert_null(ExecCacheGet(cache, argv, "k_overflow", 1000));
   cbelt_assert_true(64 == ExecCacheEntryCount(cache));

   ExecCacheDestroy(cache);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Freshness is measured from the data's production time, not harvest time
 * ========================================================================= */

CBELT_TEST(freshness_age_is_measured_from_production_time) {
   /* Harvest happens in the same poll as the freshness test, so a value
    * harvested now but produced one interval ago must be judged stale: with a
    * harvest-time stamp the age would read 0 and always look fresh, skipping
    * the next replacement spawn. */
   uint64_t const interval = 1000;
   cbelt_assert_false(ExecCacheIsFresh(true, /*now_ms=*/interval,
                                       /*updated_ms=*/0,
                                       /*ttl_ms=*/interval));
   cbelt_assert_false(ExecCacheIsFresh(true, interval, 0, interval - 1));
   cbelt_assert_false(ExecCacheIsFresh(true, interval, 0, 1));

   /* A ttl strictly greater than the age is fresh. */
   cbelt_assert_true(ExecCacheIsFresh(true, interval, 0, interval + 1));

   return TEST_SUCCESS;
}

CBELT_TEST(freshness_ttl_zero_is_never_fresh) {
   /* 0 < 0 is false: ttl = 0 always requests a refresh. */
   cbelt_assert_false(ExecCacheIsFresh(true, 0, 0, 0));
   cbelt_assert_false(ExecCacheIsFresh(true, 500, 500, 0));
   cbelt_assert_false(ExecCacheIsFresh(true, 500, 0, 0));
   return TEST_SUCCESS;
}

CBELT_TEST(freshness_requires_a_value) {
   /* No value recorded means nothing can be fresh, regardless of ttl. */
   cbelt_assert_false(ExecCacheIsFresh(false, 1000, 0, 1000));
   cbelt_assert_false(ExecCacheIsFresh(false, 1000, 0, 60000));
   return TEST_SUCCESS;
}