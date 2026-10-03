# Power Meter Integration — Implementation Proposal

**Project:** IES-WI-C6A + Schneider PM2200 power meter
**For:** BossFarm — review and approval
**Prepared by:** Alessandro Frondini

---

## 1. Goal

Let the IES-WI-C6A device read a **Schneider PM2200 power meter** and show the electricity consumption of a site on the BossFarm online dashboard, with **voltage and current on all three lines, power, and total energy consumed in Wh**.

## 2. How it will work

```
 PM2200 power meter  ──RS485──▶  IES-WI-C6A device  ──WiFi──▶  Raspberry Pi  ──▶  Online dashboard
 (measures 3 lines)              (new power-meter firmware)     (existing server)   (existing)
```

- The PM2200 measures the electrical supply and exposes its values over a standard industrial RS485 connection.
- The IES-WI-C6A already has the RS485 hardware on board. It will read the meter every few seconds and send the values over WiFi, exactly like it does today for the other sensors.
- The data reaches the same Raspberry Pi server and the same dashboard already in use. No new infrastructure is needed.

## 3. What will be delivered

| # | Deliverable | What it means for BossFarm |
|---|---|---|
| 1 | **Power-meter firmware** for the IES-WI-C6A | A dedicated version of the device software that reads the PM2200 and sends its data. Setup (WiFi, server) works the same way as today's devices. |
| 2 | **Readings reported** | Voltage and current on each of the 3 lines, power per line and in total, power factor, frequency, and **cumulative energy in Wh**, taken directly from the meter's own energy counter. |
| 3 | **Dedicated flasher tool** | A separate, clearly branded tool (distinct colour and name) used to load the power-meter firmware onto a device, so it cannot be confused with the existing sensor tool. |
| 4 | **Meter simulator** | A software stand-in for the PM2200 so everything can be built and tested before the physical meter is available. |
| 5 | **Dashboard integration** | The power data stored and made available to the dashboard alongside the existing sensor data. |
| 6 | **Commissioning checklist** | A short step-by-step guide for connecting a real meter and verifying the readings. |

## 4. Work order

Each step ends with something visible that can be reviewed before the next one starts.

| Step | What happens | What you will see |
|---|---|---|
| 1 | **Specification** — fix the exact list of measurements and the data format | A short reference document |
| 2 | **Simulated meter** — a program that behaves like a PM2200 connected to a device | Realistic meter readings printed live on screen |
| 3 | **Simulated dashboard page** — a simple page showing those readings live | Voltage, current, power and a running Wh counter updating in a browser |
| 4 | **Device firmware** — the device runs the new software, initially using simulated meter data | A real device sending power-meter data to the server, with no meter attached |
| 5 | **Server and dashboard integration** — store the data on the Raspberry Pi and prepare it for the dashboard | Power-meter data visible next to the existing sensors |
| 6 | **Flasher tool** — dedicated tool to install the firmware | A ready-to-use installer for the team |
| 7 | **Real-meter commissioning** — connect a PM2200 and verify | Device readings matching the meter's own display |

Steps 1 to 6 do not need the physical meter. **Step 7 is the only step that does.**

## 5. What we need from BossFarm

1. **Dashboard display:** Approval of the idea to show the power meter **on each individual device card** (for example with a button that opens the consumption view), since meters will be connected to different devices. The exact look can be decided together once the data is flowing.
2. **Meter model:** Which PM2200 variant will be used (PM2210, PM2220 or PM2230). The PM2230 additionally offers harmonic analysis if that is ever of interest.
3. **Meter settings:** The communication settings configured on the meter for the deployment (address, speed, parity), or permission to set them at installation.
4. **Test meter:** Access to one real PM2200 for the final verification in step 7.

## 6. Assumptions and risks

- **Meter data layout:** The detailed list of the meter's internal data addresses is published separately by Schneider. It will be checked against Schneider's official documentation before connecting a real meter. Until a real meter is tested, the exact values are verified against the simulator only.
- **Real-world check:** Wiring, communication settings and reading accuracy can only be fully confirmed with the physical meter (step 7).
- **Scope of the first version:** Voltage, current, power, power factor, frequency and Wh energy. Additional measurements (for example reactive power, harmonics, or energy exported back to the grid) can be added later without changing what is already delivered.
- **Existing devices are unaffected:** The current sensor firmware and flasher stay as they are. The power-meter version is separate.

## 7. Approval

Please confirm:

- [ ] The scope and work order above are approved
- [ ] The per-device dashboard approach (section 5, item 1) is acceptable
- [ ] The PM2200 model to use: ______________________
