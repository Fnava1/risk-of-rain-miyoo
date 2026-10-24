# Risk of Rain (2013) — Miyoo Mini Plus Native Port

[![Engine](https://img.shields.io/badge/Engine-Butterscotch%20GMS1.4-orange.svg)](https://github.com/ButterscotchRunner/Butterscotch)
[![Clean-Room](https://img.shields.io/badge/Policy-Clean--Room%20BYOD-green.svg)]()

Port of **Risk of Rain (2013)** for the **Miyoo Mini Plus** running OnionOS. Powered by a customized build of the [**Butterscotch**](https://github.com/cobaltgit/Butterscotch) GameMaker: Studio 1.4 runtime.

---

## Handheld Zoom

To make the gaming experience more comfortable, 1.25x and 1.5x zoom modes have been added to the game options; you can adjust them at any time in **Audio and Video --> Zoom Scale**.
This only affects gameplay, the main menu and character selection screen are not affected. Additionally, performance is boosted at higher zoom levels.

## Installation (Bring Your Own Data)

No proprietary copyrighted game assets are included in this repository or release packages. You must provide your own legally purchased copy of Risk of Rain (2013) from Steam or GOG.

### Setup Instructions
1. Download the latest Release from the [Releases](https://github.com/Fnava1/risk-of-rain-miyoo/releases) section.
2. Extract the archive directly to the root of your Miyoo Mini SD card. It will populate:
   ```text
   SDCARD/
   └── Roms/
       └── PORTS/
           ├── Games/
           │   └── Risk of Rain/
           │       ├── RiskOfRain
           │       ├── launch.sh
           │       ├── setup_miyoo
           │       ├── Prefs.ini
           │       └── libs/
           ├── Imgs/
           │   └── Risk of Rain.png
           └── Shortcuts/
               └── Action/
                   └── Risk of Rain.port
   ```
3. Copy your PC game files into `Roms/PORTS/Games/Risk of Rain/`:
   * `data.win`
   * All `.ogg` music and sound files (e.g. `musicStage1.ogg`, `musicBoss1a.ogg`, etc.)
4. Insert the SD card into your Miyoo Mini Plus, boot into OnionOS, go to **Games > Ports**, and launch **Risk of Rain**.
5. On the first launch, `setup_miyoo` will automatically optimize textures, configure default gamepad mappings, and initialize the game.

---

## Default Controls (Miyoo Mini Plus)

| Button | Action |
| :--- | :--- |
| **D-Pad Left / Right** | Move |
| **D-Pad Up / Down** | Climb Ladders / Ropes |
| **A** | Confirm / Interact / Buy Chests |
| **B** | Jump |
| **Y** | Primary Attack (Skill 1) |
| **L1** | Secondary Skill (Skill 2) |
| **R2** | Utility Skill (Skill 3) |
| **R1** | Special Skill (Skill 4) |
| **X** | Use Equipment Item |
| **L2** | Swap Equipment Item |
| **Select** | Hold for Inspect Cursor (Mouse) |
| **Start / Menu** | Pause Menu (Escape) |

---

## Building from Source

### Prerequisites
* Docker (recommended for reproducible cross-compilation)
* Python 3.10+ (for unit test runner and asset tools)
* Git

### Compiling the Engine

1. Clone the repository:
   ```bash
   git clone https://github.com/Fnava1/risk-of-rain-miyoo.git
   cd risk-of-rain-miyoo
   ```

2. Build using the Miyoo cross-compilation Docker container:
   ```bash
   # Build Miyoo ARM binary
   docker run --rm -v "$(pwd):/root/workspace" riskrain-builder:latest bash -c \
       "cd /root/workspace/butterscotch/build_miyoo && make -j4 && \
        /opt/miyoomini-toolchain/usr/bin/arm-linux-gnueabihf-strip -s butterscotch -o /root/workspace/dist/Roms/PORTS/Games/Risk\ of\ Rain/RiskOfRain"
   ```

3. Assemble clean release distribution package:
   ```bash
   python tools/assemble_dist.py
   ```

---

## Running the Unit Test Suite

The project includes an extensive suite of **42 automated regression tests** covering clean-room compliance, zoom aspect ratios, Sharp Bilinear mapping mathematics, room classifications, and renderer fast-paths:

```bash
python -m unittest discover tests
```

---

## Legal & Clean-Room Notice

* This project is an independent open-source port and runtime implementation.
* It is **not** affiliated with, endorsed by, or connected to Hopoo Games or Chucklefish.
* All original Risk of Rain (2013) game assets, code, music, and intellectual property belong to their respective copyright holders.
* Users must supply their own legally acquired game files.

---

## License

This project is licensed under the [MIT License](LICENSE).
Portions based on Butterscotch are licensed under their original project terms.
