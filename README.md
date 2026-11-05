# FCEUmm Interface Edition — Core Integration Build

This package combines the tested FCEUmm Interface Edition frontend with the
community FCEUmm-PS2 0.3.3 emulator core. FCEUmm is the **only emulation core**.

## Important status

This is the first integration source package. The bridge and build configuration
have been prepared and statically checked, but the full PS2DEV build and real
console test are still required. Do not treat this package as a verified final
ELF until the GitHub Actions build succeeds and the resulting ELF is tested on
the PlayStation 2.

The previous `FCEUmm-Interface-Only-PS2.ELF` is retained in `BACKUP/` as a
known-good interface-only fallback. Do not overwrite or delete it.

## USB structure — unchanged

Copy the `FCEUmm` folder from `USB-STRUCTURE` to the root of the USB drive:

```text
PENDRIVE/
└── FCEUmm/
    ├── nes roms/
    ├── images/
    ├── cheats/
    └── STATES/
```

The `nes roms` directory name is lowercase exactly as shown. No extra folders
are required on the USB drive.

## What is integrated in this source package

- The previously tested frontend, menu navigation and embedded NES/Famicom background.
- The existing USB/exFAT module loading sequence and ROM browser.
- The existing 200x200 PNG cover reader.
- FCEUmm-PS2 0.3.3 core sources.
- A bridge from the frontend to `FCEUI_Initialize`, `FCEUI_LoadGame`,
  `FCEUI_Emulate`, `FCEUI_CloseGame`, save-state and cheat APIs.
- GSKit rendering of the core's 256x240 indexed NES frame.
- Mono audsrv output at 22050 Hz, matching the FCEUmm-PS2 0.3.3 release setting.
- DualShock mapping: Cross=A, Square=B, Select, Start and D-pad.
- Existing turbo buttons: Circle=Turbo A, Triangle=Turbo B.
- Five real FCEUmm save-state slots under `FCEUmm/STATES`.
- Frontend CHT parsing with Game Genie and 8-digit PAR code decoding. Unsupported
  CHT code formats are not silently applied.

## ROM formats shown by the browser

The browser lists `.nes`, `.zip`, `.unf`, `.unif`, `.nsf` and `.nez` files.
The FCEUmm core handles ZIP member selection. FDS `.fds` images are not listed
in this first integration build because the agreed USB layout does not include
the required `disksys.rom` BIOS file.

## What remains to be verified

1. GitHub Actions compilation with the PS2DEV toolchain.
2. Successful launch of the integrated ELF on the real PS2.
3. Loading one `.nes` ROM from `mass:/FCEUmm/nes roms/`.
4. Correct GSKit picture and audsrv sound.
5. DualShock input and return-to-browser behavior.
6. ZIP loading from the USB drive.
7. Real Save State save/load.
8. Game Genie/PAR cheat application.

The interface-only ELF remains the fallback if any integration test fails.

## Build

The workflow is provided separately as `FCEUmm-Core-Integration.yml`.
Create it in the GitHub repository at:

```text
.github/workflows/FCEUmm-Core-Integration.yml
```

Then run **FCEUmm Core Integration Build** from GitHub Actions. The workflow
builds inside the official PS2DEV container and uploads
`FCEUmm-NES-PS2.ELF` when compilation succeeds.

## Source and license

The FCEUmm-PS2 0.3.3 source is included under `src/core/`. Its upstream
`Copying`, `Authors`, README, patch and changelog are retained under
`LICENSES/`. The FCEUmm core is GPL-2.0-or-later; preserve its copyright and
license notices in redistributed source builds.

No commercial ROMs or game BIOS files are bundled.
