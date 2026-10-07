/**
 * @file crash_dump.c
 * @brief Vectored Exception Handler crash dump implementation.
 */

#include "crash_dump.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

enum {
   /** Total bytes of the format buffer. */
   CRASH_DUMP_BUF_SIZE = 1024,
   /** Max call-stack frames captured (first skipped). */
   CRASH_STACK_FRAMES = 8,
   /** Min remaining buffer bytes to keep printing stack. */
   CRASH_LINE_SAFETY_MARGIN = 64,
   /** AddVectoredExceptionHandler first-call flag. */
   CRASH_VEH_FIRST = 1
};

namespace {
/*
 * ------------------------------------------------------------------
 *  Module base address, captured once at startup so that every
 *  crash report can show the instruction offset relative to the
 *  executable image (stable across ASLR-randomized base addresses).
 * ------------------------------------------------------------------
 */
HMODULE crash_module_base;

/*
 * ------------------------------------------------------------------
 *  Format helper — appends printf-style text to a fixed buffer,
 *  returning the new write position (or unchanged on overflow).
 * ------------------------------------------------------------------
 */
inline auto AppendToBuffer(char *buffer, int position, int capacity,
                           const char *format, ...) -> int {
   va_list args;
   va_start(args, format);
   int const result = vsnprintf(buffer + position,
                                (size_t)(capacity - position), format, args);
   va_end(args);
   if (result > 0)
      position += result;
   return position;
}

/*
 * ------------------------------------------------------------------
 *  Vectored Exception Handler — the callback invoked by the OS when
 *  an exception occurs that no handler has yet claimed.
 *
 *  Prints the crash report to stderr via WriteFile (safe inside a
 *  VEH which avoids CRT functions that may hold locks).  Then returns
 *  EXCEPTION_CONTINUE_SEARCH so the normal crash path (debugger,
 *  WER, or silent termination) proceeds unchanged.
 * ------------------------------------------------------------------
 */
inline auto CALLBACK CrashDumpHandler(struct _EXCEPTION_POINTERS *exception)
    -> LONG {
   HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
   if (stderr_handle == INVALID_HANDLE_VALUE || stderr_handle == nullptr)
      return EXCEPTION_CONTINUE_SEARCH;

   std::string buffer;
   buffer.resize(CRASH_DUMP_BUF_SIZE);
   int position = 0;

   position = AppendToBuffer(
       buffer.data(), position, (int)buffer.size(),
       "\n==================== BFWM CRASH ====================\n");

   position = AppendToBuffer(
       buffer.data(), position, (int)buffer.size(),
       "Code: 0x%08lx  Flags: 0x%lx  Addr: %p"
       "  ModuleBase: %p  Offset: 0x%llx\n",
       (unsigned long)exception->ExceptionRecord->ExceptionCode,
       (unsigned long)exception->ExceptionRecord->ExceptionFlags,
       (void *)exception->ExceptionRecord->ExceptionAddress,
       (void *)crash_module_base,
       (unsigned long long)((uintptr_t)
                                exception->ExceptionRecord->ExceptionAddress -
                            (uintptr_t)crash_module_base));

#if defined(_M_AMD64) || defined(__x86_64__)
   position = AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                             "RIP: 0x%llx  RSP: 0x%llx  RBP: 0x%llx\n",
                             (unsigned long long)exception->ContextRecord->Rip,
                             (unsigned long long)exception->ContextRecord->Rsp,
                             (unsigned long long)exception->ContextRecord->Rbp);
   position =
       AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                      "RAX: 0x%llx  RBX: 0x%llx  RCX: 0x%llx  RDX: 0x%llx\n",
                      (unsigned long long)exception->ContextRecord->Rax,
                      (unsigned long long)exception->ContextRecord->Rbx,
                      (unsigned long long)exception->ContextRecord->Rcx,
                      (unsigned long long)exception->ContextRecord->Rdx);
   position =
       AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                      "RDI: 0x%llx  RSI: 0x%llx  R8: 0x%llx  R9: 0x%llx\n",
                      (unsigned long long)exception->ContextRecord->Rdi,
                      (unsigned long long)exception->ContextRecord->Rsi,
                      (unsigned long long)exception->ContextRecord->R8,
                      (unsigned long long)exception->ContextRecord->R9);
   position =
       AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                      "R10: 0x%llx  R11: 0x%llx  R12: 0x%llx  R13: 0x%llx\n",
                      (unsigned long long)exception->ContextRecord->R10,
                      (unsigned long long)exception->ContextRecord->R11,
                      (unsigned long long)exception->ContextRecord->R12,
                      (unsigned long long)exception->ContextRecord->R13);
   position = AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                             "R14: 0x%llx  R15: 0x%llx\n",
                             (unsigned long long)exception->ContextRecord->R14,
                             (unsigned long long)exception->ContextRecord->R15);
#elif defined(_M_IX86) || defined(__i386__)
   position = AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                             "EIP: 0x%08lx  ESP: 0x%08lx  EBP: 0x%08lx\n",
                             (unsigned long)exception->ContextRecord->Eip,
                             (unsigned long)exception->ContextRecord->Esp,
                             (unsigned long)exception->ContextRecord->Ebp);
   position = AppendToBuffer(
       buffer.data(), position, (int)buffer.size(),
       "EAX: 0x%08lx  EBX: 0x%08lx  ECX: 0x%08lx  EDX: 0x%08lx\n",
       (unsigned long)exception->ContextRecord->Eax,
       (unsigned long)exception->ContextRecord->Ebx,
       (unsigned long)exception->ContextRecord->Ecx,
       (unsigned long)exception->ContextRecord->Edx);
#endif

   {
      std::array<void *, CRASH_STACK_FRAMES> stack;
      USHORT const frame_count = RtlCaptureStackBackTrace(
          1, CRASH_STACK_FRAMES, stack.data(), nullptr);
      position = AppendToBuffer(buffer.data(), position, (int)buffer.size(),
                                "Stack (%u frames):\n", (unsigned)frame_count);
      for (USHORT frame_index = 0;
           frame_index < frame_count &&
           (size_t)position < buffer.size() - CRASH_LINE_SAFETY_MARGIN;
           frame_index++) {
         auto address = (uintptr_t)stack[frame_index];
         position = AppendToBuffer(
             buffer.data(), position, (int)buffer.size(),
             "  %2u: %p  (base+0x%llx)\n", (unsigned)frame_index,
             (void *)address,
             (unsigned long long)(address - (uintptr_t)crash_module_base));
      }
   }

   position = AppendToBuffer(
       buffer.data(), position, (int)buffer.size(),
       "========================================================\n\n");

   DWORD written;
   WriteFile(stderr_handle, buffer.data(), (DWORD)position, &written, nullptr);
   FlushFileBuffers(stderr_handle);
   return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void CrashDumpInit() {
   crash_module_base = GetModuleHandleW(nullptr);
   AddVectoredExceptionHandler(CRASH_VEH_FIRST, CrashDumpHandler);
}
