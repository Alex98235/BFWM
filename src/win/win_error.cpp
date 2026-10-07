/**
 * @file win_error.c
 * @brief Win32 error logging implementation.
 */

#include "win_error.h"
#include "../logging/logger.h"
#include <array>
#include <cstdio>
#include <errhandlingapi.h>
#include <minwindef.h>
#include <windows.h>
#include <winnt.h>

enum { ERR_BUF_SIZE = 1024 };

void BFWMLogLastError(const char *prefix) {
   DWORD const error = GetLastError();
   LPSTR messageBuffer = nullptr;

   FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                  nullptr, error, 0, (LPSTR)&messageBuffer, 0, nullptr);

   std::string buf;
   if (prefix != nullptr)
      buf = std::string(prefix) + ": ";

   if (messageBuffer != nullptr) {
      std::array<char, ERR_BUF_SIZE> tmp = {};
      sprintf_s(tmp.data(), ERR_BUF_SIZE, "error %lu: %s", error,
                messageBuffer);
      LocalFree(messageBuffer);
      buf += tmp.data();
   } else {
      std::array<char, ERR_BUF_SIZE> tmp = {};
      sprintf_s(tmp.data(), ERR_BUF_SIZE, "error %lu\n", error);
      buf += tmp.data();
   }

   Error("%s", buf.data());
   fprintf_s(stderr, "%s", buf.data());
}
