# Vault UI preview

Host-side renderer for the vault screens in `src/ui/` (no device needed).

    make            # build, render every scene into build/out (4x PNG, contact sheets, GIFs)
    make SHOTS=1    # also copy the renders to docs/screenshots/vault/

- `stub/gui/` stands in for the firmware's `<gui/canvas.h>` / `<gui/elements.h>`;
  `canvas_stub.c` rasterises into a 128x64 1-bit buffer with u8g2's page layout and
  u8g2's circle/rounded-box algorithms. The firmware's `elements_button_*` and
  scrollbar are close re-drawings, not the firmware code.
- Text uses the firmware's own u8g2 fonts, pulled out of the uFBT SDK's
  `firmware.elf` at build time (`extract_fonts.py`, into `build/fonts.h`, not committed).
  Set `UFBT_HOME` if your SDK is not in `~/.ufbt-ui`.
- `main.c` drives the pure draw functions (`scene_*.c`, `vault_*.c`) with fake data;
  `render.py` (Pillow, via `uv run --with pillow`) writes the images.
- Settings, PIN menu, PIN entry, the account picker and confirmations use the
  firmware's own modules (submenu, text input, dialog) and are not rendered here.
