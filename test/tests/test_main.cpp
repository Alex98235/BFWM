#define CBELT_IMPLEMENTATION
#include "../../src/logging/logger.h"
#include "../cbelt/cbelt.h"

auto main() -> int {
   // Suppress log output for tests.
   // The test code implicitely inits the logger with default values
   // (LOG_WARN) otherwise.
   Logger::Instance().SetLevel(LogLevel::None);
   cbelt_run_all_tests();
}
