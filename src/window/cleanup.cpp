/**
 * @file window_cleanup.c
 * @brief Implementation for cleaning up window registries after termination.
 *
 * Provides CleanupTerminatedWindow(), a single helper that removes a window
 * from both the WindowRegistry and its owning Workspace, frees the Window
 * struct, and re-applies layout.
 */

#include "cleanup.h"

#include "../bar/bar.h"
#include "../core/main_context.h"
#include "../core/shared_state.h"
#include "../core/sync.h"
#include "../logging/logger.h"
#include "../monitor/monitor.h"
#include "../workspace/workspace.h"
#include "window.h"
#include <synchapi.h>
#include <windef.h>

void CleanupTerminatedWindow(struct SharedState *shared,
                             struct MainContext *main, HWND hWnd) {
   if ((shared == nullptr) || (hWnd == nullptr))
      return;

   Workspace *workspace = nullptr;
   BOOL need_focus = FALSE;
   HWND fallback = nullptr;
   {
      ScopedLock const lock(main->lock);

      Window *win = main->windows->FindByHwnd(hWnd);
      if (win == nullptr) {
         Debug("CleanupTerminatedWindow: HWND %p not in window registry", hWnd);
         return;
      }

      workspace = FindWorkspaceByHwnd(shared, main, hWnd);

      need_focus = static_cast<BOOL>(main->focused_hwnd == hWnd);
      BOOL const was_fullscreen = win->IsFullscreen();

      // Compute fallback BEFORE removal — the reference window must still
      // be in the layout tree for get_closest_window to find it.
      if ((need_focus == TRUE) && (workspace != nullptr) &&
          workspace->Windows().size() > 1 &&
          (workspace->GetEngine() != nullptr)) {
         fallback = workspace->GetEngine()->get_closest_window(hWnd);
      }

      if (workspace != nullptr)
         workspace->RemoveWindow(win);

      if ((was_fullscreen == TRUE) && (workspace != nullptr)) {
         Monitor *mon = FindMonitorByWorkspace(shared, main, workspace);
         if (mon != nullptr) {
            if (mon->GetFullscreenCount() > 0)
               mon->DecrementFullscreenCount();
            if (mon->GetFullscreenCount() == 0 && (mon->GetBar() != nullptr))
               mon->GetBar()->Show();
         }
      }

      if (need_focus == TRUE)
         main->focused_hwnd = nullptr;

      main->windows->Remove(win);
   }

   if (workspace != nullptr) {
      workspace->ApplyLayout(shared, main);
      if (need_focus == TRUE) {
         if ((fallback == nullptr) && !workspace->IsEmpty())
            fallback = workspace->Windows()[0]->GetHwnd();
         if (fallback != nullptr)
            FocusWindow(fallback, shared, main);
      }
   }
}
