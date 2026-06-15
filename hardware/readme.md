# Hardware — noknok LED Button

Hardware design files for the noknok LED Button module (CH32V003F4U6 — I2C tactile button with RGB LED backlight).

- KiCad project: `kicad/Keyboard_LED_button.*`
- Schematic (PDF): `Schematics_Keyboard_Button_20260408.pdf`
- Board renders: `module-I2C-ledbutton-front.png`, `module-I2C-ledbutton-back.png`

Hardware is licensed CC BY-SA 4.0 (see `../LICENSE-hardware`). Connector, flashing and mounting standards follow the [noknok Ecosystem guidelines](https://github.com/buildwithnoknok/Ecosystem) (electrical + mechanical).

---

## Hardware Change Record

### v2.0 — changes from v1.1
- **Status indicator LED on PD1** (in parallel with the SWIO flashing line — zero extra GPIO). Active-low: `+3V3_PROT → 2.2 kΩ → LED anode; LED cathode → PD1/SWIO`. Driven by the bootloader (off = app running, slow pulse = updating/recovering, solid = error). Parts: red 0603 LED (LCSC C2286) + 2.2 kΩ 0603 (LCSC C4190).
- **Optional custom-button thru-holes** — two thru-hole pads wired in parallel with the on-board tactile switch (button input net / GND), left unpopulated, so a user can solder their own button via wires.
- **Removed the castellated edge pads** — both the I2C edge and the flashing edge (unreliable to contact). I2C is via the JST-SH (Qwiic) connectors.
- **noknok custom footprints** — introduced the noknok M2.5 mounting holes and the noknok flashing pads (M2.5 keyed mounting hole + 3 pogo pads: VCC, SWIO, GND), from the noknok KiCad library in the Ecosystem repo.
- **Removed the on-board I²C pull-up resistors** — pull-ups now live on the host (Conductor), not per module (avoids stacking on the shared bus). The host must provide the bus pull-ups (proposed 3.3 kΩ); a noknok pull-up PCB covers third-party hosts that lack them. See the [I²C Pull-up Resistor Strategy ADR](https://noknokdev.atlassian.net/wiki/spaces/SD/pages/82280449).
