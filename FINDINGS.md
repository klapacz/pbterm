# Findings: pbterm + Ghostling/libghostty + Pocketbook-Texteditor

Date: 2026-05-09

This repo is `pbterm`, a PocketBook terminal app. The goal investigated here is whether it is feasible to combine:

- `pbterm` for PocketBook/InkView app lifecycle, shell/PTTY integration, menus, touch/buttons, orientation, and SDK build setup.
- `ghostling` / `libghostty-vt` for real terminal emulation and TUI rendering.
- `Pocketbook-Texteditor` for Bluetooth keyboard input on PocketBook via Linux evdev.

Short verdict: **yes, feasible**, but this is a substantial integration/rewrite, not a simple source merge.

## Repository layout and references

Current working repo:

- `.`: pbterm repo.
- Main source directory: `src/`.
- Important pbterm files:
  - `src/pbterm.cpp`: InkView event-loop entry point.
  - `src/Messenger.cpp`: central app controller / message router.
  - `src/Term.cpp`: shell spawning, PTY/pipes, read/write polling.
  - `src/Display.cpp`: current screen drawing wrapper.
  - `src/Lines.cpp`, `src/Line.cpp`: current line-oriented output buffer and wrapping.
  - `src/Keyboard_Handler.cpp`: current on-screen keyboard command entry.
  - `src/Button_Handler.cpp`, `src/Pointer_Handler.cpp`, `src/Rotation_Handler.cpp`: hardware buttons, touch, orientation.
  - `src/Inkview.hpp`: wrapper around `inkview.h` plus compatibility notes.
  - `CMakeLists.txt`: current PocketBook cross-build setup.

Reference repos cloned under `.reference/`:

- `.reference/ghostling/`
  - Minimal terminal demo using `libghostty-vt` C API.
  - Important files:
    - `.reference/ghostling/README.md`: explains Ghostling and libghostty-vt.
    - `.reference/ghostling/main.c`: all relevant libghostty usage: PTY read/write, key/mouse encoding, render-state iteration, resize, effects callbacks.
    - `.reference/ghostling/CMakeLists.txt`: FetchContent setup for Raylib and Ghostty/libghostty-vt.
  - Note: Ghostling itself uses Raylib, so it is **not directly portable** to PocketBook. Use it as an API/reference implementation for libghostty-vt only.

- `.reference/Pocketbook-Texteditor/`
  - PocketBook text editor that can use external Bluetooth keyboards.
  - Important files:
    - `.reference/Pocketbook-Texteditor/README.md`: requirements and usage.
    - `.reference/Pocketbook-Texteditor/src/handler/eventHandler.cpp`: Bluetooth enable/discovery, `/proc/bus/input/devices` parsing, device-node creation.
    - `.reference/Pocketbook-Texteditor/src/ui/textView/textView.cpp`: raw `/dev/input/eventN` read loop and `struct input_event` key handling.
    - `.reference/Pocketbook-Texteditor/kbLayouts/en.keys`, `de.keys`: simple Linux key-code to character layout maps.
    - `.reference/Pocketbook-Texteditor/CMakeLists.txt`: PocketBook SDK build setup.
  - Note: Texteditor's key input loop is blocking and must be rewritten for pbterm/libghostty usage.

Additional repo cloned for lookup only:

- `/tmp/ghostty-check/`
  - Fresh clone of `https://github.com/ghostty-org/ghostty.git`.
  - Used only to inspect current `libghostty-vt` build support.
  - Important files:
    - `/tmp/ghostty-check/CMakeLists.txt`: CMake wrapper for libghostty-vt; includes cross-compile helper `ghostty_vt_add_target(...)`.
    - `/tmp/ghostty-check/LICENSE`: Ghostty/libghostty is MIT licensed.
  - This is outside this repo and can be deleted/recloned as needed.

## SDK locations for lookup/build work

Local SDK directory visible from this repo's parent:

- `../SDK/SDK_6.3.0/`

Available SDK subtargets locally:

- `../SDK/SDK_6.3.0/SDK-A13/`
- `../SDK/SDK_6.3.0/SDK-B288/`
- `../SDK/SDK_6.3.0/SDK-iMX6/`

Useful SDK lookup paths:

- InkView header:
  - `../SDK/SDK_6.3.0/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/include/inkview.h`
- PTY header:
  - `../SDK/SDK_6.3.0/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/pty.h`
- `libutil` for `forkpty/openpty`:
  - `../SDK/SDK_6.3.0/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/lib/libutil.a`
  - `../SDK/SDK_6.3.0/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/lib/libutil.so`

