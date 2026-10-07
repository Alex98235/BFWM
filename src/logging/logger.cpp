/**
 * @file logger.cpp
 * @brief Object-oriented logging subsystem implementation.
 */

#include "logger.h"

#include <windows.h>
#include <winnls.h>

#include <cstring>
#include <ctime>
#include <iterator>
#include <optional>
#include <utility>

namespace {

/// Plain level names, indexed by LogLevel (Debug..Fatal).
constexpr std::array<std::string_view, 5> kLevelNames = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

/// ANSI-colorized level names, indexed by LogLevel (Debug..Fatal).
constexpr std::array<std::string_view, 5> kLevelColors = {
    "\x1b[34mDEBUG\x1b[0m", "\x1b[32mINFO\x1b[0m", "\x1b[33mWARN\x1b[0m",
    "\x1b[31mERROR\x1b[0m", "\x1b[41mFATAL\x1b[0m"};

/// Display name for a log level, clamped to a valid index.
auto LevelName(LogLevel level, bool use_color) -> std::string_view {
   const auto index = static_cast<size_t>(level);
   const size_t clamped =
       index < std::size(kLevelNames) ? index : std::size(kLevelNames) - 1;
   return use_color ? kLevelColors[clamped] : kLevelNames[clamped];
}

/// Format a printf-style narrow string using a va_list.
auto VFormatString(const char *format, va_list args) -> std::string {
   va_list args_copy;
   va_copy(args_copy, args);
   const int length = vsnprintf(nullptr, 0, format, args_copy);
   va_end(args_copy);
   if (length < 0) {
      return {};
   }

   std::string result(static_cast<size_t>(length), '\0');
   vsnprintf(result.data(), result.size() + 1, format, args);
   return result;
}

/// Format a printf-style wide string and convert the result to UTF-8.
auto VFormatStringW(const wchar_t *format, va_list args)
    -> std::optional<std::string> {
   va_list args_copy;
   va_copy(args_copy, args);
   const int wide_length = _vsnwprintf(nullptr, 0, format, args_copy);
   va_end(args_copy);
   if (wide_length < 0) {
      return std::nullopt;
   }

   std::wstring wide(static_cast<size_t>(wide_length) + 1, L'\0');
   _vsnwprintf(wide.data(), wide.size(), format, args);

   const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                               nullptr, 0, nullptr, nullptr);
   if (utf8_length <= 0) {
      return std::nullopt;
   }

   std::string utf8(static_cast<size_t>(utf8_length) - 1, '\0');
   WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), utf8_length,
                       nullptr, nullptr);
   return utf8;
}

/// Format the current local time with a strftime-style format string.
auto MakeTimestamp(const std::string &format) -> std::optional<std::string> {
   constexpr size_t kMaxTimestampSize = 100;

   const std::time_t now = std::time(nullptr);
   std::tm local_time{};
   if (localtime_s(&local_time, &now) != 0) {
      return std::nullopt;
   }

   std::string timestamp(kMaxTimestampSize, '\0');
   const size_t written = strftime(timestamp.data(), timestamp.size(),
                                   format.c_str(), &local_time);
   if (written == 0) {
      return std::nullopt;
   }
   timestamp.resize(written);
   return timestamp;
}

