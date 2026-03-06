## Version 1 of the PCB

This is the Version 1 of the pcb which features: Hardware Integration & Packaging

The PCB serves as the central interface board for a Teensy 4.1 data logger in a motorbike 12V electrical environment, integrating power conversion, sensor conditioning, and plug-in modules in a serviceable package. The board is 110 mm × 90 mm and is intended to be mounted in a 3D-printed ASA enclosure, with internal standoffs and bolts securing the PCB to withstand vibration and impacts.

### System requirements and design goals

The main goals of the hardware integration were to:

- Accept 12V vehicle power (nominal 12V with possible transients/spikes)
- Generate stable 5V and 3.3V rails
- Keep Teensy I/O protected so no signal exceeds 3.3V
- Provide robust inputs for noisy speed/hall signals
- Use connectors for all wiring to simplify assembly, debugging, and service
- Keep the design manufacturable in EasyEDA/JLCPCB

### High-level integration architecture

The PCB is partitioned into four functional blocks to keep switching power noise away from sensitive signals:

- Power entry & protection (12V input, surge suppression)
- Power conversion (12V → 5V buck, 5V → 3.3V LDO)
- Module interfaces (Bluetooth via UART, IMU via I²C, GPS via serial)
- Sensor conditioning (hall/speed inputs and brake pressure analog input)

### Power entry and regulation

Because vehicle 12V rails can carry electrical noise and voltage spikes, the input stage includes a TVS diode for transient clamping and bulk + ceramic capacitors for ripple reduction and short dip ride-through. A TPS54260 buck regulator converts 12V to 5V efficiently to minimize heat inside the enclosure, using the required support parts (Schottky diode, inductor, feedback network, compensation, and input/output capacitors). A low-noise 3.3V rail is then generated from 5V using an AP2112K-3.3 LDO, providing a stable logic/sensor reference.

### Connectors and physical integration

To keep assembly straightforward and reduce wiring mistakes, the PCB uses dedicated connectors for each subsystem:

- Screw terminal for the 12V power input (robust for field wiring)
- JST XH B3B-XH-A(LF)(SN) connectors for external wiring to Hall1, Hall2, and the brake pressure sensor
- Female Dupont headers for the Teensy, IMU, Bluetooth module, and GPS to allow quick plug-in replacement and easy debugging

### Sensor conditioning and interface rationale

Speed/hall lines can be noisy and may be referenced to vehicle wiring (including higher-voltage pullups). Each hall channel therefore uses an opto-isolated input stage (PC817) with a current-limiting resistor, a 3.3V pull-up on the output for clean logic pulses, and a small capacitor for spike filtering. The brake pressure sensor outputs 0–5V, so the analog front end includes a divider to ~0–3.3V, plus a series resistor and RC filtering to reduce noise and protect the ADC input.