Note: this pbterm fork's `CMakeLists.txt` currently points at:

```cmake
SET (TOOLCHAIN_PATH "../SDK/SDK-6.8/SDK-B300")
```

That path was not present in this checkout. The local SDK path uses `SDK_6.3.0`, not `SDK-6.8`.

## Current pbterm behavior and limitations

pbterm is currently command/line-oriented, not a real terminal emulator.

Current output flow:

```text
PTY output -> message::New_Text -> Display::add_text -> Lines::add -> Line::redraw -> DrawString
```

Relevant code:

- `src/Term.cpp`
  - starts shell with PTY via `posix_openpt`, or falls back to pipes.
  - polls shell output with `SetWeakTimer(...)`.
  - sends output as text messages.
- `src/Messenger.cpp`
  - `message::New_Text` calls `m_display->add_text(...)`.
  - `message::New_Command` writes command to shell and also locally displays command text.
- `src/Display.cpp`
  - clears screen, draws current stored lines, calls `SoftUpdate()`.
- `src/Lines.cpp`, `src/Line.cpp`
  - split text at `\n`, wrap by measuring strings, draw via `DrawString`.

Known limitations:

- no VT escape sequence parser.
- `clear`, cursor addressing, alternate screen, colors/styles, curses apps, etc. do not work correctly.
- current on-screen keyboard sends whole commands rather than terminal key events.
- pipe fallback cannot support proper TUI behavior.

## Ghostling/libghostty-vt findings

Ghostling is a minimal Raylib GUI terminal built around `libghostty-vt`.

Important pattern from `.reference/ghostling/main.c`:

1. Create a libghostty terminal:

```c
GhosttyTerminalOptions opts = { .cols = term_cols, .rows = term_rows, .max_scrollback = 1000 };
ghostty_terminal_new(NULL, &terminal, opts);
ghostty_terminal_resize(terminal, term_cols, term_rows, cell_width, cell_height);
```

2. Read bytes from PTY and feed them to the VT parser:

```c
ghostty_terminal_vt_write(terminal, buf, n);
```

3. Register effect callbacks for terminal queries/responses:

- `GHOSTTY_TERMINAL_OPT_WRITE_PTY`
- `GHOSTTY_TERMINAL_OPT_SIZE`
- `GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES`
- `GHOSTTY_TERMINAL_OPT_XTVERSION`
- `GHOSTTY_TERMINAL_OPT_TITLE_CHANGED`
- etc.

These matter because programs like vim/tmux/htop query terminal features.

4. Encode keyboard/mouse events with libghostty encoders:

```c
ghostty_key_encoder_setopt_from_terminal(encoder, terminal);
ghostty_key_encoder_encode(encoder, event, buf, sizeof(buf), &written);
```

5. Update render state and iterate rows/cells:

```c
ghostty_render_state_update(render_state, terminal);
ghostty_render_state_get(render_state, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter);
while (ghostty_render_state_row_iterator_next(row_iter)) {
    ghostty_render_state_row_get(row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells);
    while (ghostty_render_state_row_cells_next(cells)) {
        // read grapheme, fg/bg color, style flags, draw cell
    }
}
```

For PocketBook we should copy this architecture but replace Raylib drawing with InkView drawing.

## Ghostty/libghostty build notes

Ghostling fetches Ghostty from CMake and links `ghostty-vt`.

Current Ghostty checkout `/tmp/ghostty-check/CMakeLists.txt` provides:

- native imported targets:
  - `ghostty-vt`
  - `ghostty-vt-static`
- cross-compilation helper:

```cmake
ghostty_vt_add_target(NAME linux-amd64 ZIG_TARGET x86_64-linux-gnu)
```

For PocketBook, likely starting point:

```cmake
ghostty_vt_add_target(
    NAME pocketbook-arm
    ZIG_TARGET arm-linux-gnueabi
    ZIG_FLAGS -Dsimd=false
)
```

Then link static:

```cmake
target_link_libraries(pbterm.app PRIVATE ghostty-vt-static-pocketbook-arm)
```

Caveats:

- Ghostty/libghostty requires Zig 0.15.x according to Ghostling/Ghostty docs.
- `zig` was not available on PATH in this environment during investigation, so ARM build was not tested.
- PocketBook toolchain uses `arm-obreey-linux-gnueabi`; Zig will likely use a generic target such as `arm-linux-gnueabi`.
- Start with `-Dsimd=false` to reduce CPU/ABI risk.
- Static linking is likely easiest for deployment.

