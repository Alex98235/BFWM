/**
 * @file layout_test_access.h
 * @brief Test-only accessors that expose layout engine internals.
 *
 * The engines keep their state protected/private. These thin subclasses
 * promote the members white-box tests inspect via using-declarations, so the
 * production headers stay free of test scaffolding and remain ODR-clean.
 *
 * Use these types (not the base engines) in tests that read engine internals.
 */

#ifndef BFWM_LAYOUT_TEST_ACCESS_H
#define BFWM_LAYOUT_TEST_ACCESS_H

#include "../../src/workspace/layouts/dwindle/dwindle.h"
#include "../../src/workspace/layouts/master/master.h"
#include "../../src/workspace/layouts/monocle/monocle.h"

/// DwindleEngine with tree internals exposed for white-box tests.
struct TestDwindle : DwindleEngine {
 public:
   using BaseLayout::border_width;
   using BaseLayout::gap_between;
   using BaseLayout::gap_edge;
   using BaseLayout::tiled_rect;
   using BaseLayout::workspace_rect;
   using DwindleEngine::root;
   using DwindleEngine::split_ratio;
   using DwindleEngine::window_count;
};

/// MasterEngine with sequence/geometry internals exposed for white-box tests.
struct TestMaster : MasterEngine {
 public:
   using BaseLayout::border_width;
   using BaseLayout::gap_between;
   using BaseLayout::gap_edge;
   using BaseLayout::tiled_rect;
   using BaseLayout::workspace_rect;
   using MasterEngine::master_count;
   using MasterEngine::master_factor;
   using SequenceLayout::selected_hwnd;
   using SequenceLayout::windows;
};

/// MonocleEngine with sequence/geometry internals exposed for white-box tests.
struct TestMonocle : MonocleEngine {
 public:
   using BaseLayout::border_width;
   using BaseLayout::gap_between;
   using BaseLayout::gap_edge;
   using BaseLayout::tiled_rect;
   using BaseLayout::workspace_rect;
   using SequenceLayout::selected_hwnd;
   using SequenceLayout::windows;
};

#endif /* BFWM_LAYOUT_TEST_ACCESS_H */