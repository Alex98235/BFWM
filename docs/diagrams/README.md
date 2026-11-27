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

## Maintenance

These are hand-maintained documentation. Nothing verifies that they are current,
so update a diagram when the behaviour it describes changes, and commit it with
that change.

Each page carries a footer naming the commit it was drawn against, and a `prov`
cell wrapping it with `basedOn` / `date` / `covers` attributes:

```xml
<object id="prov" basedOn="1a2b3cd" date="2026-11-27"
        covers="src/transaction/operation.h;src/transaction/transaction.cpp;...">
```

They are reading aids, not guarantees, and they drift. Read `basedOn` as
"roughly when this was drawn", and prefer the footer text and the diagram
content where the two disagree.

## Updating

1. Edit in draw.io (desktop app or <https://app.diagrams.net>).
2. Set `date` to today, update the footer, and point `basedOn` at the commit you
   drew against.
3. Widen `covers` if the diagram now covers more paths.
4. Commit the diagram with the code change that invalidated it.

## Conventions

- **Colour encodes ownership**: blue = main thread, green = worker thread,
  yellow = queued or shared state, purple = Lua / DirectComposition, grey =
  external, red = sharp edge.
- **Red boxes are claims about the code** as drawn, not instructions. Verify
  before acting on them.
- **Dashed arrows** mean a delayed or asynchronous relationship.
- Containers are `swimlane` shapes and mark a thread or lifecycle boundary.
- Where a diagram and the source disagree, the source wins — then fix the diagram.

## Adding a diagram

There is no template file. Start from the smallest existing diagram,
`system-overview.drawio`, so the document prolog and the two bookkeeping cells
come for free. Keep both cells and replace the content around them:

| Cell | How to find it | What it carries |
| --- | --- | --- |
| provenance footer | `<object id="prov" …>` wrapping a text `mxCell`, bottom-left | the visible `Based on commit …` line plus `basedOn` / `date` / `covers` |
| legend | `<mxCell id="legend" …>`, same text style | the colour key |

Set `basedOn`, `date`, the footer text and `covers` to the commit you are drawing
against, adjust the legend to the colours actually used, and add a row to the
table at the top of this file.
