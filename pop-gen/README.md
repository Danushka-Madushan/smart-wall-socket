# Smart Wall Socket — PoP QR & Sticker Generator (`pop-gen`)

A lightweight, zero-dependency, client-side web application specifically designed to generate, customize, and export **Proof of Possession (PoP)** QR codes and physical pairing stickers for the **Smart Wall Socket** IoT ecosystem.

---

## 🚀 Live Demo & GitHub Pages Hosting

This tool is 100% client-side (pure HTML5, CSS3, and JavaScript) with zero external server dependencies and uses relative paths. It can be hosted directly on **GitHub Pages**:

### Hosting Steps on GitHub:
1. Go to your repository on GitHub: `Settings` $\rightarrow$ `Pages`.
2. Under **Build and deployment**:
   - **Source**: Select `Deploy from a branch`.
   - **Branch**: Select `main` (or your primary branch) and `/ (root)`.
3. Your QR generator will be live at:
   ```
   https://<your-username>.github.io/smart-wall-socket/pop-gen/
   ```

### Running Locally:
Simply open `pop-gen/index.html` in any modern web browser, or serve it using Python:
```powershell
# From the repository root:
python -m http.server 8080
# Open http://localhost:8080/pop-gen/
```

---

## ⚡ Key Features

- **Hardware Presets**:
  - **ESP32-S Smart Wall Socket**: Auto-fills `RELAY_32S_NODE`, PoP PIN `S3_SEC_9821`, Transport `ble` (FreeRTOS Core 1 Energy Monitoring, Single Inverted Relay).
  - **ESP8266 2-Channel Switch**: Auto-fills `ESP_SWITCH_2CH`, PoP PIN `NODE_SEC_4102`, Transport `lan` (Dual Relays, mDNS Discovery).
  - **Custom Node**: User-defined device identifiers and secret keys.
- **Protocol Encodings**:
  - **Compact Semicolon (SSV)**: `v1;<device_id>;<pop_secret>;<transport>` (Recommended default recognized by the SocketMan Android app).
  - **Minified JSON**: `{"id":"...","pop":"...","transport":"..."}`.
  - **Raw PIN**: Single secret token.
- **Export Formats**:
  - 🏷️ **Complete Sticker Badge (PNG)**: Renders a production-ready 1000 $\times$ 1250 px label card with logo, QR code, Device ID, PIN text, and scan instructions.
  - 💾 **Standard PNG**: 600 $\times$ 600 px high-contrast QR image.
  - 🖼️ **High-DPI PNG**: 1200 $\times$ 1200 px ultra-sharp QR image for large prints.
  - 📐 **Vector SVG**: Scalable vector XML with zero pixelation at any resolution.
  - 📋 **Copy to Clipboard**: Quick copy of the sticker image directly into clipboard.
  - 🖨️ **Thermal Label Printing**: Built-in `@media print` layout sized for 50mm $\times$ 50mm adhesive label printers.

---

## 🖨️ Thermal Sticker Label Sizing Guide

When printing physical adhesive stickers for your smart wall socket enclosure:

| Label Size | Recommended Format | Print Dialog Tip |
|---|---|---|
| **50mm $\times$ 50mm (2" $\times$ 2")** | Sticker Badge (PNG) or Browser Print (`Ctrl+P`) | Set margins to **None**, scale to **Fit to Printable Area**. |
| **40mm $\times$ 40mm (1.5" $\times$ 1.5")** | Sticker Badge (PNG) | Scale down to 80% in label printer software. |
| **25mm $\times$ 25mm (1" $\times$ 1")** | QR Only (600px PNG) | Clean QR only for small enclosure cavities. |

---

## 📂 File Structure

```
pop-gen/
├── index.html            # Main web application entry point
├── css/
│   └── style.css         # Modern dark glassmorphism theme & print stylesheet
├── js/
│   ├── qrcode.min.js     # Bundled zero-dependency QR code generator engine
│   └── app.js            # State management, sticker compositor & export handlers
├── assets/
│   └── icon.svg          # Self-contained project logo
└── README.md             # This documentation guide
```
