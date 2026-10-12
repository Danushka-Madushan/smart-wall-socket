# ESP32-S Enhanced Smart Wall Socket Firmware

Production firmware for the **ESP32 NodeMCU-32S** in the Smart Wall Socket ecosystem. This device operates as an intelligent, high-precision power socket node featuring **True-RMS AC energy monitoring** isolated on FreeRTOS Core 1, **1-channel inverted relay control** on GPIO 4, **Espressif Unified BLE Provisioning with Proof of Possession (PoP)**, **Multicast DNS (mDNS)** discovery, and a real-time **0.96" 128x64 I2C OLED dashboard**.

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
7. [BLE Wi-Fi Provisioning Workflow (WiFiProv)](#ble-wi-fi-provisioning-workflow-wifiprov)
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

The ESP32 firmware harnesses the dual-core Xtensa LX6 architecture to separate deterministic ADC sampling from asynchronous networking and display rendering:

```
+-----------------------------------------------------------------------------------------+
|                                    ESP32 NodeMCU-32S                                    |
|                                                                                         |
|  [CORE 1: Telemetry Engine]                             [CORE 0: Network & UI Engine]   |
|  - FreeRTOS Task (Priority 2)                           - WiFiProv BLE Provisioning     |
|  - ADS1115 I2C Burst Sampling (100 samples)             - Wi-Fi STA (Auto-Reconnect)    |
|  - Dynamic DC Bias Cancellation                         - WebServer (HTTP Port 80)      |
|  - True-RMS & Active Power Math                         - ESPmDNS (smartsocket.local)   |
|  - 40mA Zero-Load Noise Gate                            - 128x64 SSD1306 OLED Rendering |
+----------------------------+---------------------------------------------+--------------+
                             |                                             |
             +---------------+---------------+             +---------------+---------------+
             |                               |             |                               |
             v                               v             v                               v
     +---------------+               +---------------+ +-------+                       +-------+
     |  ADS1115 ADC  |               |  128x64 OLED  | |GPIO 4 |                       |Radio  |
     |  Addr: 0x48   |               |  Addr: 0x3C   | |Relay  |                       |Wi-Fi/ |
     |  SDA: GPIO 21 |               |  SDA: GPIO 21 | |Active |                       |BLE    |
     |  SCL: GPIO 22 |               |  SCL: GPIO 22 | |LOW    |                       +-------+
     +---+-------+---+               +---------------+ +-------+
         |       |
         v       v
      [A0]       [A1]
     SCT-013   ZMPT101B
     Current   Voltage
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
│   │   └── DisplayManager.cpp         # 128x64 OLED UI: BLE pairing mode, telemetry, modals
│   ├── NetworkManager/
│   │   ├── NetworkManager.h
│   │   └── NetworkManager.cpp         # WiFiProv (BLE + PoP), Wi-Fi STA, mDNS service
│   ├── PowerMonitor/
│   │   ├── PowerMonitor.h
│   │   └── PowerMonitor.cpp           # Core 1 ADS1115 burst sampling & True-RMS calculations
│   ├── RelayController/
│   │   ├── RelayController.h
│   │   └── RelayController.cpp        # Inverted single-relay driver with glitch suppression
│   └── StorageManager/
│       ├── StorageManager.h
│       └── StorageManager.cpp         # ESP32 NVS (Preferences) wear-leveled storage
├── src/
│   ├── Config.cpp                     # Static device constants (DEVICE_ID, POP_SECRET)
│   └── main.cpp                       # Dual-core boot orchestrator and event dispatcher
├── platformio.ini                     # huge_app partition, dependencies, compiler flags
└── how-to.md                          # BLE provisioning reference guide
```

### Module Responsibilities

| Module | Location | Responsibility |
|---|---|---|
| **Config** | `include/Config.h`, `src/Config.cpp` | Single source of truth. Contains `DEVICE_ID` (`PROV_A1B2C3`), `POP_SECRET` (`S3_SEC_9821`), calibration factors (`V_CAL`, `I_CAL`), and shared structs. |
| **PowerMonitor** | `lib/PowerMonitor/` | Manages ADS1115 I2C ADC. Runs `powerSamplingTask` pinned to **Core 1**, computing True-RMS voltage, current, real power, apparent power, and power factor with mutex protection. |
| **RelayController** | `lib/RelayController/` | Manages single relay on `GPIO 4`. Implements **inverted logic** (`LOW = Socket ON`, `HIGH = Socket CUT`) with hardware glitch suppression. |
| **StorageManager** | `lib/StorageManager/` | Uses ESP32 Non-Volatile Storage (`<Preferences.h>`) to store claim state and `master_token` across power cycles with flash wear-leveling. |
| **DisplayManager** | `lib/DisplayManager/` | Manages 128x64 SSD1306 OLED. Displays BLE pairing status when unprovisioned, and live electrical telemetry when operational. Supports modal toasts. |
| **NetworkManager** | `lib/NetworkManager/` | Manages Espressif Unified Provisioning (`WiFiProv`) over BLE with Security 1 and PoP. Releases Bluetooth stack once connected and publishes mDNS `smartsocket.local`. |
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

High-speed AC waveform sampling requires tight timing. On single-core microcontrollers, network interrupts or HTTP parsing can distort RMS integration. The ESP32 solves this by pinning the sampling loop to **Core 1**:

1. **Burst Sampling**: Captures a window of 100 consecutive AC waveform samples across A1 (voltage) and A0 (current) via the ADS1115 16-bit ADC running at 860 samples/sec.
2. **Dynamic DC Bias Cancellation**: Averages the sample array to dynamically compute DC offset (`vOffset`, `iOffset`), preventing DC baseline drift from corrupting AC readings.
3. **True-RMS Math**:
   $$\text{VRMS} = \sqrt{\frac{1}{N} \sum (v_i - v_{\text{offset}})^2} \times V_{\text{CAL}}$$
   $$\text{IRMS} = \sqrt{\frac{1}{N} \sum (i_i - i_{\text{offset}})^2} \times I_{\text{CAL}}$$
   - `V_CAL = 365.0` (calibrated against 230V mains).
   - `I_CAL = 52.0` (calibrated against known 60W load).
4. **Zero-Load Noise Gate**: `CURRENT_NOISE_FLOOR = 0.04` (40mA). When no load is plugged in, readings cleanly snap to `0.00 A` and `0.0 W`.
5. **Real & Apparent Power**: Real active power magnitude is calculated, and apparent power is calculated as $\text{VA} = \text{VRMS} \times \text{IRMS}$. Power factor is derived as $\text{PF} = P / \text{VA}$.
6. **Low-Pass Filter**: A 70/30 exponential moving average filter eliminates high-frequency display jitter while maintaining sub-second response times.
7. **Thread Safety**: Core 1 publishes filtered readings to `sharedData` protected by `dataMutex`.

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
v1;PROV_A1B2C3;S3_SEC_9821;ble
```

* **`v1`**: Protocol payload version.
* **`PROV_A1B2C3`**: Hardware identifier (`DEVICE_ID`).
* **`S3_SEC_9821`**: Secret cryptographic factory key (`POP_SECRET`).
* **`ble`**: Transport medium (`ble` for Bluetooth Low Energy).

> **Efficiency Optimization:** Replacing verbose JSON with semicolon-separated values reduces payload size from 71 characters to **29 characters** (**59.2% reduction**). This fits easily into a **Version 2 QR Code (25×25 matrix)**, cutting symbol density in half and enabling faster, more reliable scanning on small physical stickers.

#### Legacy JSON Fallback (Also supported by mobile app)
```json
{"ver":"v1","name":"PROV_A1B2C3","pop":"S3_SEC_9821","transport":"ble"}
```

#### QR Code Printing Specifications
* **Recommended Dimensions**: 15 mm × 15 mm up to 25 mm × 25 mm.
* **Error Correction Level**: **Level M (15%)** or **Level Q (25%)**.
* **CLI Command to Generate Sticker (using `qrencode`)**:
  ```bash
  qrencode -o smart_socket_esp32_qr.png -s 8 -l M 'v1;PROV_A1B2C3;S3_SEC_9821;ble'
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

## BLE Wi-Fi Provisioning Workflow (WiFiProv)

The ESP32 uses Espressif’s Unified Provisioning Protocol (`WiFiProv`):

1. **Boot Check**: Upon boot, `NetworkManager` queries NVS for saved Wi-Fi credentials.
   - **Provisioned**: Skips BLE, connects directly to saved Wi-Fi, and starts mDNS/Web services.
   - **Unprovisioned**: Launches BLE GATT provisioning service with `WIFI_PROV_SCHEME_BLE`.
2. **Advertising**: Broadcasts as `PROV_A1B2C3`. The 128x64 OLED displays `[ BLE PAIRING ACTIVE ]`, the Device Name, and the PoP PIN.
3. **Mutual Authentication (Security 1)**: The mobile app connects over BLE and initiates Curve25519 ECDH key exchange. An AES-256-CTR session key is derived using `pop = "S3_SEC_9821"`.
4. **Encrypted Wi-Fi Transfer**: The phone sends target Wi-Fi credentials (SSID and Password) encrypted over `prov-config`.
5. **Connection & Teardown**: The ESP32 tests the Wi-Fi connection with the router. Upon acquiring a DHCP IP, it saves credentials to NVS and fires `WIFI_PROV_SCHEME_HANDLER_FREE_BTDM`. The entire Bluetooth controller stack unloads from RAM, reclaiming **~80–100 KB of RAM**.
6. **Operational Transition**: The OLED transitions to the live telemetry dashboard, and mDNS responder starts broadcasting.

---

## Multicast DNS (mDNS) Discovery

The ESP32 advertises itself on the local network as:
- **Hostname**: `smartsocket.local`
- **Service**: `_http._tcp` on Port 80

### Published TXT Records
| Key | Value | Description |
|---|---|---|
| `id` | `PROV_A1B2C3` | Unique hardware identifier (`DEVICE_ID`) |
| `type` | `node` | Identifies this device as a smart socket node |
| `switches` | `1` | Indicates single-relay channel |
| `energy` | `1` | Indicates active True-RMS energy monitoring |
| `transport` | `ble` | Provisioning transport method |
| `claimed` | `0` or `1` | Real-time claim lock status |

---

## OLED Display Interface (128x64)

The 0.96" SSD1306 OLED provides crisp, dual-mode visual feedback:

### Mode 1: BLE Provisioning Mode (Unprovisioned)
```
+------------------------------------------+
|          BLE PAIRING ACTIVE              | (Inverted White Header)
| Device:  PROV_A1B2C3                     |
| PoP PIN: S3_SEC_9821                     |
| Status:  Waiting for app...              |
|------------------------------------------|
| Pwr:ON        Security-1             BLE |
+------------------------------------------+
```

### Mode 2: Operational Telemetry Mode (Connected)
```
+------------------------------------------+
| [CLAIMED]                   [SOCKET ON]  | (Header with Badges)
| IP: 192.168.1.120                        |
| Voltage : 230.1 V                        |
| Current :  0.26 A                        |
| Power   :  59.8 W                        |
|------------------------------------------|
| PF:0.99       VA:60                ESP32 |
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
  "id": "PROV_A1B2C3",
  "type": "node",
  "switches": 1,
  "energy_monitoring": true,
  "claimed": false,
  "transport": "ble",
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
  "id": "PROV_A1B2C3",
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
  "ssid": "HomeWiFi",
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
  "current_A": 0.26,
  "power_W": 59.8,
  "apparent_VA": 60.1,
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
  "id": "PROV_A1B2C3"
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
*(Setting `"clear_wifi": true` wipes Wi-Fi credentials and restarts into BLE provisioning mode)*
- **Response `200 OK`**:
```json
{
  "status": "unclaimed",
  "message": "Device reverted to factory unclaimed state"
}
```

---

## Step-by-Step Usage & Testing Guide (cURL)

Replace `192.168.1.120` with your socket's IP address:

```bash
# 1. Discover device info
curl http://192.168.1.120/api/info

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