/// Remove ANSI escape sequences (used before writing to a log file).
auto StripAnsi(const std::string &str) -> std::string {
   std::string clean;
   clean.reserve(str.size());
   for (size_t i = 0; i < str.size(); ++i) {
      if (str[i] == '\x1b') {
         while (i < str.size() && str[i] != 'm') {
            ++i;
         }
      } else {
         clean.push_back(str[i]);
      }
   }
   return clean;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto Logger::Instance() noexcept -> Logger & {
   static Logger instance;
   return instance;
}

Logger::Logger() = default;

Logger::~Logger() {
   std::lock_guard<std::mutex> const lock(mutex_);
   if (log_file_ != nullptr) {
      std::fflush(log_file_);
      if (own_log_file_) {
         std::fclose(log_file_);
      }
      log_file_ = nullptr;
   }
   std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Logger::SetLevel(LogLevel level) {
   std::lock_guard<std::mutex> const lock(mutex_);
   level_ = level;
}

void Logger::UseTimestamps(bool enable, const char *format) {
   std::lock_guard<std::mutex> const lock(mutex_);
   use_timestamps_ = enable;
   timestamp_format_ = (format != nullptr) ? format : TIMESTAMP_FORMAT;
}

void Logger::UseColor(bool enable) {
   std::lock_guard<std::mutex> const lock(mutex_);
   color_ = enable;
}

void Logger::SetLogToStdout(bool enable) {
   std::lock_guard<std::mutex> const lock(mutex_);
   log_to_stdout_ = enable;
}

void Logger::SetLogToFile(bool enable) {
   std::lock_guard<std::mutex> const lock(mutex_);
   log_to_file_ = enable;
}

void Logger::SetLogFile(std::FILE *file) {
   std::lock_guard<std::mutex> const lock(mutex_);
   if (own_log_file_ && log_file_ != nullptr) {
      std::fclose(log_file_);
   }
   log_file_ = file;
   own_log_file_ = false;
}

auto Logger::OpenLogFile(const char *path, const char *mode) -> bool {
   std::FILE *file = std::fopen(path, mode);
   if (file == nullptr) {
      return false;
   }
   std::lock_guard<std::mutex> const lock(mutex_);
   if (own_log_file_ && log_file_ != nullptr) {
      std::fclose(log_file_);
   }
   log_file_ = file;
   own_log_file_ = true;
   return true;
}

void Logger::SetLogFormatter(LogFormatter formatter) {
   std::lock_guard<std::mutex> const lock(mutex_);
   formatter_ = std::move(formatter);
}

void Logger::Reset() {
   std::lock_guard<std::mutex> const lock(mutex_);
   if (own_log_file_ && log_file_ != nullptr) {
      std::fclose(log_file_);
   }
   level_ = LogLevel::Warn;
   use_timestamps_ = true;
   timestamp_format_ = TIMESTAMP_FORMAT;
   color_ = true;
   log_to_stdout_ = true;
   log_to_file_ = false;
   log_file_ = nullptr;
   own_log_file_ = false;
   formatter_ = nullptr;
}

// ---------------------------------------------------------------------------
// Logging pipeline
// ---------------------------------------------------------------------------

/// Build a line with the default `[timestamp] [LEVEL] message` layout.
auto Logger::BuildDefaultLine(LogLevel level, std::string_view timestamp,
                              std::string_view message, bool use_color)
    -> std::string {
   std::string line;
   line.reserve(timestamp.size() + message.size() + 16);
   line += '[';
   if (!timestamp.empty()) {
      line += timestamp;
      line += "] [";
   }
   line += LevelName(level, use_color);
   line += "] ";
   // Indent continuation lines with a tab so multi-line messages stay aligned.
   for (const char c : message) {
      line += c;
      if (c == '\n') {
         line += '\t';
      }
   }
   return line;
}

/// Route a finished line to the configured destinations.
void Logger::WriteLine(const std::string &line) {
   bool to_file = false;
   bool to_stdout = true;
   std::FILE *file = nullptr;
   {
      std::lock_guard<std::mutex> const lock(mutex_);
      to_file = log_to_file_ && log_file_ != nullptr;
      to_stdout = log_to_stdout_;
      file = log_file_;
   }

   if (to_file) {
      const std::string clean = StripAnsi(line);
      std::fprintf(file, "%s\n", clean.c_str());
      std::fflush(file);
   }
   if (to_stdout) {
      std::fprintf(stdout, "%s\n", line.c_str());
      std::fflush(stdout);
   }
}

/// Build and emit a line for a formatted message.
void Logger::Emit(LogLevel level, std::string timestamp, std::string message,
                  const LogFormatter &formatter, bool use_color) {
   std::string line;
   if (formatter) {
      line = formatter(level, timestamp, message);
   } else {
      line = BuildDefaultLine(level, timestamp, message, use_color);
   }
   WriteLine(line);
}

void Logger::VLog(LogLevel level, const char *format, va_list args) {
   LogLevel configured_level;
   bool use_ts = false;
   bool use_color = true;
   std::string timestamp_format;
   LogFormatter formatter;
   {
      std::lock_guard<std::mutex> const lock(mutex_);
      configured_level = level_;
      use_ts = use_timestamps_;
      use_color = color_;
      timestamp_format = timestamp_format_;
      formatter = formatter_;
   }

   if (level < configured_level) {
      return;
   }

   std::string timestamp;
   if (use_ts) {
      timestamp = MakeTimestamp(timestamp_format).value_or("");
   }

   Emit(level, std::move(timestamp), VFormatString(format, args), formatter,
        use_color);
}

void Logger::VLogW(LogLevel level, const wchar_t *format, va_list args) {
   LogLevel configured_level;
   bool use_ts = false;
   bool use_color = true;
   std::string timestamp_format;
   LogFormatter formatter;
   {
      std::lock_guard<std::mutex> const lock(mutex_);
      configured_level = level_;
      use_ts = use_timestamps_;
      use_color = color_;
      timestamp_format = timestamp_format_;
      formatter = formatter_;
   }

   if (level < configured_level) {
      return;
   }

   std::optional<std::string> message = VFormatStringW(format, args);
   if (!message) {
      return;
   }

   std::string timestamp;
   if (use_ts) {
      timestamp = MakeTimestamp(timestamp_format).value_or("");
   }

   Emit(level, std::move(timestamp), std::move(*message), formatter, use_color);
}

// ---------------------------------------------------------------------------
// Narrow (char) entry points
// ---------------------------------------------------------------------------

void Logger::Log(LogLevel level, const char *format, ...) {
   va_list args;
   va_start(args, format);
   VLog(level, format, args);
   va_end(args);
}

void Logger::Debug(const char *format, ...) {
   va_list args;
   va_start(args, format);
   VLog(LogLevel::Debug, format, args);
   va_end(args);
}

void Logger::Info(const char *format, ...) {
   va_list args;
   va_start(args, format);
   VLog(LogLevel::Info, format, args);
   va_end(args);
}

void Logger::Warn(const char *format, ...) {
   va_list args;
   va_start(args, format);
   VLog(LogLevel::Warn, format, args);
   va_end(args);
}

void Logger::Error(const char *format, ...) {
   va_list args;
   va_start(args, format);
   VLog(LogLevel::Error, format, args);
   va_end(args);
}

void Logger::Fatal(const char *format, ...) {
   va_list args;
   va_start(args, format);
   VLog(LogLevel::Fatal, format, args);
   va_end(args);
   std::exit(EXIT_FAILURE);
}

// ---------------------------------------------------------------------------
// Wide (wchar_t) entry points
// ---------------------------------------------------------------------------

void Logger::LogW(LogLevel level, const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   VLogW(level, format, args);
   va_end(args);
}

void Logger::DebugW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   VLogW(LogLevel::Debug, format, args);
   va_end(args);
}

void Logger::InfoW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   VLogW(LogLevel::Info, format, args);
   va_end(args);
}

void Logger::WarnW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   VLogW(LogLevel::Warn, format, args);
   va_end(args);
}

void Logger::ErrorW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   VLogW(LogLevel::Error, format, args);
   va_end(args);
}

void Logger::FatalW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   VLogW(LogLevel::Fatal, format, args);
   va_end(args);
   std::exit(EXIT_FAILURE);
}
