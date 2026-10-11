/**
 * @file logger.h
 * @brief Object-oriented logging subsystem.
 *
 * A process-wide Logger instance: constructed in main() and destroyed at
 * program exit (any owned log file is flushed and closed by the destructor).
 * Logging entry points are exposed both as member functions
 * (`Logger::Error(...)`) and as namespace-level free functions
 * (`Error(...)`) so call sites never need to name the instance.
 *
 * For testing and DI, an `ILogger` abstract interface is provided. New code
 * can opt into DI by accepting an `ILogger*` (defaulting to the global
 * instance via `GetGlobalLogger()`).
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
 * @brief Abstract logger interface for DI and testing.
 */
class ILogger {
 public:
   virtual ~ILogger() = default;
   virtual void VLog(LogLevel level, const char *format, va_list args) = 0;
   virtual void VLogW(LogLevel level, const wchar_t *format, va_list args) = 0;
   virtual void SetLevel(LogLevel level) = 0;
   virtual void UseTimestamps(bool enable,
                              const char *format = TIMESTAMP_FORMAT) = 0;
   virtual void UseColor(bool enable) = 0;
   virtual void SetLogToStdout(bool enable) = 0;
   virtual void SetLogToFile(bool enable) = 0;
   virtual void SetLogFile(std::FILE *file) = 0;
   virtual auto OpenLogFile(const char *path, const char *mode = "a")
       -> bool = 0;
   virtual void SetLogFormatter(
       std::function<std::string(LogLevel, std::string_view, std::string_view)>
           formatter) = 0;
   virtual void Reset() = 0;
};

/**
 * @brief Concrete logger implementation.
 *
 * Not a singleton — construct directly. Use `SetGlobalLogger()` to publish
 * it for the free-function API (`Debug()`, `Info()`, etc.).
 */
class Logger : public ILogger {
 public:
   /// Custom log-line builder. Receives the level, the timestamp (empty when
   /// disabled or unavailable) and the formatted message, and must return the
   /// complete line to emit.
   using LogFormatter = std::function<std::string(
       LogLevel level, std::string_view timestamp, std::string_view message)>;

   Logger();
   ~Logger() override;

   Logger(const Logger &) = delete;
   auto operator=(const Logger &) -> Logger & = delete;

   // ---- Configuration ----------------------------------------------------

   /// Set the minimum level that is actually emitted.
   void SetLevel(LogLevel level) override;

   /// Enable/disable timestamps. `format` is a strftime-style format string
   /// (nullptr or omitted = default).
   void UseTimestamps(bool enable,
                      const char *format = TIMESTAMP_FORMAT) override;

   /// Enable/disable ANSI color output (terminal only).
   void UseColor(bool enable) override;

   /// Enable/disable logging to stdout.
   void SetLogToStdout(bool enable) override;

   /// Enable/disable logging to the configured file.
   void SetLogToFile(bool enable) override;

   /// Attach an externally-owned output stream. The logger does not close it.
   void SetLogFile(std::FILE *file) override;

   /// Open `path` and take ownership. The logger closes it on replacement,
   /// on Reset() and on destruction. Returns false if the file cannot be
   /// opened.
   auto OpenLogFile(const char *path, const char *mode = "a") -> bool override;

   /// Replace the default line formatter (pass {} or nullptr to restore it).
   void SetLogFormatter(LogFormatter formatter) override;

   /// Restore default settings and close any owned log file.
   void Reset() override;

   // ---- Narrow (char) logging -------------------------------------------

   /// Variadic logging entry point (calls VLog).
   void Log(LogLevel level, const char *format, ...);

   /// va_list variant of Log() - implements ILogger.
   void VLog(LogLevel level, const char *format, va_list args) override;

   void Debug(const char *format, ...);
   void Info(const char *format, ...);
   void Warn(const char *format, ...);
   void Error(const char *format, ...);

   /// Log at Fatal level, then terminate the process.
   [[noreturn]] void Fatal(const char *format, ...);

   // ---- Wide (wchar_t) logging ------------------------------------------

   /// Variadic logging entry point (calls VLogW).
   void LogW(LogLevel level, const wchar_t *format, ...);

   /// va_list variant of LogW() - implements ILogger.
   void VLogW(LogLevel level, const wchar_t *format, va_list args) override;

   void DebugW(const wchar_t *format, ...);
   void InfoW(const wchar_t *format, ...);
   void WarnW(const wchar_t *format, ...);
   void ErrorW(const wchar_t *format, ...);

   /// Log at Fatal level, then terminate the process.
   [[noreturn]] void FatalW(const wchar_t *format, ...);

 private:
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
// Global logger access (for free-function API and opt-in DI)
// ===========================================================================

/// Set the global logger used by free functions (Debug, Info, Warn, ...).
/// Call once at startup with the address of your Logger instance.
void SetGlobalLogger(ILogger *logger);

/// Get the current global logger (never null after SetGlobalLogger called).
auto GetGlobalLogger() -> ILogger *;

// ===========================================================================
// Namespace-level convenience wrappers (use global logger)
// ===========================================================================

inline void Log(LogLevel level, const char *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLog(level, format, args);
   va_end(args);
}

inline void Debug(const char *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLog(LogLevel::Debug, format, args);
   va_end(args);
}

inline void Info(const char *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLog(LogLevel::Info, format, args);
   va_end(args);
}

inline void Warn(const char *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLog(LogLevel::Warn, format, args);
   va_end(args);
}

inline void Error(const char *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLog(LogLevel::Error, format, args);
   va_end(args);
}

[[noreturn]] inline void Fatal(const char *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLog(LogLevel::Fatal, format, args);
   va_end(args);
   std::exit(EXIT_FAILURE);
}

inline void LogW(LogLevel level, const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLogW(level, format, args);
   va_end(args);
}

inline void DebugW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLogW(LogLevel::Debug, format, args);
   va_end(args);
}

inline void InfoW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLogW(LogLevel::Info, format, args);
   va_end(args);
}

inline void WarnW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLogW(LogLevel::Warn, format, args);
   va_end(args);
}

inline void ErrorW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLogW(LogLevel::Error, format, args);
   va_end(args);
}

[[noreturn]] inline void FatalW(const wchar_t *format, ...) {
   va_list args;
   va_start(args, format);
   if (auto *log = GetGlobalLogger())
      log->VLogW(LogLevel::Fatal, format, args);
   va_end(args);
   std::exit(EXIT_FAILURE);
}

#endif
