# Hardware — noknok LED Button

Hardware design files for the noknok LED Button module (CH32V003F4U6 — I2C tactile button with RGB LED backlight).

- KiCad project: `kicad/Keyboard_LED_button.*`
- Schematic (PDF): `Schematics_LEDButton_V2_20260616.pdf`
- BOM: `module_I2C-LEDButton.xls`
- Board renders: `module-I2C-ledbutton-front.png`, `module-I2C-ledbutton-back.png` *(pre-V2 renders — pending re-export)*

Hardware is licensed CC BY-SA 4.0 (see `../LICENSE-hardware`). Connector, flashing and mounting standards follow the [noknok Ecosystem guidelines](https://github.com/buildwithnoknok/Ecosystem) (electrical + mechanical).

---

## Hardware Change Record

### v2.0 — changes from v1.1
- **Status indicator LED on PD1** (in parallel with the SWIO flashing line — zero extra GPIO). Active-low: `+3V3_PROT → R5 (2.2 kΩ) → D4 anode; D4 cathode → PD1/SWIO`. Driven by the bootloader (off = app running, slow pulse = updating/recovering, solid = error). Parts: red 0603 LED (LCSC C2286) + 2.2 kΩ 0603 (LCSC C4190).
- **Removed the castellated edge pads** — both the I2C edge and the flashing edge (unreliable to contact). I2C is via the JST-SH (Qwiic) connectors J1/J2 only.
- **New flashing interface (J3)** — noknok flash pads (`noknok_FlashPads_I2C-module_1x3_M2.5`): 3 single-side SMD pogo pads (GND / SWIO / VCC) plus an embedded M2.5 keyed mounting hole, from the noknok KiCad library in the Ecosystem repo.
- **Second mounting hole (H1)** — plain `noknok_MountingHole_2.5mm_M2.5`, diagonally opposite the flash-pad hole.
- **Removed the on-board I²C pull-up resistors** (previously R1/R2) — pull-ups now live on the host (Conductor), not per module (avoids stacking on the shared bus). The host must provide the bus pull-ups (proposed 3.3 kΩ); a noknok pull-up PCB covers third-party hosts that lack them. See the [I²C Pull-up Resistor Strategy ADR](https://noknokdev.atlassian.net/wiki/spaces/SD/pages/82280449).
- **Remaining resistors renumbered** after the pull-ups were removed (values unchanged): VDD series R3→**R1** (10 Ω, no spike issue), LED current R4→**R2** (300 Ω), boost feedback divider R5/R6→**R3**/**R4** (25 kΩ / 10 kΩ). New status-LED resistor is **R5** (2.2 kΩ, see above).
- D1 (SK6812MINI-E RGB LED, payload), D2 (protection Zener) and D3 (boost-converter Schottky) are unchanged from v1.1.

**Gap vs. Knob/Buzzer V2 docs:** the board renders are still the pre-V2 photos, pending new production units.