## Pocketbook-Texteditor Bluetooth keyboard findings

Texteditor proves external Bluetooth keyboard input is possible on PocketBook, but requires root/jailbreak.

Important logic in `.reference/Pocketbook-Texteditor/src/handler/eventHandler.cpp`:

- Enables Bluetooth:
  - `SetBluetoothOn()`
  - `BluetoothWakeUp()`
- Scans `/proc/bus/input/devices`.
- Detects Bluetooth keyboard candidates by:
  - `Bus=0005`
  - `Handlers=... kbd eventN ...`
  - keyboard event bit mask `EV=...`
- Reads `/sys.../uevent` for:
  - `MAJOR`
  - `MINOR`
  - `DEVNAME`
- Uses root helper `/mnt/secure/su` to recreate `/dev/input/eventN`:

```cpp
/mnt/secure/su rm /dev/input/eventN
/mnt/secure/su mknod -m 664 /dev/input/eventN c MAJOR MINOR
```

Important logic in `.reference/Pocketbook-Texteditor/src/ui/textView/textView.cpp`:

- Opens `/dev/input/eventN`.
- Reads `struct input_event` from Linux input.
- Handles `EV_KEY` events.
- Tracks shift/altgr and maps Linux key codes to characters through `kbLayouts/*.keys`.

Problems to fix for terminal use:

- Current read loop is blocking:

```cpp
while(inputSession) {
    eventFile.read(data,sizeof(event));
    ...
}
```

- This would block InkView event processing and terminal rendering.
- It should be rewritten as one of:
  - nonblocking file descriptor polled by a timer, or
  - separate reader thread that posts events into the main app.
- Need to map Linux `KEY_*` to `GhosttyKey`, modifiers, and UTF-8 text, then let `ghostty_key_encoder_encode(...)` generate terminal sequences.

## Proposed combined architecture

Recommended architecture:

```text
InkView event loop
   |
   v
Messenger / app controller
   |
   +--> PtySession
   |       - fork/open pty
   |       - run shell
   |       - set TERM=xterm-256color
   |       - set window size with TIOCSWINSZ
   |       - nonblocking read/write
   |
   +--> GhosttyTerminalCore
   |       - ghostty_terminal_new
   |       - ghostty_terminal_vt_write for shell output
   |       - ghostty_key_encoder_encode for keyboard input
   |       - ghostty_render_state_update
   |
   +--> InkViewTerminalRenderer
   |       - iterate ghostty render rows/cells
   |       - draw bg rectangles and glyphs with InkView
   |       - draw cursor
   |       - map colors to grayscale/BW/color
   |       - PartialUpdate / DynamicUpdate
   |
   +--> BluetoothKeyboardInput
           - borrow Texteditor device discovery
           - read evdev events nonblocking
           - map Linux KEY_* to GhosttyKey + UTF-8
           - write encoded sequences to PTY
```

## Implementation strategy

### Phase 1: libghostty terminal core without Bluetooth

Goal: make current pbterm use libghostty-vt for PTY output/rendering.

Tasks:

1. Add libghostty-vt to the build, likely through Ghostty CMake `FetchContent` or local checkout.
2. Create a terminal core object:
   - owns `GhosttyTerminal`
   - owns `GhosttyRenderState`
   - owns row/cell iterators
   - owns key encoder/event later
3. Change `Term` or replace it with `PtySession`:
   - PTY only; no pipe fallback for TUI mode.
   - set `TERM=xterm-256color` in child.
   - set `TIOCSWINSZ` cols/rows.
4. On PTY output:
   - call `ghostty_terminal_vt_write(...)`.
   - request display redraw.
5. Implement basic InkView render loop:
   - fixed monospace font.
   - compute cols/rows from screen and cell dimensions.
   - draw cells.
   - draw cursor.
6. Verify basic commands:
   - shell prompt
   - `clear`
   - cursor movement escape sequences
   - simple curses/TUI app if available.

### Phase 2: resize/orientation/font

Tasks:

1. On orientation change or font-size change:
   - recompute cell width/height.
   - recompute rows/cols.
   - call `ghostty_terminal_resize(...)`.
   - call `ioctl(pty_fd, TIOCSWINSZ, ...)`.
2. Keep pbterm menu/button/orientation features if useful.
3. Add throttled redraw to avoid excessive E Ink updates.

### Phase 3: Bluetooth keyboard

Tasks:

