#include "queue.h"
#include "../core/sync.h"
#include "../input/keystroke.h"
#include "../logging/logger.h"
#include "../win/win_error.h"
#include <handleapi.h>
#include <minwindef.h>
#include <synchapi.h>
#include <windows.h>

KeystrokeQueue::KeystrokeQueue() {
   // Initialise synchronisation primitives
   InitializeCriticalSection(&lock);

   semaphore = CreateSemaphore(nullptr,        // Default security attributes
                               0,              // Initial count
                               MAX_QUEUE_SIZE, // Maximum count
                               nullptr         // Unnamed semaphore
   );
   if (semaphore == nullptr) {
      BFWMLogLastError("CreateSemaphore failed");
      Fatal("Could not create keystroke queue semaphore");
   }
}

KeystrokeQueue::~KeystrokeQueue() {
   if (semaphore != nullptr) {
      CloseHandle(semaphore);
      semaphore = nullptr;
   }

   DeleteCriticalSection(&lock);
}

auto KeystrokeQueue::Enqueue(const Keystroke &key) -> bool {
   BOOL dropped = FALSE;

   ScopedLock const guard(lock);

   if (count >= MAX_QUEUE_SIZE) {
      // Queue full - drop oldest
      head = (head + 1) % MAX_QUEUE_SIZE;
      count--;
      dropped = TRUE;
   }

   keys[tail] = key;
   tail = (tail + 1) % MAX_QUEUE_SIZE;
   count++;

   /* Only release the semaphore if we didn't drop an entry.
      When dropping, the consumed semaphore token is recycled for the new item,
      so releasing again would overcount. */
   if (dropped == 0) {
      if (ReleaseSemaphore(semaphore, 1, nullptr) == 0) {
         BFWMLogLastError("ReleaseSemaphore failed");
         return false;
      }
   }

   return true;
}

auto KeystrokeQueue::Dequeue(Keystroke &out_key) -> bool {
   DWORD const dwWaitResult = WaitForSingleObject(semaphore, 0);
   if (dwWaitResult == WAIT_OBJECT_0) {
      ScopedLock const guard(lock);

      out_key = keys[head];
      head = (head + 1) % MAX_QUEUE_SIZE;
      count--;

      return true;
   }

   return false;
}