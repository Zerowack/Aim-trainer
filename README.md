# RawAim: an aim trainer calibrated for Valorant

RawAim is a native Windows aim trainer written in C++17 with raylib. It reads your
mouse through the **Windows Raw Input API**, so your aim feels exactly like in
Valorant:

- **1:1 with Valorant:** every mouse count turns `sens × 0.07°`, with a 103° horizontal FOV (Hor+).
  There is no smoothing and no acceleration, and Windows "Enhance pointer precision" is bypassed.
- **Every raw packet counts:** all mouse packets between frames are applied in order,
  so nothing is lost at 1000–8000 Hz polling. Each click is judged at the exact
  crosshair position where it happened, even in the middle of a frame.
- **Timing:** FPS is uncapped by default with V-Sync off. You can set an optional cap (144/240/360),
  and an FPS counter shows frame time. All timing uses `QueryPerformanceCounter`.
- **Six modes:** Gridshot, Microshot, Tracking (ADAD strafes), Flick 180, Reaction and Peek Practice.
- **Sens Finder:** uses the Perfect Sensitivity Approximation (PSA) method. It runs 7 rounds of blind A/B
  tests, draws a graph of your results, and has a one-click "apply" button. Every session
  is saved and combined into an average across days.
- **Stats:** after each run you get score, accuracy, reaction time, time-to-kill (TTK), and your
  overshoot/undershoot tendency, plus a coaching tip (e.g. *"You overshoot flicks, try
  lowering sens ~5%"*). Per-mode progress graphs and personal bests are included.
- **Crosshair editor:** Valorant-style, with a live preview. You can **paste your Valorant crosshair share code** to import it. You can
  also change target colour, map brightness, volume and keybinds.

No Riot logos, fonts or assets are used. The UI uses the Windows system font
(Bahnschrift or Segoe UI), and every sound is generated in code.

---

## Build it (step by step)

You only need two free tools: **Visual Studio 2022 Build Tools** (the C++
compiler) and **CMake** (the build system). raylib is downloaded automatically.

### 1. Install Visual Studio 2022 Build Tools (the C++ compiler)

1. Go to <https://visualstudio.microsoft.com/downloads/>.
2. Scroll down to **"Tools for Visual Studio"** and download
   **"Build Tools for Visual Studio 2022"**.
3. Run the installer. On the **Workloads** tab, tick
   **"Desktop development with C++"**. Keep the default options on the right,
   which include "MSVC v143" and a "Windows 11 SDK" (or Windows 10 SDK).
4. Click **Install** and wait. It is a few GB, so it can take a while.

*(Alternative for experienced users: in PowerShell run
`winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"`.)*

### 2. Install CMake

1. Go to <https://cmake.org/download/>.
2. Download the **Windows x64 Installer** (`cmake-x.y.z-windows-x86_64.msi`).
3. Run it. When asked, choose **"Add CMake to the PATH for all users"**.

*(Alternative: `winget install Kitware.CMake`.)*

### 3. Get the code

- On the GitHub page click **Code → Download ZIP**, then extract it to a simple
  path such as `C:\RawAim`.
  *(Or, if you use git: `git clone <repo-url> C:\RawAim`.)*

The folder should contain `CMakeLists.txt`, `README.md` and a `src` folder.

### 4. Open the right terminal

Press the Windows key, type **"Developer PowerShell for VS 2022"** and open it.
(A normal PowerShell also works if you installed CMake with "Add to PATH". The
Developer PowerShell is the safest choice.)

Go to the project folder:

```powershell
cd C:\RawAim
```

### 5. Configure (only needed once)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

The first time, this downloads raylib 5.5 from GitHub, so you need an
internet connection. It takes about a minute. It is finished when you see
`-- Build files have been written to: C:/RawAim/build`.

### 6. Build in Release mode

```powershell
cmake --build build --config Release
```

The first build compiles raylib as well and takes 1–2 minutes. Later builds are fast.

### 7. Run it

The program is here:

```
C:\RawAim\build\Release\RawAim.exe
```

Double-click it, or run `.\build\Release\RawAim.exe` in the terminal.
The .exe is self-contained (the C++ runtime is linked statically). You can copy it
anywhere, for example to your desktop. It stores its files **next to the .exe**:

| File | Contents |
|---|---|
| `config.ini` | all settings (DPI, sens, video, crosshair, keybinds …) |
| `stats.csv` | one line per finished run (opens in Excel) |
| `finder_sessions.csv` | one line per Sens Finder session (recommendation, cm/360) |
| `finder_tests.csv` | every single Sens Finder test with its score breakdown |

> Windows SmartScreen may warn you the first time, because the .exe is not signed.
> Click **More info → Run anyway**.

### Optional: Debug build with the math self-test

```powershell
cmake --build build --config Debug
.\build\Debug\RawAim.exe
```

Debug builds run a self-test at startup that checks the sensitivity, cm/360,
FOV, camera, PSA and crosshair-code math against known answers. The result is shown at
the bottom of the main menu ("math self-test PASSED"). If anything fails, a
message box lists the failing checks.

### Troubleshooting

- **`cmake` is not recognized**: close the terminal and open a new one after
  installing CMake, or use the "Developer PowerShell for VS 2022".
- **"No CMAKE_CXX_COMPILER could be found" / generator not found**: the
  "Desktop development with C++" workload is missing. Re-run the Visual Studio
  Installer, click **Modify** and tick it.
- **raylib download fails**: check your internet connection or firewall, then
  delete the `build` folder and run step 5 again.
- **FPS stuck at 60/144/240 even with "Uncapped"**: your GPU driver forces
  V-Sync or a frame limit. In the NVIDIA Control Panel or AMD Software, set
  V-Sync to "Off / application controlled" for RawAim.exe.
- **Starting over**: delete the `build` folder and repeat steps 5–6.
  To reset settings, delete `config.ini` next to the .exe.

---

## Using RawAim

**First launch:** enter your mouse DPI and your current Valorant sensitivity.
RawAim shows your eDPI (`DPI × sens`) and cm/360
(`360 / (DPI × sens × 0.07) × 2.54`).

**Controls (defaults, rebindable in Settings → Keybinds):**

| Action | Key |
|---|---|
| Shoot | Mouse 1 |
| Pause / menu | Esc (always) or P |
| Restart run | R |
| FPS counter on/off | F2 |

Alt-tab or any focus loss pauses the run and releases the cursor immediately.

**Modes** (60 s by default, adjustable in Settings → Gameplay, with a 3 s countdown):

1. **Gridshot**: 3 body-sized targets on a grid 10 m away. Destroy one and a new one spawns.
2. **Microshot**: head-sized (~25 cm) targets at 10–16 m that vanish after 1.4 s.
3. **Tracking**: a 1.75 m agent at 13 m doing Valorant-style ADAD strafes at 5.4 m/s,
   with random timing and counter-strafe stops. Hold fire while on target; the score is time-on-target %.
4. **Flick 180**: targets spawn 90–180° to your side or behind you. An arrow points to them.
5. **Reaction**: the target appears after a random delay. Click as fast as you can.
   Timing starts at the first frame that shows the target. Clicking early costs points.
6. **Peek Practice**: agents swing out from behind three cover boxes for a short
   window, like holding an angle. Headshots score extra.

**Results:** score, accuracy, hits/misses, average reaction time, average TTK,
over/undershoot, average click error and coaching tips. Everything is appended to
`stats.csv`. **Stats & Progress** shows per-mode graphs, personal bests and recent runs.

**Sens Finder (PSA):**

1. It starts from your current sens and tests `×1.5` and `×0.5`.
2. Each round has two blind 20-second tests (A/B, in random order): 7 s of flicks,
   7 s of tracking and 6 s of micro-adjustments. After each test you rate its comfort from 1 to 5.
3. Each test is scored from 0 to 100 as: 25% accuracy, 20% TTK, 15% click precision
   (over/undershoot relative to target size), 25% tracking and 15% comfort.
4. The weaker side moves to the midpoint, so the range halves toward the better side.
   After 7 rounds the midpoint is your recommendation, shown with eDPI, cm/360 and a
   graph of every tested sens against its score. Click **Apply** to use it.
5. Run it on different days. The combined recommendation averages all sessions in
   cm/360, so it stays correct even if you change DPI.

---

## How the accuracy-critical parts work

- **Raw Input:** `platform_win32.cpp` calls `RegisterRawInputDevices` and
  subclasses the raylib/GLFW window procedure to receive `WM_INPUT`. Every
  packet is queued with a QPC timestamp. Movement is merged, and a click always
  splits the queue, so the game rotates the camera by exactly the movement that came
  before each click. `windows.h` is only included in that one file, because it
  clashes with raylib's names.
- **Cursor:** while playing, the cursor is hidden and clipped to a 1×1 pixel
  rectangle at the window centre. It is released on pause, alt-tab or focus loss.
- **Frame pacing:** input is pumped at the very start of each frame. When an FPS cap is set,
  the wait uses a high-resolution waitable timer followed by a short spin, never V-Sync.
- **Math:** `camera.cpp`: yaw/pitch change = `counts × sens × 0.07°` and pitch is
  clamped to ±89°. The vertical FOV is `2·atan(tan(103°/2) / (16/9)) ≈ 70.53°` (fixed,
  Hor+), and the horizontal FOV is `2·atan(tan(vFOV/2) × aspect)`, which is exactly 103° at 16:9.
- **Overshoot/undershoot:** at each click, the angular error to the target is projected
  onto the direction the crosshair was moving over the last 80 ms. A positive error
  means the crosshair hadn't reached the target yet (undershoot); a negative error means
  it had already passed it (overshoot).

## Source files

| File | Purpose |
|---|---|
| `src/main.cpp` | entry point |
| `src/app.h`, `src/app.cpp` | main loop, display modes, run flow, HUD, pause menu |
| `src/app_screens.cpp` | menus, results, settings, stats and Sens Finder screens |
| `src/platform.h`, `src/platform_win32.cpp` | Raw Input, QPC timer, cursor lock, focus, V-Sync, precise sleep |
| `src/input.h`, `src/input.cpp` | keybinds and in-order processing of the raw mouse stream |
| `src/camera.h`, `src/camera.cpp` | Valorant sens / cm360 / FOV math, camera, aim history |
| `src/targets.h`, `src/targets.cpp` | hitbox sizes, ray hit tests, target drawing |
| `src/world.h`, `src/world.cpp` | shooting range, lighting shader, cover boxes |
| `src/modes.h`, `src/modes.cpp` | the six training modes and the mixed Sens Finder test |
| `src/sens_finder.h`, `src/sens_finder.cpp` | PSA logic, scoring, session storage and averaging |
| `src/stats.h`, `src/stats.cpp` | run statistics, coaching tips, `stats.csv` |
| `src/config.h`, `src/config.cpp` | settings and `config.ini` |
| `src/crosshair.h`, `src/crosshair.cpp` | crosshair drawing and share codes |
| `src/ui.h`, `src/ui.cpp` | dark, sharp-angled immediate-mode UI and charts |
| `src/audio.h`, `src/audio.cpp` | sound effects synthesised at startup |
| `src/rng.h` | random numbers |
| `src/selftest.h`, `src/selftest.cpp` | startup math self-test (Debug builds) |

### Importing your Valorant crosshair

In Valorant open **Settings → Crosshair → Crosshair Profile → Export**. This copies
a code like `0;P;c;5;h;0;f;0;0l;4;0o;2;0a;1;0f;0;1b;0`. In RawAim go to
**Settings → Crosshair**, click **PASTE** (or click the code box and press Ctrl+V), then
click **IMPORT**. RawAim imports the primary crosshair: colour (including custom
colours), outlines, centre dot, and inner and outer lines. Valorant-only extras such as
firing/movement error, separate vertical length, and the ADS and sniper crosshairs are ignored.

### RawAim crosshair code format

```
XH1;c=00FF00;o=1;ot=1;oa=0.50;d=0;dt=2;da=1.00;i=1;ia=0.80;il=6;it=2;io=3;x=0;xa=0.35;xl=2;xt=2;xo=10
```

| Key | Meaning | Range |
|---|---|---|
| `c` | colour (hex RRGGBB) | |
| `o`, `ot`, `oa` | outline on/off, thickness, opacity | 0/1, 1–6, 0–1 |
| `d`, `dt`, `da` | centre dot on/off, thickness, opacity | 0/1, 1–6, 0–1 |
| `i`, `ia`, `il`, `it`, `io` | inner lines on/off, opacity, length, thickness, offset | 0/1, 0–1, 0–20, 1–10, 0–20 |
| `x`, `xa`, `xl`, `xt`, `xo` | outer lines on/off, opacity, length, thickness, offset | 0/1, 0–1, 0–20, 1–10, 0–40 |

Keys can come in any order, and missing keys use the defaults. Paste a code into
Settings → Crosshair and press **Import**, or press **Copy** to share yours.
