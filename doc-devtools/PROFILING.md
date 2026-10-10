# Profiling

ETW-based profiling of BFWM with [PerfView](https://github.com/microsoft/perfview).
This is optional dev tooling; nothing here is needed to build or run the WM.

## Build for profiling

Normal builds link with GNU `ld` and emit no debug info. To profile, enable the
opt-in profiling build, which emits CodeView debug info and a PDB (via `lld`)
so PerfView can resolve symbols:

```sh
cmake -S . -B build-profile -G "MSYS Makefiles" \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/clang.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/clang++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe \
  -DBFWM_PROFILE_BUILD=ON
cmake --build build-profile --target BFWM
```

This produces `build-profile/BFWM.exe` and `BFWM.pdb` alongside it. A separate
build directory is recommended so profiling flags never leak into normal builds;
when the option is `OFF`, the normal GNU `ld` build is unchanged.

## Tools

- **PerfView** — a self-contained executable; no separate runtime or profile
  files needed. Its GUI does the analysis (CPU flame graphs, thread-time
  analysis, automatic symbol resolution from the PDB next to the exe).
- [uv](https://docs.astral.sh/uv/) — not required for profiling itself, only if
  you also use [bfwm-tidy](BFWM_TIDY.md).

## Capturing

`scripts/ProfileCapture.ps1` runs a full capture: it self-elevates (kernel ETW
requires admin), starts PerfView, runs a synthetic workload, waits for the
auto-stop, and reports. It prompts for a capture name, duration, and window
churn.

```powershell
powershell -File scripts/ProfileCapture.ps1
```

Captures are written to `profiling/captures/<name>.etl.zip` under the repo root
(git-ignored). PerfView is auto-detected — from `PATH`, the standard install
locations, `$env:BFWM_PERFVIEW`, or the `-PerfView <path>` and
`-CaptureDir <path>` overrides.

`scripts/ProfileWorkload.ps1` is the synthetic workload on its own — it sends
keystrokes that exercise the WM's hot paths (focus switching, layout cycles, gap
toggles, config reloads) and is safe on a live desktop (it never kills or moves
windows):

```powershell
powershell -File scripts/ProfileWorkload.ps1 -DurationSec 60 -BaseDelayMs 120 -WindowChurn 0
```

### Analysing

```
PerfView.exe open <capture>.etl.zip
```

Then use the **CPU Stacks** and **Thread Time** views.

## Gotchas

- **UIPI.** BFWM's keyboard hook runs at medium integrity, so it cannot see
  input directed at a higher-integrity (elevated) window. Run the workload from
  a normal, non-elevated shell — or let `ProfileCapture.ps1` minimize the
  elevated console (it does this automatically) — so the synthetic keys reach a
  normal window.
