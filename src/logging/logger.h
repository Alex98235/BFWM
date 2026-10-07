/**
 * @file logger.h
 * @brief Object-oriented logging subsystem.
 *
 * A process-wide Logger singleton: lazily constructed on first use and
 * destroyed at program exit (any owned log file is flushed and closed by the
 * destructor). Logging entry points are exposed both as member functions
 * (`Logger::Instance().Error(...)`) and as namespace-level free functions
 * (`Error(...)`) so call sites never need to name the singleton.
 */

#ifndef BFWM_LOGGER_H
#define BFWM_LOGGER_H

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

/// Default strftime-style timestamp format.
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

/**
 * @brief Log levels, in increasing severity.
 *
 * `None` disables all logging.
 */
enum class LogLevel { Debug, Info, Warn, Error, Fatal, None };

/**
 * @brief Process-wide logger singleton.
 *
 * Configure it through `Logger::Instance()`, or log through the free
 * `Debug()` / `Info()` / ... functions declared below.
 */
class Logger {
 public:
   /// Custom log-line builder. Receives the level, the timestamp (empty when
   /// disabled or unavailable) and the formatted message, and must return the
   /// complete line to emit.
   using LogFormatter = std::function<std::string(
       LogLevel level, std::string_view timestamp, std::string_view message)>;

   /// Access the process-wide logger.
   static auto Instance() noexcept -> Logger &;

   /// The logger is a singleton; copying is disabled. Kept public so that
   /// misuse produces a clear "use of deleted function" diagnostic instead
   /// of a confusing access-control error.
   Logger(const Logger &) = delete;
   auto operator=(const Logger &) -> Logger & = delete;

   // ---- Configuration ----------------------------------------------------

   /// Set the minimum level that is actually emitted.
   void SetLevel(LogLevel level);

   /// Enable/disable timestamps. `format` is a strftime-style format string
   /// (nullptr or omitted = default).
   void UseTimestamps(bool enable, const char *format = TIMESTAMP_FORMAT);

   /// Enable/disable ANSI color output (terminal only).
   void UseColor(bool enable);

   /// Enable/disable logging to stdout.
   void SetLogToStdout(bool enable);

   /// Enable/disable logging to the configured file.
   void SetLogToFile(bool enable);

   /// Attach an externally-owned output stream. The logger does not close it.
   void SetLogFile(std::FILE *file);

   /// Open `path` and take ownership. The logger closes it on replacement,
   /// on Reset() and on destruction. Returns false if the file cannot be
   /// opened.
   auto OpenLogFile(const char *path, const char *mode = "a") -> bool;

   /// Replace the default line formatter (pass {} or nullptr to restore it).
   void SetLogFormatter(LogFormatter formatter);

   /// Restore default settings and close any owned log file.
   void Reset();

   // ---- Narrow (char) logging -------------------------------------------

   void Log(LogLevel level, const char *format, ...);

   /// va_list variant of Log().
   void VLog(LogLevel level, const char *format, va_list args);

   void Debug(const char *format, ...);
   void Info(const char *format, ...);
   void Warn(const char *format, ...);
   void Error(const char *format, ...);

   /// Log at Fatal level, then terminate the process.
   [[noreturn]] void Fatal(const char *format, ...);

   // ---- Wide (wchar_t) logging ------------------------------------------

   void LogW(LogLevel level, const wchar_t *format, ...);

   /// va_list variant of LogW().
   void VLogW(LogLevel level, const wchar_t *format, va_list args);

   void DebugW(const wchar_t *format, ...);
   void InfoW(const wchar_t *format, ...);
   void WarnW(const wchar_t *format, ...);
   void ErrorW(const wchar_t *format, ...);

   /// Log at Fatal level, then terminate the process.
   [[noreturn]] void FatalW(const wchar_t *format, ...);

 private:
   Logger();
   ~Logger();

   void Emit(LogLevel level, std::string timestamp, std::string message,
             const LogFormatter &formatter, bool use_color);
   static auto BuildDefaultLine(LogLevel level, std::string_view timestamp,
                                std::string_view message, bool use_color)
       -> std::string;
   void WriteLine(const std::string &line);

   mutable std::mutex mutex_;
   LogLevel level_ = LogLevel::Warn;
   bool use_timestamps_ = true;
   std::string timestamp_format_ = TIMESTAMP_FORMAT;
   bool color_ = true;
   bool log_to_stdout_ = true;
   bool log_to_file_ = false;
   std::FILE *log_file_ = nullptr;
   bool own_log_file_ = false;
   LogFormatter formatter_;
};

// ===========================================================================
// Namespace-level convenience wrappers
// ===========================================================================

inline void Log(LogLevel level, const char *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLog(level, format, args);
   va_end(args);
}

inline void Debug(const char *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLog(LogLevel::Debug, format, args);
   va_end(args);
}

inline void Info(const char *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLog(LogLevel::Info, format, args);
   va_end(args);
}

inline void Warn(const char *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLog(LogLevel::Warn, format, args);
   va_end(args);
}

inline void Error(const char *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLog(LogLevel::Error, format, args);
   va_end(args);
}

[[noreturn]] inline void Fatal(const char *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLog(LogLevel::Fatal, format, args);
   va_end(args);
   std::exit(EXIT_FAILURE);
}

inline void LogW(LogLevel level, const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLogW(level, format, args);
   va_end(args);
}

inline void DebugW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLogW(LogLevel::Debug, format, args);
   va_end(args);
}

inline void InfoW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLogW(LogLevel::Info, format, args);
   va_end(args);
}

inline void WarnW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLogW(LogLevel::Warn, format, args);
   va_end(args);
}

inline void ErrorW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLogW(LogLevel::Error, format, args);
   va_end(args);
}

[[noreturn]] inline void FatalW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   Logger::Instance().VLogW(LogLevel::Fatal, format, args);
   va_end(args);
   std::exit(EXIT_FAILURE);
}

#endif
