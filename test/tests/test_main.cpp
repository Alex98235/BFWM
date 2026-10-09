#define CBELT_IMPLEMENTATION
#include "../../src/logging/logger.h"
#include "../cbelt/cbelt.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>

namespace {

// Child mode for the exec_cache argv round-trip test. Dumps argv[2..] to
// stdout as a length-prefixed stream so the bytes survive command-line
// quoting verbatim:
//   <count>\n
//   <byteLen>\n<raw bytes>\n   (repeated once per argument)
void DumpArgv(int argc, char **argv) {
   // Raw bytes must survive verbatim; disable CRT text-mode LF->CRLF and
   // Ctrl-Z handling on stdout (which would also corrupt payloads containing
   // '\n').
   (void)_setmode(_fileno(stdout), _O_BINARY);
   size_t const count = (argc > 2) ? static_cast<size_t>(argc - 2) : 0;
   std::printf("%zu\n", count);
   for (int i = 2; i < argc; i++) {
      size_t const len = std::strlen(argv[i]);
      std::printf("%zu\n", len);
      if (len > 0)
         std::fwrite(argv[i], 1, len, stdout);
      std::fputc('\n', stdout);
   }
   std::fflush(stdout);
}

} // namespace

auto main(int argc, char **argv) -> int {
   if ((argc >= 2) && (std::strcmp(argv[1], "--argv-echo") == 0)) {
      DumpArgv(argc, argv);
      return 0;
   }

   // Suppress log output for tests.
   // The test code implicitely inits the logger with default values
   // (LOG_WARN) otherwise.
   Logger::Instance().SetLevel(LogLevel::None);
   cbelt_run_all_tests();
   return 0;
}
