#include "config_paths.h"
#include "../logging/logger.h"
#include "../memory/safe.h"
#include <array>
#include <errhandlingapi.h>
#include <fileapi.h>
#include <minwindef.h>
#include <processenv.h>
#include <string>
#include <winnt.h>

enum {
   CONFIG_PATH_BUF_SIZE = 1024,
};

auto ResolveConfigPath(const char *filename, char *out_path, size_t out_size)
    -> bool {
   const std::array<std::string, 2> prefixes = {
       "%APPDATA%\\BFWM\\",
       R"(%USERPROFILE%\.config\BFWM\)",
   };

   for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
      std::string dir;
      dir.resize(CONFIG_PATH_BUF_SIZE);
      DWORD const ret = ExpandEnvironmentStringsA(prefixes[i].c_str(),
                                                  dir.data(), dir.size());
      if (ret == 0 || ret > dir.size()) {
         Warn("ExpandEnvironmentStringsA failed for '%s': %lu",
              prefixes[i].c_str(), GetLastError());
         continue;
      }
      dir.resize(ret > 0 ? ret - 1 : 0);

      std::string full_path;
      full_path.resize(CONFIG_PATH_BUF_SIZE);
      int const written = snprintf(full_path.data(), full_path.size(), "%s%s",
                                   dir.c_str(), filename);
      if (written < 0 || (size_t)written >= full_path.size())
         continue;
      full_path.resize((size_t)written);

      DWORD const attrs = GetFileAttributesA(full_path.c_str());
      if (attrs != INVALID_FILE_ATTRIBUTES &&
          (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
         safe_strcpy(out_path, out_size, full_path.c_str());
         Info("Resolved config path: %s", out_path);
         return true;
      }
   }

   return false;
}
