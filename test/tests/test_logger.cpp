#include "../../src/logging/logger.h"
#include "../cbelt/cbelt.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

/// Test logger that captures output for verification
class TestLogger : public ILogger {
 public:
   TestLogger() = default;
   ~TestLogger() override = default;

   void VLog(LogLevel level, const char *format, va_list args) override {
      if (level < level_) {
         return;
      }
      char buf[1024];
      vsnprintf(buf, sizeof(buf), format, args);
      last_message_ = buf;
      last_level_ = level;
      call_count_++;
   }

   void VLogW(LogLevel level, const wchar_t *format, va_list args) override {
      if (level < level_) {
         return;
      }
      wchar_t buf[1024];
      _vsnwprintf(buf, sizeof(buf) / sizeof(wchar_t), format, args);
      last_message_w_ = buf;
      last_level_ = level;
      call_count_++;
   }

   void SetLevel(LogLevel level) override { level_ = level; }
   void UseTimestamps(bool, const char *) override {}
   void UseColor(bool) override {}
   void SetLogToStdout(bool) override {}
   void SetLogToFile(bool) override {}
   void SetLogFile(std::FILE *) override {}
   auto OpenLogFile(const char *, const char *) -> bool override {
      return false;
   }
   void SetLogFormatter(std::function<std::string(LogLevel, std::string_view,
                                                  std::string_view)>) override {
   }
   void Reset() override {}

   // Test inspection
   auto GetLastMessage() const -> std::string { return last_message_; }
   auto GetLastMessageW() const -> std::wstring { return last_message_w_; }
   auto GetLastLevel() const -> LogLevel { return last_level_; }
   auto GetCallCount() const -> int { return call_count_; }
   auto GetLevel() const -> LogLevel { return level_; }

   void ResetCounters() {
      last_message_.clear();
      last_message_w_.clear();
      last_level_ = LogLevel::None;
      call_count_ = 0;
   }

 private:
   std::string last_message_;
   std::wstring last_message_w_;
   LogLevel last_level_ = LogLevel::None;
   int call_count_ = 0;
   LogLevel level_ = LogLevel::Warn;
};

/// Helper to check if a string contains a substring
auto StrContains(const std::string &haystack, const std::string &needle)
    -> bool {
   return haystack.find(needle) != std::string::npos;
}

} // namespace

CBELT_GROUP("logger")

CBELT_SETUP() {
   // Reset global logger to nullptr before each test
   SetGlobalLogger(nullptr);
}

CBELT_TEARDOWN() {
   // Reset global logger after each test to avoid dangling pointer
   SetGlobalLogger(nullptr);
}

CBELT_TEST(basic_logging) {
   TestLogger logger;
   logger.SetLevel(LogLevel::Debug); // Capture all levels
   SetGlobalLogger(&logger);

   Debug("debug message");
   Info("info message");
   Warn("warn message");
   Error("error message");

   cbelt_assert_equal(logger.GetCallCount(), 4);
   cbelt_assert_equal(static_cast<int>(logger.GetLastLevel()),
                      static_cast<int>(LogLevel::Error));
   cbelt_assert(StrContains(logger.GetLastMessage(), "error message"));

   return TEST_SUCCESS;
}

CBELT_TEST(level_filtering) {
   TestLogger logger;
   logger.SetLevel(LogLevel::Warn);
   SetGlobalLogger(&logger);

   Debug("should not appear");
   Info("should not appear");
   Warn("should appear");
   Error("should appear");

   cbelt_assert_equal(logger.GetCallCount(), 2); // only Warn and Error
   cbelt_assert_equal(static_cast<int>(logger.GetLastLevel()),
                      static_cast<int>(LogLevel::Error));

   return TEST_SUCCESS;
}

CBELT_TEST(wide_logging) {
   TestLogger logger;
   logger.SetLevel(LogLevel::Debug); // Capture all levels
   SetGlobalLogger(&logger);

   DebugW(L"wide debug %d", 42);
   InfoW(L"wide info %ls", L"test");

   cbelt_assert_equal(logger.GetCallCount(), 2);
   cbelt_assert(StrContains(std::string(logger.GetLastMessageW().begin(),
                                        logger.GetLastMessageW().end()),
                            "wide info"));

   return TEST_SUCCESS;
}

CBELT_TEST(global_logger_null_safe) {
   SetGlobalLogger(nullptr);
   // Should not crash
   Debug("no logger set");
   Info("no logger set");
   Warn("no logger set");
   Error("no logger set");
   // If we reach here, test passes
   cbelt_assert_true(true);

   return TEST_SUCCESS;
}

CBELT_TEST(di_pattern) {
   TestLogger logger1;
   logger1.SetLevel(LogLevel::Debug);
   TestLogger logger2;
   logger2.SetLevel(LogLevel::Debug);

   // Component using DI
   struct Component {
      // Test fixture.
      // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
      ILogger *log_;
      explicit Component(ILogger *log = nullptr)
          : log_(log ? log : GetGlobalLogger()) {}
      void DoWork() const { log_->VLog(LogLevel::Info, "working", nullptr); }
   };

   // Use logger1 via DI
   Component c1(&logger1);
   c1.DoWork();
   cbelt_assert_equal(logger1.GetCallCount(), 1);
   cbelt_assert_equal(logger2.GetCallCount(), 0);

   // Use global logger (logger2 set as global)
   SetGlobalLogger(&logger2);
   Component c2;
   c2.DoWork();
   cbelt_assert_equal(logger1.GetCallCount(), 1);
   cbelt_assert_equal(logger2.GetCallCount(), 1);

   return TEST_SUCCESS;
}