1. Extract/rewrite Texteditor keyboard discovery into reusable class.
2. Add config/menu option to select/start external keyboard.
3. Read `/dev/input/eventN` nonblocking or via a thread.
4. Track modifiers.
5. Map key events to libghostty key events:
   - letters/digits/symbols via keymap + UTF-8 text.
   - Enter, Backspace, Tab, Esc, arrows, Home/End, PageUp/PageDown, Delete, Insert, F-keys as `GhosttyKey` values.
   - Ctrl/Alt modifiers through `GhosttyMods`.
6. Feed encoded bytes to PTY.

### Phase 4: polish

Tasks:

1. Dirty-row/cell rendering, partial updates.
2. Grayscale/color mapping tuning.
3. Scrollback/touch scrolling through libghostty scrollback.
4. Optional mouse/touch reporting.
5. Optional on-screen keyboard fallback converted to key/byte input rather than whole-command mode.

## InkView rendering notes

The SDK InkView API supports basic primitives needed for rendering terminal cells:

From `inkview.h`:

- Colors are `0x00RRGGBB`.
- Common colors:
  - `BLACK 0x000000`
  - `DGRAY 0x555555`
  - `LGRAY 0xaaaaaa`
  - `WHITE 0xffffff`
- Drawing primitives:
  - `DrawPixel`
  - `DrawLine`
  - `DrawRect`
  - `FillArea`
  - `DrawString`
  - `DrawTextRect`
  - `DrawBitmap`
- Update primitives:
  - `FullUpdate`
  - `SoftUpdate`
  - `PartialUpdate`
  - `PartialUpdateBW`
  - `PartialUpdateHQ`
  - `DynamicUpdate`
  - `DynamicUpdateBW`
  - `DynamicUpdateA2`
  - `WaitForUpdateComplete`

Likely first renderer:

- Use `OpenFont("LiberationMono" or configured font, size, ...)`.
- `cell_width = StringWidth("M")` or max of representative glyphs.
- `cell_height = font_size + line_spacing`.
- For each cell:
  - draw bg with `FillArea(x, y, cell_width, cell_height, color)` if needed.
  - draw grapheme with `DrawString`/`DrawTextRect`.
  - fake bold by drawing twice with x+1 if needed.
  - inverse by swapping fg/bg.
- Draw cursor as filled/semi-filled rectangle. InkView has no alpha, so use black/white/inverted block.

Do not implement Kitty graphics first. Keep first target to text TUIs.

## Key risks

1. **E Ink refresh performance**
   - TUI apps can update rapidly.
   - Need throttling and partial updates.
   - Avoid full-screen refresh per PTY read.

2. **Bluetooth keyboard root requirement**
   - Texteditor requires root/jailbreak and `/mnt/secure/su`.
   - This likely remains true.

3. **Input correctness**
   - Basic keys are easy.
   - Ctrl/Alt/AltGr, layouts, F-keys, repeats, Kitty keyboard protocol are harder.

4. **Build/toolchain risk**
   - libghostty-vt needs Zig 0.15.x.
   - ARM/PocketBook target not yet tested.
   - ABI target must match PocketBook's `gnueabi` expectations.

5. **Font/Unicode limitations**
   - libghostty tracks graphemes, but InkView font rendering may not handle all Unicode perfectly.
   - First target should be ASCII/UTF-8 box drawing and common shell/TUI output.

6. **pbterm command-oriented UX**
   - Current pbterm sends whole commands and locally echoes them.
   - Real terminal mode needs byte/key-event-oriented input and no local echo.

## Licensing notes

- pbterm: GPLv3 or later, per source headers and `COPYING`.
- Pocketbook-Texteditor: GPLv3, per `.reference/Pocketbook-Texteditor/LICENSE`.
- Ghostling: MIT, per `.reference/ghostling/LICENSE`.
- Ghostty/libghostty: MIT, per `/tmp/ghostty-check/LICENSE`.

Combining MIT code/library into GPLv3 pbterm should be fine, but distribution should preserve notices and provide GPL source as required.

## Practical next step

The best first coding milestone is **not Bluetooth**. First prove libghostty-vt can build and render on PocketBook:

1. Install Zig 0.15.x on the build machine.
2. Add Ghostty/libghostty-vt as a dependency.
3. Build `libghostty-vt` for `arm-linux-gnueabi` with `-Dsimd=false`.
4. Replace pbterm line rendering with a minimal libghostty render-state renderer using InkView.
5. Test shell prompt, `clear`, and one simple TUI.

After that works, add Bluetooth keyboard input from Texteditor.
