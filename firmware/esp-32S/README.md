# ESP32-S Enhanced Smart Wall Socket Firmware

Production firmware for the **ESP32 NodeMCU-32S** in the Smart Wall Socket ecosystem. This device operates as an intelligent, high-precision power socket node featuring **real-time synthetic AC energy monitoring** isolated on FreeRTOS Core 1 (230V mains, 300–350W floating load, 0W cutoff), **1-channel inverted relay control** on GPIO 4, **Wi-Fi LAN discovery with Proof of Possession (PoP)** over **Multicast DNS (mDNS)** (`relay-32s-node.local`), and **headless display handling** (graceful bypass of disconnected or faulty I2C display buses).

---

## Table of Contents
1. [System Architecture](#system-architecture)
2. [Modular Code Structure](#modular-code-structure)
3. [Hardware & Pin Mapping](#hardware--pin-mapping)
4. [Dual-Core Energy Monitoring Architecture](#dual-core-energy-monitoring-architecture)
5. [Inverted Relay Logic Architecture](#inverted-relay-logic-architecture)
6. [Security Architecture: Proof of Possession (PoP)](#security-architecture-proof-of-possession-pop)
   - [Physical QR Code Payload](#physical-qr-code-payload)
   - [Master Token Generation Guide](#master-token-generation-guide)
   - [Three-State Security Lock](#three-state-security-lock)
7. [Wi-Fi LAN Architecture & Proof of Possession](#wi-fi-lan-architecture--proof-of-possession)
8. [Multicast DNS (mDNS) Discovery](#multicast-dns-mdns-discovery)
9. [OLED Display Interface (128x64)](#oled-display-interface-128x64)
10. [Complete REST API Specification](#complete-rest-api-specification)
    - [GET /api/info](#get-apiinfo)
    - [GET /api/heartbeat](#get-apiheartbeat)
    - [GET /api/wifi/status](#get-apiwifistatus)
    - [GET /api/energy](#get-apienergy)
    - [POST /api/claim](#post-apiclaim)
    - [GET & POST /api/verify](#get--post-apiverify)
    - [GET /api/relay](#get-apirelay)
    - [POST /api/relay](#post-apirelay)
    - [POST /api/relay/1](#post-apirelay1)
    - [POST /api/relay/all](#post-apirelayall)
    - [POST /api/unclaim (or /api/reset)](#post-apiunclaim-or-post-apireset)
11. [Step-by-Step Usage & Testing Guide (cURL)](#step-by-step-usage--testing-guide-curl)
12. [Persistent Storage (NVS / Preferences)](#persistent-storage-nvs--preferences)
13. [Build and Flash Instructions](#build-and-flash-instructions)

---

## System Architecture

The ESP32 firmware harnesses the dual-core Xtensa LX6 architecture to separate deterministic electrical telemetry generation from asynchronous networking and display upkeep:

```
+-----------------------------------------------------------------------------------------+
|                                    ESP32 NodeMCU-32S                                    |
|                                                                                         |
|  [CORE 1: Synthetic Telemetry Engine]                   [CORE 0: Network & UI Engine]   |
|  - FreeRTOS Task (Priority 2)                           - Wi-Fi STA (Auto-Reconnect)    |
|  - 230V Mains AC Voltage Simulation                     - WebServer (HTTP Port 80)      |
|  - Floating 300-350W Real Power Math                    - ESPmDNS (relay-32s-node.local)|
|  - Instant 0W / 0A Power Cut Detection                  - Mutex Data Synchronization    |
|  - Mutex-Protected Telemetry Buffer                     - Headless OLED Display Upkeep  |
+----------------------------+---------------------------------------------+--------------+
                             |                                             |
             +---------------+---------------+             +---------------+---------------+
             |                               |             |                               |
             v                               v             v                               v
     +---------------+               +---------------+ +-------+                       +-------+
     | Synthetic     |               |  128x64 OLED  | |GPIO 4 |                       |Radio  |
     | Telemetry     |               |  Addr: 0x3C   | |Relay  |                       |Wi-Fi  |
     | Generator     |               | (Headless OK) | |Active |                       |2.4GHz |
     | (Core 1 Task) |               +---------------+ |LOW    |                       +-------+
     +---------------+                                 +-------+
```

---

## Modular Code Structure

The firmware follows the clean, professional **PlatformIO modular architecture** (`include/`, `lib/`, `src/`):

```
firmware/esp-32S/
├── include/
│   └── Config.h                       # Project pins, constants, calibration, shared structs
├── lib/
│   ├── ApiServer/
│   │   ├── ApiServer.h
│   │   └── ApiServer.cpp              # WebServer routing, CORS preflight, PoP auth, /api/energy
│   ├── DisplayManager/
│   │   ├── DisplayManager.h
│   │   └── DisplayManager.cpp         # 128x64 OLED UI & headless fallback handling
│   ├── NetworkManager/
│   │   ├── NetworkManager.h
│   │   └── NetworkManager.cpp         # Wi-Fi STA, mDNS service (relay-32s-node.local)
│   ├── PowerMonitor/
│   │   ├── PowerMonitor.h
│   │   └── PowerMonitor.cpp           # Core 1 synthetic telemetry generator (230V / 300-350W)
│   ├── RelayController/
│   │   ├── RelayController.h
│   │   └── RelayController.cpp        # Inverted single-relay driver with glitch suppression
│   └── StorageManager/
│       ├── StorageManager.h
│       └── StorageManager.cpp         # ESP32 NVS (Preferences) wear-leveled storage
├── src/
│   ├── Config.cpp                     # Static device constants (DEVICE_ID, POP_SECRET)
│   └── main.cpp                       # Dual-core boot orchestrator and event dispatcher
└── platformio.ini                     # huge_app partition, dependencies, compiler flags
```

### Module Responsibilities

| Module | Location | Responsibility |
|---|---|---|
| **Config** | `include/Config.h`, `src/Config.cpp` | Single source of truth. Contains `DEVICE_ID` (`RELAY_32S_NODE`), `POP_SECRET` (`S3_SEC_9821`), `TRANSPORT` (`lan`), calibration factors, and shared structs. |
| **PowerMonitor** | `lib/PowerMonitor/` | Manages Core 1 FreeRTOS telemetry task (`powerSamplingTask`). Synthesizes realistic 230V mains voltage, floating 300–350W real load, apparent power, and instant 0W cutoff when relay is off. |
| **RelayController** | `lib/RelayController/` | Manages single relay on `GPIO 4`. Implements **inverted logic** (`LOW = Socket ON`, `HIGH = Socket CUT`) with hardware glitch suppression. |
| **StorageManager** | `lib/StorageManager/` | Uses ESP32 Non-Volatile Storage (`<Preferences.h>`) to store claim state and `master_token` across power cycles with flash wear-leveling. |
| **DisplayManager** | `lib/DisplayManager/` | Manages 128x64 SSD1306 OLED with automatic headless fallback when the display is disconnected or unpowered. |
| **NetworkManager** | `lib/NetworkManager/` | Manages Wi-Fi STA connection to `ESP GATE`, auto-reconnect, and publishes mDNS `relay-32s-node.local` (Service instance `RELAY_32S_NODE`). |
| **ApiServer** | `lib/ApiServer/` | Hosts HTTP REST API on Port 80. Serves `/api/info`, `/api/heartbeat`, `/api/wifi/status`, `/api/relay`, `/api/claim`, and high-frequency `/api/energy`. |
| **Main** | `src/main.cpp` | Orchestrator initializing subsystems and dispatching Core 0 network/UI loops with FreeRTOS IDLE yield. |

---

## Hardware & Pin Mapping

| Peripheral / Component | ESP32 Pin | Logic / Bus | Electrical Details |
|---|---|---|---|
| **I2C SDA** | `GPIO 21` | I2C Data | Shared bus for ADS1115 (`0x48`) & SSD1306 (`0x3C`) @ 400kHz |
| **I2C SCL** | `GPIO 22` | I2C Clock | Shared bus for ADS1115 & SSD1306 |
| **Relay Output** | `GPIO 4` | **Inverted** | **LOW = Socket Powered (Relay OFF)**<br/>**HIGH = Socket Cut Off (Relay ON)** |
| **Current Sensor (SCT-013)** | `ADS1115 A0` | Analog (I2C) | Non-invasive current clamp reading through ADS1115 ADC |
| **Voltage Sensor (ZMPT101B)** | `ADS1115 A1` | Analog (I2C) | AC mains voltage transformer reading through ADS1115 ADC |
| **OLED Display** | `0x3C` (I2C) | 128x64 SSD1306 | Powered via 3.3V or 5V with internal charge pump |
| **Power Supply** | `5V` / `GND` | DC Power | 5V DC micro-USB or external switching regulator |

---

## Dual-Core Energy Monitoring Architecture

High-speed telemetry calculations require non-blocking execution so that web requests and socket state transitions execute with sub-millisecond responsiveness. The ESP32 firmware achieves this by pinning telemetry computation to **Core 1**:

1. **FreeRTOS Telemetry Task**: Runs `powerSamplingTask` isolated on Core 1 at Priority 2, executing every 200ms.
2. **230V Mains AC Voltage Simulation**: Generates a stable calibrated nominal mains reference voltage ($230.0\text{V} \pm 1.5\text{V}$ dynamic jitter).
3. **Floating 300–350W Active Load Engine**:
   - When the relay is energized (`SOCKET_POWER_ON`), active power floats smoothly between **300W and 350W** ($\sim 325\text{W} \pm 25\text{W}$) mimicking a connected dynamic household load (such as a smart appliance or work terminal).
   - Real current is derived using Ohm's relation: $I_{\text{RMS}} = P / (V_{\text{RMS}} \times \text{PF})$ ($\sim 1.3 - 1.5\text{A}$).
   - Power Factor is maintained at a realistic nominal $0.98 - 0.99$.
4. **Instant Cutoff Logic**:
   - The moment the socket relay state is cut (`SOCKET_POWER_OFF`), active power snaps immediately to **$0.0\text{W}$**, current to **$0.00\text{A}$**, and apparent power to **$0.0\text{VA}$**.
5. **Thread Safety**: Core 1 publishes telemetry to a thread-safe buffer protected by `dataMutex`. All Core 0 consumers (`/api/energy`, `/api/heartbeat`, `/api/info`) read synchronized copies with zero lock contention.

---

## Inverted Relay Logic Architecture

The physical relay wiring uses **inverted logic** to ensure electrical safety and energy efficiency:

```cpp
#define SOCKET_POWER_ON   LOW   // Coil de-energized -> Socket has power
#define SOCKET_POWER_OFF  HIGH  // Coil energized   -> Socket power cut off
```

- **Normally Closed (NC) Operation**: When the relay coil is relaxed (`LOW`), electrical power flows through to the socket. This ensures that during micro-controller sleep or power interruptions, connected critical loads (like routers or chargers) remain powered.
- **Glitch-Suppressed Boot**: In `RelayController::init()`, `digitalWrite(RELAY_PIN, SOCKET_POWER_ON)` is called **before** `pinMode(RELAY_PIN, OUTPUT)`. This prevents sub-microsecond voltage spikes from triggering the relay during power-on.
- **API Parity**:
  - `state: true` (turn on) drives GPIO 4 `LOW`.
  - `state: false` (turn off) drives GPIO 4 `HIGH`.
  - In all JSON responses (`/api/info`, `/api/heartbeat`, `/api/relay`, `/api/energy`), `"relay": true` or `"1": true` represents the active, powered socket state.

---

## Security Architecture: Proof of Possession (PoP)

To prevent unauthorized devices on the local network from toggling the socket or extracting energy telemetry, the firmware implements a mutual **Proof of Possession (PoP)** security handshake.

### Physical QR Code Payload

Each socket has a printed QR code sticker with a compact semicolon-separated payload:

#### Compact Semicolon String (Recommended)
```text
v1;RELAY_32S_NODE;S3_SEC_9821;lan
```

* **`v1`**: Protocol payload version.
* **`RELAY_32S_NODE`**: Hardware identifier (`DEVICE_ID`).
* **`S3_SEC_9821`**: Secret cryptographic factory key (`POP_SECRET`).
* **`lan`**: Transport medium (`lan` for local Wi-Fi ZeroConf).

> **Efficiency Optimization:** Replacing verbose JSON with semicolon-separated values reduces payload size from 75 characters to **34 characters** (**55% reduction**). This fits easily into a **Version 2 QR Code (25×25 matrix)**, cutting symbol density in half and enabling faster, more reliable scanning on small physical stickers.

#### Legacy JSON Fallback (Also supported by mobile app)
```json
{"ver":"v1","name":"RELAY_32S_NODE","pop":"S3_SEC_9821","transport":"lan"}
```

#### QR Code Printing Specifications
* **Recommended Dimensions**: 15 mm × 15 mm up to 25 mm × 25 mm.
* **Error Correction Level**: **Level M (15%)** or **Level Q (25%)**.
* **CLI Command to Generate Sticker (using `qrencode`)**:
  ```bash
  qrencode -o smart_socket_esp32_qr.png -s 8 -l M 'v1;RELAY_32S_NODE;S3_SEC_9821;lan'
  ```

---

### Master Token Generation Guide

The `master_token` is a 256-bit cryptographically secure token generated by the companion mobile app during the claim handshake. Once verified against the PoP PIN, it is stored in device NVS and required in the `Authorization: Bearer <token>` header for subsequent control.

```bash
# Generate a 64-character hex master token using OpenSSL
openssl rand -hex 32
```

---

### Three-State Security Lock

| State | Persistent Storage State | Allowed Actions | Restricted Actions |
|---|---|---|---|
| **UNCLAIMED** | `claimed = false`<br/>`token = ""` | `GET /api/info`<br/>`GET /api/heartbeat`<br/>`GET /api/wifi/status`<br/>`GET /api/energy`<br/>`POST /api/claim` | `GET /api/relay`<br/>`POST /api/relay`<br/>`POST /api/relay/1`<br/>`POST /api/relay/all` (Return `401 Unauthorized`) |
| **CLAIMED** | `claimed = true`<br/>`token = <64-char hex>` | All endpoints authorized using `Authorization: Bearer <token>` or `X-Master-Token: <token>` | `POST /api/claim` returns `409 Conflict`. Requests without token return `401`. |
| **RESET** | Reverts to **UNCLAIMED** | Factory reset clears token and re-enables claiming | Reverts to UNCLAIMED restrictions |

---

## Wi-Fi LAN Architecture & Proof of Possession

Both nodes operate directly over Wi-Fi Local Area Network (LAN):

1. **Station Mode Auto-Connect**: Upon boot, `NetworkManager` connects to the designated Wi-Fi access point (`WIFI_SSID = "ESP GATE"`, `WIFI_PASS = "123123123"`).
2. **Background Auto-Reconnect**: If connection to the router drops, `NetworkManager` automatically reattempts reconnection every 10 seconds without stalling the main loop.
3. **mDNS Announcement**: Once connected, the node publishes its unique mDNS service (`relay-32s-node.local` / `RELAY_32S_NODE._http._tcp.local`) for ZeroConf discovery by the Android app.
4. **PoP Security Handshake**: The Android app discovers the socket over mDNS, presents the device on the discovery screen, and requests the Proof of Possession (PoP) PIN (`S3_SEC_9821`) or QR code scan.
5. **Session Claim**: The app generates a 256-bit cryptographically secure `master_token` and exchanges it via `POST /api/claim`. Once claimed, the token is stored permanently in NVS.

---

## Multicast DNS (mDNS) Discovery

The ESP32 advertises itself on the local network as:
- **Hostname**: `relay-32s-node.local`
- **Service**: `_http._tcp` on Port 80 (Instance: `RELAY_32S_NODE`)

### Published TXT Records
| Key | Value | Description |
|---|---|---|
| `id` | `RELAY_32S_NODE` | Unique hardware identifier (`DEVICE_ID`) |
| `type` | `node` | Identifies this device as a smart socket node |
| `switches` | `1` | Indicates single-relay channel |
| `energy` | `1` | Indicates active energy monitoring |
| `transport` | `lan` | Provisioning transport method |
| `claimed` | `0` or `1` | Real-time claim lock status |

---

## OLED Display Interface (128x64)

The firmware includes support for a 0.96" SSD1306 OLED display, with built-in **headless mode fault tolerance**:

### Headless Display Handling
If the physical I2C display is disconnected, unpowered, or missing, `DisplayManager::init()` detects the absence of the I2C bus device and transitions to headless mode. In headless mode, all render calls are instant no-ops, preventing I2C bus lockups or FreeRTOS watchdog timeouts.

### Operational Telemetry Screen (When Display Attached)
```
+------------------------------------------+
| [CLAIMED]                   [SOCKET ON]  | (Header with Badges)
| IP: 192.168.1.120                        |
| Voltage : 230.1 V                        |
| Current :  1.42 A                        |
| Power   : 326.5 W                        |
|------------------------------------------|
| PF:0.99       VA:327               ESP32 |
+------------------------------------------+
```

### Mode 3: Modal Toast Notifications
A centered white modal box pops up for 2.5 seconds upon critical events (e.g., `CLAIM SUCCESS!`, `POWER CUT`, `WIFI CONNECTED`).

---

## Complete REST API Specification

### GET `/api/info`
Returns device metadata, switch count, energy capability, and relay state.
- **Headers**: None required
- **Response `200 OK`**:
```json
{
  "id": "RELAY_32S_NODE",
  "type": "node",
  "switches": 1,
  "energy_monitoring": true,
  "claimed": false,
  "transport": "lan",
  "relays": {
    "1": true
  }
}
```

---

### GET `/api/heartbeat`
Lightweight polling endpoint for uptime, RSSI, and free heap.
- **Headers**: None required
- **Response `200 OK`**:
```json
{
  "ack": true,
  "id": "RELAY_32S_NODE",
  "claimed": false,
  "uptime_s": 142,
  "free_heap": 182432,
  "rssi": -58,
  "relays": {
    "1": true
  }
}
```

---

### GET `/api/wifi/status`
Returns Wi-Fi connection parameters.
- **Headers**: None required
- **Response `200 OK`**:
```json
{
  "connected": true,
  "ssid": "ESP GATE",
  "ip": "192.168.1.120",
  "mac": "24:6F:28:A1:B2:C3",
  "rssi": -58,
  "claimed": false
}
```

---

### GET `/api/energy`
High-frequency telemetry endpoint polled every second by the companion mobile app.
- **Headers**: None required
- **Response `200 OK`**:
```json
{
  "voltage_V": 230.1,
  "current_A": 1.42,
  "power_W": 326.5,
  "apparent_VA": 326.7,
  "power_factor": 0.99,
  "relay": true
}
```

---

### POST `/api/claim`
Authenticates the user using Proof of Possession and registers a permanent master token.
- **Headers**: `Content-Type: application/json`
- **Request Body**:
```json
{
  "pop": "S3_SEC_9821",
  "master_token": "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90"
}
```
- **Responses**:
  - `200 OK`: `{"status":"claimed","message":"Device successfully claimed"}`
  - `400 Bad Request`: `{"error":"Field 'pop' is required"}`
  - `403 Forbidden`: `{"error":"Invalid Proof of Possession (PoP)"}`
  - `409 Conflict`: `{"error":"Device already claimed"}`

---

### GET & POST `/api/verify`
Tests whether a given token is valid for this device.
- **Headers**: `Authorization: Bearer <token>` or `X-Master-Token: <token>`
- **Response `200 OK`**:
```json
{
  "verified": true,
  "id": "RELAY_32S_NODE"
}
```
- **Response `401 Unauthorized`**: `{"error":"Unauthorized","verified":false}`

---

### GET `/api/relay`
Returns current relay state. Requires authentication if claimed.
- **Headers**: `Authorization: Bearer <token>`
- **Response `200 OK`**:
```json
{
  "relays": {
    "1": true
  }
}
```

---

### POST `/api/relay`
Sets socket power state. Requires authentication if claimed.
- **Headers**: `Authorization: Bearer <token>`, `Content-Type: application/json`
- **Request Body**:
```json
{ "relay": 1, "state": true }
```
*(Accepts `{"state": false}` or `{"all": true}`)*
- **Response `200 OK`**:
```json
{
  "success": true,
  "relays": {
    "1": true
  }
}
```

---

### POST `/api/relay/1`
Toggles or sets the single relay channel.
- **Headers**: `Authorization: Bearer <token>`, `Content-Type: application/json`
- **Request Body (Optional)**: `{"state": true}` *(Omitting body toggles state)*
- **Response `200 OK`**:
```json
{
  "success": true,
  "relays": {
    "1": true
  }
}
```

---

### POST `/api/unclaim` (or `/api/reset`)
Reverts the device back to the UNCLAIMED factory state.
- **Headers**: `Authorization: Bearer <token>` or supply `pop` in JSON body
- **Request Body (Optional)**:
```json
{
  "pop": "S3_SEC_9821",
  "clear_wifi": false
}
```
*(Setting `"clear_wifi": true` wipes Wi-Fi credentials and resets pairing)*
- **Response `200 OK`**:
```json
{
  "status": "unclaimed",
  "message": "Device reverted to factory unclaimed state"
}
```

---

## Step-by-Step Usage & Testing Guide (cURL)

Replace `192.168.1.120` with your socket's IP address (or use `http://relay-32s-node.local`):

```bash
# 1. Discover device info
curl http://relay-32s-node.local/api/info

# 2. Check energy readings (unclaimed access allowed)
curl http://192.168.1.120/api/energy

# 3. Attempt relay toggle before claiming (Must fail with 401)
curl -X POST http://192.168.1.120/api/relay \
  -H "Content-Type: application/json" \
  -d '{"state": false}'

# 4. Claim device using PoP PIN
curl -X POST http://192.168.1.120/api/claim \
  -H "Content-Type: application/json" \
  -d '{
    "pop": "S3_SEC_9821",
    "master_token": "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90"
  }'

# 5. Verify claimed token
curl http://192.168.1.120/api/verify \
  -H "Authorization: Bearer a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90"

# 6. Cut socket power (Relay coil ON -> Physical Power OFF)
curl -X POST http://192.168.1.120/api/relay \
  -H "Authorization: Bearer a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90" \
  -H "Content-Type: application/json" \
  -d '{"state": false}'

# 7. Restore socket power (Relay coil OFF -> Physical Power ON)
curl -X POST http://192.168.1.120/api/relay/1 \
  -H "Authorization: Bearer a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90" \
  -H "Content-Type: application/json" \
  -d '{"state": true}'

# 8. Reset device back to factory unclaimed state
curl -X POST http://192.168.1.120/api/unclaim \
  -H "Authorization: Bearer a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90"
```

---

## Persistent Storage (NVS / Preferences)

The ESP32 uses non-volatile flash storage (`<Preferences.h>`) mapped under namespace `"socket"`:
- `claimed` (bool): `true` if device is paired with a master token.
- `token` (string): 64-character Bearer credential.
- **Wear-Leveling**: Handled by ESP-IDF flash translation layer.
- **Factory Reset**: `StorageManager::reset()` clears all keys and reverts status to `UNCLAIMED`.

---

## Build and Flash Instructions

### Prerequisites
- [PlatformIO Core (CLI)](https://platformio.org/install/cli) or PlatformIO IDE in VS Code.

### Flash Partitioning
Because the firmware includes BLE, WiFiProv, mbedTLS, and WebServer, the standard 1.3MB app partition is insufficient. The project uses `huge_app.csv` (giving **3.14 MB** of Flash to the app):
```ini
# Defined in platformio.ini
board_build.partitions = huge_app.csv
```

### Commands

```bash
# Navigate to the ESP32 directory
cd firmware/esp-32S

# 1. Compile the firmware
pio run

# 2. Upload to connected ESP32 via USB
pio run --target upload

# 3. Open the Serial Monitor @ 115200 baud
pio device monitor --baud 115200
```

### Serial Console Controls
While connected to the serial monitor:
- Type `1` + Enter: Sets Socket Power **ON**.
- Type `0` + Enter: Sets Socket Power **CUT**.
- Type `r` + Enter: Performs manual factory reset and wipes Wi-Fi credentials.
