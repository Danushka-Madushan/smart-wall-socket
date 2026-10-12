The ESP32-S3 Wi-Fi Provisioning process over Bluetooth Low Energy (BLE) with Proof of Possession (PoP) uses Espressif’s Unified Provisioning Protocol (`protocomm`) to establish an encrypted, authenticated link between an offline phone and an unconfigured micro-controller.

---

### Step-by-Step Provisioning Architecture

1. **Check Storage and Trigger BLE Mode:** Firmware Boot.
Upon boot, the ESP32-S3 checks its Non-Volatile Storage (NVS) for saved Wi-Fi station credentials.

* If valid Wi-Fi credentials exist, the device skips BLE entirely, connects to the local LAN, and starts application loops (such as Core 0 networking and Core 1 power sampling).
* If no credentials exist—or if a physical factory reset button is held—the firmware starts the `wifi_prov_mgr` service using the BLE transport scheme (`WIFI_PROV_SCHEME_BLE`).
* **Verification:** The device's serial log prints `wifi_prov_mgr: Provisioning started with BLE scheme`.


2. **Broadcast Peripheral GATT Service:** BLE Advertising.
The ESP32-S3 initializes NimBLE/Bluedroid and broadcasts standard BLE advertising packets containing a service UUID and a unique device name (e.g., `PROV_A1B2C3`, derived from the last 3 bytes of its MAC address).

* Inside the BLE GATT server, it registers dedicated characteristics (`endpoints`) backed by Protocol Buffers (`protobuf`):
* `prov-session`: Handles key exchange and security validation.
* `prov-scan`: Handles Wi-Fi network scanning.
* `prov-config`: Receives target Wi-Fi credentials and connection triggers.


* **Verification:** A BLE scanner tool (such as nRF Connect) can discover the peripheral `PROV_A1B2C3` with its provisioning GATT service active.


3. **Capture Device Identity and Secret:** Mobile App Scan.
The mobile phone—connected only to cellular data or no network at all—opens the camera within your custom application to scan the physical QR code affixed to the socket.

* The QR code contains a structured JSON payload:

```json
{
  "ver": "v1",
  "name": "PROV_A1B2C3",
  "pop": "S3_SEC_9821",
  "transport": "ble"
}

```

* The app parses `name` (the target BLE peripheral) and caches `pop` (the Proof of Possession secret).
* **Verification:** The app automatically filters BLE scan results and pairs strictly with the peripheral matching `"PROV_A1B2C3"` without showing extraneous Bluetooth devices.


4. **Execute Cryptographic PoP Handshake:** Security 1 Mutual Authentication.
The app and ESP32-S3 establish a secure channel over `prov-session` using **Security 1** (Curve25519 ECDH + AES-256-CTR):

1. **Key Exchange:** Both the app and ESP32-S3 generate ephemeral Curve25519 keypairs and swap public keys over BLE.
2. **Shared Secret:** Both sides compute a shared secret via Elliptic Curve Diffie-Hellman (ECDH).
3. **PoP Binding:** Both derive an AES-CTR session key by mixing the shared secret with the `pop` token (`S3_SEC_9821`).
4. **Challenge-Response:** The app encrypts a verification payload using this key. The ESP32-S3 decrypts it and compares it with its internal PoP.

* If the token does not match, the ESP32-S3 drops the BLE connection immediately, preventing brute-force attacks or eavesdropping.
* **Verification:** The app reports a successful cryptographic session, and subsequent GATT packets transition to encrypted ciphertext.


5. **Scan and Transmit Wi-Fi Credentials:** Encrypted BLE Exchange.
With the encrypted tunnel active:

1. The app writes to `prov-scan`. The ESP32-S3 triggers a local 2.4 GHz Wi-Fi scan and returns the list of nearby SSIDs and signal strengths back over BLE.
2. The user selects their home network in the mobile app and enters the Wi-Fi password.
3. The app packs the SSID and password into a `proto-config` payload, encrypts it with the negotiated AES session key, and writes it to `prov-config`.

* **Verification:** The ESP32-S3 receives the packet and emits the internal event `WIFI_PROV_CRED_RECV`.


6. **Join Network and Release BLE Resources:** State Transition.
The ESP32-S3 attempts to connect to the configured Wi-Fi access point:

1. It tests the connection. If the password is wrong, it returns an error code to the app over BLE so the user can re-enter it.
2. Once an IP address is obtained (`IP_EVENT_STA_GOT_IP`), it reports `Success` to the mobile app.
3. The ESP32-S3 writes the credentials permanently to the NVS partition.
4. The firmware stops BLE advertising, halts the Bluetooth controller stack, and reclaims ~80–100 KB of internal RAM.

* **Verification:** The device disconnects from the phone's BLE interface, joins the router, and starts broadcasting its mDNS/LAN discovery services.


---

### Security Mechanics: Why PoP is Essential

Without Proof of Possession, any nearby smartphone running a provisioning app could connect to the ESP32-S3's open BLE advertisement and push rogue Wi-Fi credentials or extract local network data.

By demanding the PoP key during the Curve25519 session derivation, the handshake mathematically guarantees that:

* **Physical Proximity & Access:** Only the person physically looking at or holding the device's printed QR code can authorize setup.
* **Over-the-Air Protection:** The raw PoP string is never sent across BLE in plaintext. An attacker sniffing BLE packets cannot extract the key because it is used only as a key-derivation input for the encrypted session.
