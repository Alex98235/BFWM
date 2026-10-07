/**
 * @file operation.h
 * @brief Defines pending window operations that are queued during a
 *        transaction and applied atomically in BFWMTransactionCommit().
 *
 * Each operation type represents a deferred Win32 call. Operations are
 * collected during keystroke handler execution and flushed in priority
 * order at commit time: moves → border colors → focus.
 */

#ifndef BFWM_PENDING_OPERATION_H
#define BFWM_PENDING_OPERATION_H

#include <windows.h>

class Workspace;

/**
 * @brief Types of pending window operations.
 *
 * These are ordered by the priority they are applied in. The enum values
 * are used as a sort key so the commit loop can process them in the correct
 * order without a separate sort step.
 */
using OperationType = enum {
   /// SetWindowPos (positions only, no borders)
   OP_MOVE_WINDOW = 0,
   /// Recompute and apply layout for a workspace
   OP_RELAYOUT_WORKSPACE,
   /// Repaint the border overlay ring (OverlaySetColor)
   OP_BORDER_COLOR,
   /// DwmSetWindowAttribute DWMWA_CLOAKED
   OP_CLOAK_WINDOW,
   /// PostMessage WM_CLOSE
   OP_CLOSE_WINDOW,
   /// SwitchToThisWindow + active border
   OP_FOCUS_WINDOW,
   /// SetWindowPos with SWP_NOZORDER=false to change Z-order
   OP_ZORDER,
   /// ShowWindow(SW_SHOW) or similar
   OP_SHOW_WINDOW,
   /// ShowWindow(SW_HIDE)
   OP_HIDE_WINDOW,
   /// SetWindowLong(GWL_STYLE / GWL_EXSTYLE)
   OP_SET_WINDOW_STYLE,
   /// RedrawWindow for border area
   OP_REDRAW_BORDER,
   /// RedrawWindow for client content area
   OP_REDRAW_CONTENT,
   /// Minimize window via ShowWindow(SW_MINIMIZE)
   OP_MINIMIZE_WINDOW,
   /// Update ctx->focus_tracking state (no Win32 call, internal state only)
   OP_UPDATE_FOCUS_TRACKING,
   /// Show/hide the workspace bar based on context (fullscreen state, etc.)
   OP_BAR_VISIBILITY,
};

/**
 * @brief A single deferred window operation.
 *
 * Each operation holds the data needed to perform one Win32 call at
 * commit time. The type field determines which union member is active.
 */
using PendingOperation = struct PendingOperation {
   OperationType type;
   HWND hwnd;

   union op_types {
      /** Data for OP_MOVE_WINDOW */
      struct {
         int x, y;
         int width, height;
      } move_rect;

      /** Data for OP_RELAYOUT_WORKSPACE: the workspace to re-layout */
      Workspace *workspace;

      /** Data for OP_BORDER_COLOR */
      struct {
         COLORREF color;
      } border;

      /** Data for OP_CLOAK_WINDOW */
      struct {
         BOOL cloaked;
      } cloak;

      /** Data for OP_ZORDER */
      struct {
         HWND insert_after; // HWND_TOP, HWND_BOTTOM, HWND_NOTOPMOST, or a
                            // window handle
      } z_order;

      /** Data for OP_SHOW_WINDOW / OP_HIDE_WINDOW */
      struct {
         BOOL visible; // TRUE for show, FALSE for hide
      } show_hide;

      /** Data for OP_SET_WINDOW_STYLE */
      struct {
         DWORD style;   // new GWL_STYLE value
         DWORD exstyle; // new GWL_EXSTYLE value
      } window_style;

      /** Data for OP_BAR_VISIBILITY */
      struct {
         BOOL enabled; // TRUE to show bar, FALSE to hide bar
      } bar_visibility;

      // OP_REDRAW_BORDER, OP_REDRAW_CONTENT, OP_MINIMIZE_WINDOW,
      // OP_UPDATE_FOCUS_TRACKING use only hwnd (no extra data)
   } op_types;
};

#endif /* BFWM_PENDING_OPERATION_H */
