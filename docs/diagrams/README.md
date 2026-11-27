# BFWM architecture diagrams

Hand-authored draw.io diagrams of how BFWM works, meant to be read next to the
source: every box names a real symbol and the `file:line` it lives at.

They exist to make the awkward parts legible — the two-thread handoff, the
deferred transaction commit, the config pipeline, the window lifecycle —
including behaviour that is surprising or currently wrong.

## Files

| File | Question it answers |
| --- | --- |
| `system-overview.drawio` | What are the parts, which thread owns each, what crosses a boundary? (C4 container view) |
| `threading-and-queues.drawio` | How do the two threads talk, what is lock-free, what may the worker never touch? |
| `transaction-system.drawio` | Why is nothing applied immediately, and in what order do the Win32 calls finally happen? |
| `config-loading.drawio` | How does `config.lua` become runtime state, and what happens when it is wrong? |
| `window-lifecycle.drawio` | How does a foreign `HWND` become a tiled window, and how does it die? |
| `workspace-and-layouts.drawio` | Who owns monitors, workspaces and windows; how do the layouts and the bar get their data? |

## Provenance

Each page carries a footer cell stating the commit it was written against, and a
machine-readable cell wrapping it:

```xml
<object id="prov" basedOn="1a2b3cd" date="2026-11-27"
        covers="src/transaction/operation.h;src/transaction/transaction.cpp;...">
```

`covers` lists the paths whose change invalidates the diagram. Keep it as tight
as the diagram's actual scope.

## Updating

1. Edit in draw.io (desktop app or <https://app.diagrams.net>).
2. Set `basedOn` to `git rev-parse --short HEAD`, `date` to today, and update the
   footer text to match.
3. Widen `covers` if the diagram now covers more paths.
4. Commit the diagram with the change that invalidated it.

## Conventions

- **Colour encodes ownership**: blue = main thread, green = worker thread,
  yellow = queued or shared state, purple = Lua / DirectComposition, grey =
  external, red = sharp edge.
- **Red boxes are claims about the code** at `basedOn`, not instructions. Verify
  before acting on them.
- **Dashed arrows** mean a delayed or asynchronous relationship.
- Containers are `swimlane` shapes and mark a thread or lifecycle boundary.
- Where a diagram and the source disagree, the source wins — then fix the diagram.

## Adding a diagram

Copy the `prov` block from an existing file, add a visible footer and a `Legend:`
text cell, and add a row to the table above.
