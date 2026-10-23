# BLDC Half-Bridge (Low-Side) Driver

Open-loop drive for a **4-wire BLDC motor (star winding with accessible center tap)** using **3 low-side N-channel MOSFETs**.
3 PWM outputs + a timer + a UART.

## How it works

- The motor center tap is tied to **+V**. Each phase wire goes to the drain of one low-side MOSFET.
- Only one phase is switched on at a time (3-step, 120 deg electrical, unipolar). Current flows from +V, through one coil, to GND.
- The active phase is PWM'd (duty = torque/speed), and the active phase rotates at `ELEC_FREQ_HZ`.
- No sensors, no feedback: the rotor is simply dragged along. Too fast or too heavy a load = it loses sync (stalls or buzzes).
- No high-side switch exists, so there is no shoot-through and no dead-time is required.
- The MOSFET driver stage is your existing discrete design (3 inputs, non-inverting by default). It is shown here as a black box.

## Bill of materials (all through-hole)

| Ref      | Value / Model                         | Note                                              |
|----------|---------------------------------------|---------------------------------------------------|
| U1       | ESP32-DevKitC (ESP32-WROOM-32)        | Any ESP32 dev board with pin headers              |
| DRV      | Existing discrete MOSFET driver       | 3x N-ch low-side, e.g. IRLZ44N (TO-220)           |
| D1-D3    | 1N5822                                | Flyback, 40 V / 3 A, DO-201. Skip if DRV has them |
| C1       | 470 uF / 25 V electrolytic            | Radial, bulk cap near the driver                  |
| C2       | 100 nF / 50 V ceramic                 | Radial/disc, parallel with C1                     |
| R1-R3    | 10 kOhm, 1/4 W                        | Input pull-downs. Skip if DRV already has them    |
| M1       | BLDC motor, 4 wires (A, B, C, center) | Star winding, center tap accessible               |
| PSU      | 12 V DC, >= 3 A                       | Default design: 12 V, max 2 A per phase           |

## Circuit

```
 +12V ●=====+===========+===========+===========+===========+====> M1 CENTER (wire 4)
            |           |           |           |
          C1 (+)        |        D1 (K)      D2 (K)      D3 (K)
        470uF/25V       |        1N5822      1N5822      1N5822
            |         C2 100nF     |           |           |
            |           |          |  (A)      |  (B)      |  (C)
            |           |          +------+----|------+----|------+----> to M1 wire A / B / C
            |           |                 |           |           |
            |           |        +--------+-----------+-----------+--------+
            |           |        |  OUT_A      OUT_B       OUT_C           |
            |           |        |                                         |
            |           |        |   DISCRETE MOSFET DRIVER  (DRV)         |
            |           |        |   existing design, not drawn            |
            |           |        |                                         |
            |           |        |  IN_A       IN_B        IN_C       GND  |
            |           |        +----+----------+-----------+---------+---+
            |           |             |          |           |         |
            |           |            R1 10k     R2 10k      R3 10k      |
            |           |             |          |           |         |
            |           |             +-- GND    +-- GND     +-- GND   |
            |           |             |          |           |         |
            |           |        +----+----------+-----------+---+     |
            |           |        | GPIO25     GPIO26      GPIO27  |     |
            |           |        |                                |     |
            |           |        |       U1  ESP32-DevKitC        |     |
            |           |        |                           GND  |     |
            |           |        +--------------------------+-----+     |
            |           |                                   |           |
 GND  ●=====+===========+===================================+===========+
```

Notes:
- **Common GND is mandatory** between PSU, DRV and ESP32. It is the only "feedback-like" connection needed.
- D1-D3: cathode to +12V, anode to the phase node (MOSFET drain). They clamp the inductive kick when a phase turns off.
- R1-R3 keep the gates off while the ESP32 boots (GPIO floating). GPIO25/26/27 have no boot-strapping role.
- ESP32 is powered separately (USB or its own 5 V). Do not power it from the motor rail unless regulated.

## Files

- `README.md` this file
- `firmware.ino` Arduino sketch (Arduino-ESP32 core 2.x and 3.x, LEDC PWM, no libraries)

## Adjustable variables

Edit at the top of `firmware.ino` (defaults), or change live over Serial (115200 baud).

| Variable              | Default | Serial | Meaning                                              |
|-----------------------|---------|--------|------------------------------------------------------|
| `PWM_FREQ_HZ`         | 20000   | `p<Hz>`| PWM carrier, 1000..40000                             |
| `DUTY_PERCENT`        | 40      | `d<%>` | Target duty of the active phase, 0..100              |
| `ELEC_FREQ_HZ`        | 20      | `f<Hz>`| Target electrical frequency (full 3-step cycles/s)   |
| `RAMP_TIME_MS`        | 2000    | `r<ms>`| Soft-start time from start values to target          |
| `START_FREQ_HZ`       | 3       | -      | Frequency at the beginning of the ramp               |
| `START_DUTY_PERCENT`  | 20      | -      | Duty at the beginning of the ramp                    |
| `DIRECTION`           | 0       | `x0/x1`| 0 = forward, 1 = reverse                             |
| `ENABLE_ON_BOOT`      | false   | `e0/e1`| Motor stays off after reset until `e1`               |
| `GATE_ACTIVE_HIGH`    | true    | -      | Set false if your driver inverts the gate signal     |
| -                     | -       | `?`    | Print current status                                 |

Mechanical speed (RPM) = `ELEC_FREQ_HZ x 60 / pole_pairs`.
Commands end with newline, e.g. `d50` then Enter.

## Porting to another MCU

Replace only these in the sketch: `pwmAttach()`, `pwmSetFreq()`, `pwmWrite()` (3 PWM channels) and the `micros()/millis()` timing. The commutation logic is plain C.

## First run checklist

1. Power the motor rail from a **current-limited bench supply** (start at 6 V, 0.5 A limit).
2. Flash the sketch, open Serial at 115200, keep the motor unloaded.
3. Send `d25`, `f5`, `e1`. Raise `f` slowly; raise `d` if it skips steps.
4. If it vibrates instead of turning, lower `f` or raise `d`. If the direction is wrong, send `x1`.
5. Check MOSFET and motor temperature before running longer.

## Limitations

- Unipolar drive: lower torque and efficiency than a 6-step bipolar bridge, and the motor must have a center tap.
- Open loop: no stall detection, no current limit. Use a current-limited supply.
- Supply must stay within the MOSFET Vds rating (flyback diodes clamp spikes to about +V).
