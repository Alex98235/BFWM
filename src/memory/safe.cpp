#include "safe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

auto safe_malloc(size_t size, const char *context) -> void * {
   void *ptr = malloc(size);
   if (ptr == nullptr) {
      if (size == 0) {
         fprintf(stderr, "%s: size is 0", context);
      } else {
         perror(context);
      }
      exit(EXIT_FAILURE);
   }
   return ptr;
}

auto safe_calloc(size_t nmemb, size_t size, const char *context) -> void * {
   void *ptr = calloc(nmemb, size);
   if (ptr == nullptr) {
      if (size == 0) {
         fprintf(stderr, "%s: size is 0", context);
      } else {
         perror(context);
      }
      exit(EXIT_FAILURE);
   }
   return ptr;
}

auto safe_realloc(void *ptr, size_t new_size, const char *context) -> void * {
   void *new_ptr = realloc(ptr, new_size);
   if (new_ptr == nullptr) {
      if (new_size == 0) {
         fprintf(stderr, "%s: new_size is 0", context);
      } else {
         perror(context);
      }
      exit(EXIT_FAILURE);
   }
   return new_ptr;
}

void safe_strcpy(char *dst, size_t dst_size, const char *src) {
   if ((dst == nullptr) || dst_size == 0)
      return;
   size_t index = 0;
   while (index < dst_size - 1 && (src[index] != 0)) {
      dst[index] = src[index];
      index++;
   }
   dst[index] = '\0';
}
