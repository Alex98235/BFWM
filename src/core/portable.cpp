#include "portable.h"
#include <cstdlib>
#include <cstring>

auto strdup_portable(const char *s) -> char * {
   size_t const len = strlen(s) + 1;
   char *copy = (char *)malloc(len);
   if (copy != nullptr) {
      memcpy(copy, s, len);
   }
   return copy;
}

auto strcmpi_portable(const char *a, const char *b) -> int {
   return _stricmp(a, b);
}

auto ltokenize(char *str, const char *delim, char **saveptr) -> char * {
   if (str == nullptr)
      str = *saveptr;
   if (str == nullptr)
      return nullptr;

   str += strspn(str, delim);
   if ((*str) == 0) {
      *saveptr = nullptr;
      return nullptr;
   }

   char *token = str;
   str = strpbrk(token, delim);
   if (str != nullptr) {
      *str = '\0';
      *saveptr = str + 1;
   } else {
      *saveptr = nullptr;
   }
   return token;
}
