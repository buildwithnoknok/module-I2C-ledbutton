# noknok LED Button

A compact I²C‑controlled button module with integrated RGB LED backlight for the noknok ecosystem.  
Designed for user input, menu navigation, and illuminated UI feedback in modular builds.

![LED Button Module Front](hardware/module-I2C-keyboardbutton-front.png)
![LED Button Module Back](hardware/module-I2C-keyboardbutton-back.png)

---

## Overview

The **noknok LED Button** uses a **CH32V003F4U6** microcontroller to read a tactile push button and drive an **SK6812MINI-E** RGB LED via SPI+DMA. It connects to the Raspberry Pi Pico (Conductor) over the standard noknok **JST SH 4‑pin I²C connector**.

The I²C address is assigned dynamically at boot — no conflicts when using multiple modules on the same bus.

Typical use cases:
- Menu selection (OK / Back / Cancel)
- Illuminated status indicators
- Multi‑button control panels
- Confirm actions with colour feedback

---

## Features

- Dynamic I²C address via noknok enumeration protocol (no hardcoded address)
- CH32V003F4U6 microcontroller (RISC‑V, 48 MHz, QFN-20)
- SK6812MINI-E RGB LED (SPI + DMA driven — CPU-free updates)
- Tactile push button with 20 ms hardware debounce
- Press edge, release edge, and cumulative press count — no polling gaps
- 3.3V operation via noknok I²C connector
- Compact 20×20 mm PCB

---

## I²C Protocol

**I²C address:** assigned dynamically at boot — see [Enumeration](#enumeration).

### Commands (Pico → Module)

| Bytes | Command | Description |
|-------|---------|-------------|
| `0x00` | **LED OFF** | Turn LED off immediately |
| `0x10` `R` `G` `B` `0x00` | **SET COLOR** | Set LED colour (R, G, B each 0–255) |
| `0x11` | **RESET COUNT** | Reset cumulative press counter to 0 |

### Status read (Pico ← Module)

Read **2 bytes**:

**Byte 0 — status flags:**

| Bit | Meaning |
|-----|---------|
| 0 | Button currently pressed (`1` = held down) |
| 1 | Press event since last read (edge, auto-clears) |
| 2 | Release event since last read (edge, auto-clears) |

**Byte 1 — cumulative press count** (0–255, wraps)

> Edge flags (bits 1 and 2) are cleared on the module after each read. Poll at ~50 ms and you won't miss any press or release.

---

## Enumeration

The module uses the standard noknok dynamic enumeration protocol — no hardcoded I²C address. At boot it flashes the LED white briefly, then counts a UID-derived backoff delay before joining the bus at `0x7F` for address assignment. On successful assignment, the LED flashes green.

**→ Full protocol spec:** [Ecosystem / software / enumeration.md](https://github.com/buildwithnoknok/Ecosystem/blob/main/software/enumeration.md)

---

## Python API

Use the `noknok.py` library from the [Ecosystem repo](https://github.com/buildwithnoknok/Ecosystem/tree/main/software/pico) on the Pico.

```python
from noknok import Conductor

c = Conductor()        # GP8 = SDA, GP9 = SCL
c.enumerate()          # discover all modules (~3 s)

kb = c.ledbutton[0]             # first LED Button by discovery order
# or: kb = c.role["ok_button"]  # by role name after setup_roles()

# LED control
kb.set_color(255, 0, 0)         # red
kb.set_color(0, 255, 0)         # green
kb.set_color(255, 255, 255)     # white
kb.led_off()

# Button reading — returns LedButtonStatus or None on I2C error
s = kb.read()
if s is not None:
    print(s.pressed)        # True if held right now
    print(s.press_event)    # True if pressed since last read
    print(s.release_event)  # True if released since last read
    print(s.count)          # cumulative press count (0–255)

# Reset counter
kb.reset_count()
```

---

## Hardware

| Spec | Value |
|------|-------|
| PCB size | 20 × 20 mm |
| MCU | CH32V003F4U6 (QFN‑20, RISC‑V, 48 MHz) |
| LED | SK6812MINI-E (RGB, SPI-driven) |
| Button | Tactile switch, active LOW, internal pull-up |
| Connector | JST SH 4‑pin (Qwiic / Stemma QT compatible) |
| Supply voltage | 3.3V via I²C connector |
| LED data pin | PC6 (SPI1 MOSI, DMA-driven) |
| Button pin | PD4 (active LOW) |
| I²C SDA | PC1 |
| I²C SCL | PC2 |
| Flash header | J4 — 5‑pin (GND, SWIO, RST, VCC) |

---

## Firmware

Source is in `firmware/src/`. To build and flash, clone this repo alongside your ch32fun installation and run:

```bash
cd firmware/src
make        # compile
make flash  # compile + flash via WCH Link-E
```

> ch32fun must be installed at `../ch32fun/` relative to `firmware/src/` — see [cnlohr/ch32v003fun](https://github.com/cnlohr/ch32v003fun) for setup instructions.

| Metric | Value |
|--------|-------|
| Firmware version | v1.0 |

---

## Status

| Area | Status |
|------|--------|
| Hardware | v1.0 |
| Firmware | **v1.0 — complete** |
| Python library | **complete** (in [Ecosystem repo](https://github.com/buildwithnoknok/Ecosystem/tree/main/software/pico)) |
| Documentation | **complete** |

---

## License

TBD — to be added when the repository becomes public.