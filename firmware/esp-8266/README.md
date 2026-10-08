# ESP8266 2-Channel Wireless Smart Switch Firmware

Production firmware for the **ESP8266 NodeMCU (ESP-12E)** smart relay node in the Smart Wall Socket ecosystem. This device operates as an authenticated wireless smart switch controlling two independent electrical channels, secured by a **Proof of Possession (PoP)** pairing handshake, advertised over **Multicast DNS (mDNS)** with dynamic TXT records, and displayed in real time on a **0.91" 128x32 I2C OLED display**.

---

## Table of Contents
1. [System Architecture](#system-architecture)
2. [Modular Code Structure](#modular-code-structure)
3. [Hardware & Pin Mapping](#hardware--pin-mapping)
4. [Security Architecture: Proof of Possession (PoP)](#security-architecture-proof-of-possession-pop)
   - [Physical QR Code Payload](#physical-qr-code-payload)
   - [Master Token Generation Guide](#master-token-generation-guide)
   - [Three-State Security Lock](#three-state-security-lock)
5. [Multicast DNS (mDNS) Discovery](#multicast-dns-mdns-discovery)
6. [OLED Display Interface (128x32)](#oled-display-interface-128x32)
7. [Complete REST API Specification](#complete-rest-api-specification)
   - [GET /api/info](#get-apiinfo)
   - [GET /api/heartbeat](#get-apiheartbeat)
   - [GET /api/wifi/status](#get-apiwifistatus)
   - [POST /api/claim](#post-apiclaim)
   - [GET & POST /api/verify](#get--post-apiverify)
   - [GET /api/relay](#get-apirelay)
   - [POST /api/relay](#post-apirelay)
   - [POST /api/relay/1](#post-apirelay1)
   - [POST /api/relay/2](#post-apirelay2)
   - [POST /api/relay/all](#post-apirelayall)
   - [POST /api/unclaim](#post-apiunclaim-or-post-apireset)
8. [Step-by-Step Usage & Testing Guide (cURL)](#step-by-step-usage--testing-guide-curl)
9. [Persistent Storage (EEPROM)](#persistent-storage-eeprom)
10. [Build and Flash Instructions](#build-and-flash-instructions)

---

## System Architecture

```
                       +---------------------------------------+
                       |           ESP8266 NodeMCU             |
                       |                                       |
                       |  [Wi-Fi Client: DHCP]                 |
                       |  SSID: ESP GATE | MAC: DE:AD:BE:EF:01:01 |
                       +-------------------+-------------------+
                                           |
        +----------------------------------+----------------------------------+
        |                                  |                                  |
        v                                  v                                  v
+---------------+                  +---------------+                  +---------------+
|  I2C (D1/D2)  |                  | GPIO14 (D5)   |                  | GPIO12 (D6)   |
| 128x32 OLED   |                  | 1k -> C945 NPN|                  | 1k -> C945 NPN|
| SSD1306       |                  | Relay 1 (SW1) |                  | Relay 2 (SW2) |
+---------------+                  +---------------+                  +---------------+
```

---

## Modular Code Structure

The firmware is organized into single-responsibility C++ modules inside `firmware/esp-8266/src/`:

```
firmware/esp-8266/src/
├── Config.h & Config.cpp              # Hardware pins, credentials, constants, and unified DEVICE_ID
├── Storage.h & Storage.cpp            # EEPROM manager with rotate-and-XOR checksum integrity
├── RelayController.h & .cpp           # Glitch-suppressed GPIO12/14 relay driver and state tracker
├── DisplayManager.h & .cpp            # 0.91" 128x32 OLED renderer, layout, toasts, and headless fallback
├── NetworkManager.h & .cpp            # Wi-Fi client auto-reconnect and dynamic DNS-SD TXT publisher
├── ApiServer.h & .cpp                 # ESP8266WebServer routing, CORS preflight, PoP auth, and endpoints
└── main.cpp                           # Boot orchestrator and continuous event loop dispatcher
```

| Module | Source Files | Responsibility |
|---|---|---|
| **Config** | `Config.h`, `Config.cpp` | Single source of truth for device parameters. Modifying `DEVICE_ID` here automatically updates hostname, mDNS, splash screen, and API responses. |
| **Storage** | `Storage.h`, `Storage.cpp` | Manages non-volatile EEPROM (512B), validates data against checksum corruption, persists `master_token`, and handles factory reset. |
| **RelayController** | `RelayController.h`, `RelayController.cpp` | Controls physical relays on `D5` (GPIO14) and `D6` (GPIO12). Suppresses floating boot glitches (`digitalWrite(LOW)` before `pinMode(OUTPUT)`). |
| **DisplayManager** | `DisplayManager.h`, `DisplayManager.cpp` | Manages 128x32 OLED UI, dynamic splash centering, 1500µs clock-stretch limit protection, and headless mode fallback. |
| **NetworkManager** | `NetworkManager.h`, `NetworkManager.cpp` | Connects to `ESP GATE`, runs background auto-reconnect, and publishes `smartsocket.local` with dynamic TXT records. |
| **ApiServer** | `ApiServer.h`, `ApiServer.cpp` | Handles 11 REST API routes, universal CORS headers (`OPTIONS` -> `204`), 1KB payload gate, and PoP Bearer token verification. |
| **Main** | `main.cpp` | High-level orchestrator initializing all subsystems in order and dispatching events in `loop()`. |

---

## Hardware & Pin Mapping

| Peripheral / Component | NodeMCU Pin | ESP8266 GPIO | Electrical Details |
|---|---|---|---|
| **Relay 1 (Switch 1)** | `D5` | `GPIO14` | Active **HIGH** via C945 NPN transistor base with 1k resistor |
| **Relay 2 (Switch 2)** | `D6` | `GPIO12` | Active **HIGH** via C945 NPN transistor base with 1k resistor |
| **OLED SDA** | `D2` | `GPIO4` | I2C Data line for 0.91" SSD1306 (address `0x3C`) |
| **OLED SCL** | `D1` | `GPIO5` | I2C Clock line for 0.91" SSD1306 (address `0x3C`) |
| **Power Supply** | `Vin` / `GND` | - | 5V DC input for relay coils and ESP8266 voltage regulator |

> **Safety Default**: All relays initialize to `LOW` (`OFF`) on boot to prevent unexpected power spikes or device activation during brownouts.

---

## Security Architecture: Proof of Possession (PoP)

To prevent rogue devices on the same local network from taking control of unclaimed or paired sockets, the firmware uses a **Proof of Possession (PoP)** pairing model.

### Physical QR Code Payload
Each physical hardware unit has a unique QR code sticker printed with the following JSON schema:

#### Formatted JSON
```json
{
  "type": "node",
  "id": "RELAY_8266_NODE",
  "pop": "NODE_SEC_4102",
  "transport": "lan"
}
```

#### Exact Raw String to Print on QR Code Sticker
Use the minified string below when generating your QR code. Minified JSON creates a lower-density, faster-to-scan QR code:
```text
{"type":"node","id":"RELAY_8266_NODE","pop":"NODE_SEC_4102","transport":"lan"}
```

* **`id`**: Hardware identifier of this device (`RELAY_8266_NODE`).
* **`pop`**: Secret cryptographic factory key stored in device flash (`NODE_SEC_4102`).
* **`transport`**: Network medium (`lan` for Wi-Fi multicast; future ESP32 variants use `ble`).

#### QR Code Printing Specifications
* **Recommended Dimensions**: 15 mm × 15 mm up to 25 mm × 25 mm.
* **Error Correction Level**: **Level M (15%)** or **Level Q (25%)** (ensures readability even with minor scratches on the sticker).
* **CLI Command to Generate Sticker (using `qrencode`)**:
  ```bash
  qrencode -o smart_switch_qr.png -s 8 -l M '{"type":"node","id":"RELAY_8266_NODE","pop":"NODE_SEC_4102","transport":"lan"}'
  ```

---

### Master Token Generation Guide

The `master_token` is an application-level secret token created by your mobile app or cloud backend when claiming the socket. Once saved during the PoP handshake, this token acts as the pre-shared Bearer credential required to authenticate all future relay commands.

#### Token Requirements & Constraints
* **Length**: 16 to 64 characters (firmware buffer supports up to **64 ASCII characters** + null terminator).
* **Character Set**: Alphanumeric characters (`[a-zA-Z0-9]`), hex, or URL-safe base64.
* **Entropy**: Use a cryptographically secure pseudo-random number generator (CSPRNG) producing 256 bits (32 bytes) of entropy represented as a 64-character hexadecimal string.

#### Code Snippets to Generate a Valid `master_token`

##### 1. Android / Kotlin (Recommended for `client/` app)
```kotlin
import java.security.SecureRandom

fun generateMasterToken(): String {
    val randomBytes = ByteArray(32) // 256 bits of entropy
    SecureRandom().nextBytes(randomBytes)
    return randomBytes.joinToString("") { "%02x".format(it) }
}
// Example output: "3f8b7c2a1e4d9f0a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4d3e2f1a0b9c8d7e6f5a"
```

##### 2. Command Line / Terminal (OpenSSL)
```bash
openssl rand -hex 32
# Output: e.g. 7d8f3b2a5c1e9a4f6d8b2e1c3a5f7d9b0a2e4c6f8b1d3a5e7c9f0b2d4a6e8c1f
```

##### 3. Node.js / TypeScript
```javascript
const crypto = require('crypto');

function generateMasterToken() {
    return crypto.randomBytes(32).toString('hex');
}
```

##### 4. Python
```python
import secrets

master_token = secrets.token_hex(32)
# Output: 64-character hex string
```

#### How the Client App Uses the Token
1. **At Claim Time**: The app generates `master_token`, sends it in `POST /api/claim` along with `pop`, and securely saves it in Android `EncryptedSharedPreferences` / iOS Keychain.
2. **At Control Time**: The app attaches the token to every relay request via the `Authorization` header:
   ```http
   Authorization: Bearer <saved_master_token>
   ```

### Three-State Security Lock

```mermaid
stateDiagram-v2
    [*] --> Unclaimed: Fresh Flash / Factory Reset
    Unclaimed --> Unclaimed: Reject /api/relay (401 Unauthorized)
    Unclaimed --> Claimed: POST /api/claim (PoP matches secret)
    Claimed --> Claimed: /api/relay allowed (Bearer token verified)
    Claimed --> Unclaimed: POST /api/unclaim (Valid token or PoP)
```

1. **State 1: Unclaimed (Factory / Fresh Boot)**
   * mDNS announces service with TXT record: `claimed=0`.
   * Accepts read-only discovery requests (`/api/info`, `/api/heartbeat`, `/api/wifi/status`).
   * **All relay control endpoints (`/api/relay`, etc.) are locked** and return `401 Unauthorized`.
   * Accepts claim handshake at `POST /api/claim`.

2. **State 2: Claim Handshake**
   * Mobile app scans the physical QR code and obtains the `pop` secret.
   * Mobile app sends `POST /api/claim` containing `pop` and an app-generated `master_token`.
   * ESP8266 verifies `pop` against internal flash memory.
   * If correct:
     * Saves `master_token` and `claimed = true` to EEPROM.
     * Alters mDNS dynamic TXT record to `claimed=1`.
     * Refreshes OLED display with a solid `[CLAIMED]` badge and `"CLAIM SUCCESS!"` toast.
     * Responds with `200 OK`.

3. **State 3: Claimed (Locked Operation)**
   * Relay control endpoints (`/api/relay`, `/api/relay/1`, `/api/relay/2`, `/api/relay/all`) require authentication.
   * Requests must supply the `master_token` using one of the following methods:
     * Header: `Authorization: Bearer <master_token>`
     * Header: `X-Master-Token: <master_token>`
     * JSON Body: `{"token": "<master_token>"}` or `{"master_token": "<master_token>"}`
   * Any request with a missing or invalid token is rejected with `401 Unauthorized`.

---

## Multicast DNS (mDNS) Discovery

The device runs an mDNS responder that allows mobile apps to locate it on the local area network without needing to guess its DHCP IP address.

* **Hostname**: `smartsocket.local`
* **Service**: `_http._tcp` on Port `80`

### Dynamic TXT Records
Clients querying the `_http._tcp` service receive DNS-SD TXT key-value pairs describing device capabilities:

| Key | Example Value | Description |
|---|---|---|
| `id` | `RELAY_8266_NODE` | Unique node identifier |
| `type` | `node` | Device role (node switch vs gateway) |
| `switches` | `2` | Number of physical relay switches |
| `energy` | `0` | Energy monitoring capability (`0` = disabled) |
| `transport` | `lan` | Primary transport mechanism (`lan` or `ble`) |
| `claimed` | `0` or `1` | Real-time pairing lock state (`0` = unclaimed, `1` = claimed) |

When the device transition between claimed and unclaimed, `MDNS.announce()` broadcast the updated TXT record across the subnet immediately.

---

## OLED Display Interface (128x32)

The 0.91-inch OLED screen provides real-time diagnostics:

```
+-------------------------------------------------------+
| [CLAIMED]                                     WiFi:OK |  Row 1 (Y=0..8): Badges
|-------------------------------------------------------|  Divider (Y=10)
| IP: 192.168.4.15                                      |  Row 2 (Y=13): IP / Event Toast
| SW1:[ON ]                           SW2:[OFF]         |  Row 3 (Y=23): Relay Status
+-------------------------------------------------------+
```

### Visual Indicators
* **Header Badge (Top-Left)**:
  * Inverted Solid Box: **`CLAIMED`** (Device is paired and protected).
  * Outlined Box: **`UNCLAIMED`** (Device is in open factory state).
* **Wi-Fi Status (Top-Right)**:
  * `WiFi:OK`: Connected to local access point.
  * `WiFi:--`: Disconnected or negotiating DHCP.
* **Information Row (Middle)**:
  * Displays current local IP address: `IP: 192.168.x.x`
  * Displays transient action toasts for 2.5 seconds when events occur:
    * `"CLAIM SUCCESS!"`
    * `"CLAIM REJECTED"`
    * `"RESET: UNCLAIMED"`
* **Switch Badges (Bottom)**:
  * `SW1:[ON ]` or `SW1:[OFF]`
  * `SW2:[ON ]` or `SW2:[OFF]`

---

## Complete REST API Specification

### Base URL
```
http://smartsocket.local
# or
http://<DEVICE_IP>
```

---

### `GET /api/info`
Returns general device metadata and switch capabilities. Accessible without authentication.

* **Authentication**: None
* **Response `200 OK`**:
```json
{
  "id": "RELAY_8266_NODE",
  "type": "node",
  "switches": 2,
  "energy_monitoring": false,
  "claimed": false,
  "transport": "lan",
  "relays": {
    "1": false,
    "2": false
  }
}
```

---

### `GET /api/heartbeat`
Lightweight health-check endpoint to confirm the node is responsive.

* **Authentication**: None
* **Response `200 OK`**:
```json
{
  "ack": true,
  "id": "RELAY_8266_NODE",
  "claimed": true,
  "uptime_s": 348,
  "relays": {
    "1": true,
    "2": false
  }
}
```

---

### `GET /api/wifi/status`
Returns wireless link statistics (retained for backward compatibility with mobile dashboards).

* **Authentication**: None
* **Response `200 OK`**:
```json
{
  "connected": true,
  "ssid": "ESP GATE",
  "ip": "192.168.4.15",
  "mac": "DE:AD:BE:EF:01:01",
  "rssi": -52,
  "claimed": true
}
```

---

### `POST /api/claim`
Pairs an unclaimed device by providing the factory PoP secret and registering the client's master authentication token.

* **Authentication**: Requires valid physical `pop` secret.
* **Headers**: `Content-Type: application/json`
* **Request Body**:
```json
{
  "pop": "NODE_SEC_4102",
  "master_token": "MY_APP_SECRET_TOKEN_4821"
}
```

* **Responses**:
  * **`200 OK`**: Device claimed successfully.
    ```json
    {
      "status": "claimed",
      "message": "Device successfully claimed"
    }
    ```
  * **`400 Bad Request`**: Missing body, missing `pop`, or missing `master_token`.
    ```json
    { "error": "Field 'pop' is required" }
    ```
  * **`403 Forbidden`**: PoP secret does not match internal flash memory.
    ```json
    { "error": "Invalid Proof of Possession (PoP)" }
    ```
  * **`409 Conflict`**: Device has already been claimed.
    ```json
    { "error": "Device already claimed" }
    ```

---

### `GET` & `POST /api/verify`
Tests whether an existing token is valid for this device.

* **Headers**: `Authorization: Bearer <master_token>` or `X-Master-Token: <master_token>`
* **Responses**:
  * **`200 OK`**:
    ```json
    {
      "verified": true,
      "id": "RELAY_8266_NODE"
    }
    ```
  * **`401 Unauthorized`**: Token invalid or device unclaimed.
    ```json
    {
      "error": "Unauthorized",
      "verified": false
    }
    ```

---

### `GET /api/relay`
Fetches the current state of both relays.

* **Authentication**: Required (`master_token`).
* **Headers**: `Authorization: Bearer <master_token>`
* **Responses**:
  * **`200 OK`**:
    ```json
    {
      "1": true,
      "2": false
    }
    ```
  * **`401 Unauthorized`**: Device unclaimed or token invalid.

---

### `POST /api/relay`
Primary relay switching endpoint. Supports controlling a single switch, both switches at once, or multi-channel key sets.

* **Authentication**: Required (`master_token`).
* **Headers**:
  * `Authorization: Bearer <master_token>`
  * `Content-Type: application/json`

#### Option A: Individual Relay Control
```json
{
  "relay": 1,
  "state": true
}
```
*(Sets Relay 1 to ON. Use `"relay": 2` for Relay 2).*

#### Option B: Simultaneous Control ("all" or "both")
```json
{
  "relay": "all",
  "state": true
}
```
*(Turns both Relay 1 and Relay 2 ON simultaneously).*

#### Option C: Multi-Key Payload
```json
{
  "relay1": true,
  "relay2": false
}
```

* **Responses**:
  * **`200 OK`**:
    ```json
    {
      "success": true,
      "relays": {
        "1": true,
        "2": false
      }
    }
    ```
  * **`401 Unauthorized`**: If device is unclaimed or token is invalid.
    ```json
    { "error": "Device unclaimed. Claim device first." }
    ```
  * **`400 Bad Request`**: Missing body or unparsable relay instruction.

---

### `POST /api/relay/1`
Convenience endpoint to control or toggle Switch 1 directly.

* **Authentication**: Required (`master_token`).
* **Request Body** *(Optional)*:
```json
{ "state": true }
```
*(If body is empty, toggles Relay 1 state).*
* **Response `200 OK`**: Returns current status of all relays.

---

### `POST /api/relay/2`
Convenience endpoint to control or toggle Switch 2 directly.

* **Authentication**: Required (`master_token`).
* **Request Body** *(Optional)*:
```json
{ "state": false }
```
*(If body is empty, toggles Relay 2 state).*
* **Response `200 OK`**: Returns current status of all relays.

---

### `POST /api/relay/all`
Convenience endpoint to set all switches simultaneously.

* **Authentication**: Required (`master_token`).
* **Request Body** *(Optional, defaults to `true`)*:
```json
{ "state": false }
```
* **Response `200 OK`**: Returns current status of all relays.

---

### `POST /api/unclaim` (or `/api/reset`)
Reverts the device to factory `UNCLAIMED` state. Clears `master_token` from EEPROM, turns off all relays, and updates the display and mDNS records.

* **Authentication**: Can be authenticated with either:
  1. `Authorization: Bearer <master_token>`
  2. JSON body containing the factory `pop` secret:
     ```json
     { "pop": "NODE_SEC_4102" }
     ```
* **Response `200 OK`**:
```json
{
  "status": "unclaimed",
  "message": "Device reverted to factory unclaimed state"
}
```

---

## Step-by-Step Usage & Testing Guide (cURL)

Replace `smartsocket.local` with the actual IP address shown on the OLED display if mDNS resolution is unavailable on your test client.

### Step 1: Check Device Discovery & Info
```bash
curl http://smartsocket.local/api/info
```
*Expected: `claimed: false`, `switches: 2`, `energy_monitoring: false`.*

---

### Step 2: Attempt Unauthorized Relay Control (Verify Security Lock)
```bash
curl -X POST http://smartsocket.local/api/relay \
  -H "Content-Type: application/json" \
  -d '{"relay": 1, "state": true}'
```
*Expected: `401 Unauthorized` with `Device unclaimed. Claim device first.`*

---

### Step 3: Perform Proof of Possession (PoP) Claim Handshake
```bash
curl -X POST http://smartsocket.local/api/claim \
  -H "Content-Type: application/json" \
  -d '{
    "pop": "NODE_SEC_4102",
    "master_token": "MY_SECRET_APP_TOKEN_99"
  }'
```
*Expected:*
* HTTP `200 OK`: `{"status":"claimed","message":"Device successfully claimed"}`
* OLED display shows `"CLAIM SUCCESS!"` and top badge changes to inverted `[CLAIMED]`.

---

### Step 4: Verify Token Authentication
```bash
curl http://smartsocket.local/api/verify \
  -H "Authorization: Bearer MY_SECRET_APP_TOKEN_99"
```
*Expected: `200 OK` with `{"verified":true,"id":"RELAY_8266_NODE"}`.*

---

### Step 5: Turn ON Relay 1 (Switch 1)
```bash
curl -X POST http://smartsocket.local/api/relay \
  -H "Authorization: Bearer MY_SECRET_APP_TOKEN_99" \
  -H "Content-Type: application/json" \
  -d '{"relay": 1, "state": true}'
```
*Expected: Relay 1 clicks ON, OLED shows `SW1:[ON ] SW2:[OFF]`, HTTP 200 returned.*

---

### Step 6: Turn Both Relays ON Simultaneously
```bash
curl -X POST http://smartsocket.local/api/relay \
  -H "Authorization: Bearer MY_SECRET_APP_TOKEN_99" \
  -H "Content-Type: application/json" \
  -d '{"relay": "all", "state": true}'
```
*Expected: Both relays click ON, OLED shows `SW1:[ON ] SW2:[ON ]`.*

---

### Step 7: Turn Both Relays OFF
```bash
curl -X POST http://smartsocket.local/api/relay/all \
  -H "Authorization: Bearer MY_SECRET_APP_TOKEN_99" \
  -H "Content-Type: application/json" \
  -d '{"state": false}'
```
*Expected: Both relays click OFF, OLED shows `SW1:[OFF] SW2:[OFF]`.*

---

### Step 8: Factory Reset (Unclaim)
```bash
curl -X POST http://smartsocket.local/api/unclaim \
  -H "Authorization: Bearer MY_SECRET_APP_TOKEN_99"
```
*Expected:*
* HTTP `200 OK`: `{"status":"unclaimed","message":"Device reverted to factory unclaimed state"}`
* OLED shows `"RESET: UNCLAIMED"` and top badge changes to `[UNCLAIMED]`.
* Subsequent relay commands are locked until the next claim handshake.

---

## Persistent Storage (EEPROM)

The firmware reserves 512 bytes of flash-emulated EEPROM starting at address `0`:

```cpp
struct DeviceStorage {
  uint32_t magic;         // 0x506F5031 ("PoP1") magic header
  bool claimed;           // Boolean pairing lock state
  char masterToken[65];   // Null-terminated string holding authorization token
};
```

1. **First Boot / Flash**: If `magic != 0x506F5031`, memory is automatically formatted with factory defaults (`claimed = false`, blank token).
2. **Commit Lifecycle**: Flash is only written when `POST /api/claim` or `POST /api/unclaim` succeeds, minimizing flash wear.
3. **Power Resilience**: In case of a blackout, the device reboots directly into `[CLAIMED]` state with its registered `master_token` intact.

---

## Build and Flash Instructions

### Prerequisites
* [PlatformIO Core (CLI)](https://platformio.org/) or the PlatformIO Extension in VS Code.

### Building Firmware
From the project root:
```bash
pio run -d firmware/esp-8266
```

### Uploading to ESP8266 NodeMCU
Connect your NodeMCU via USB and run:
```bash
pio run -d firmware/esp-8266 -t upload
```

### Serial Monitor
```bash
pio device monitor -d firmware/esp-8266 -b 115200
```
