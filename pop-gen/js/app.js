/**
 * Smart Wall Socket - Proof of Possession (PoP) QR Generator
 * Client-Side Controller & High-DPI Badge Compositor
 */

(function () {
  'use strict';

  // Preset hardware configurations
  const PRESETS = {
    esp32s: {
      id: 'RELAY_32S_NODE',
      pop: 'S3_SEC_9821',
      transport: 'ble',
      tag: 'ESP32-S Smart Socket',
      format: 'compact'
    },
    esp8266: {
      id: 'ESP_SWITCH_2CH',
      pop: 'NODE_SEC_4102',
      transport: 'lan',
      tag: 'ESP8266 2-Ch Switch',
      format: 'compact'
    },
    custom: {
      id: 'SMART_SOCKET_01',
      pop: 'SEC_8492',
      transport: 'ble',
      tag: 'Custom Device',
      format: 'compact'
    }
  };

  // State
  const state = {
    currentPreset: 'esp32s',
    format: 'compact', // 'compact' | 'json' | 'raw'
    deviceId: PRESETS.esp32s.id,
    popPin: PRESETS.esp32s.pop,
    transport: PRESETS.esp32s.transport,
    darkColor: '#000000',
    lightColor: '#ffffff',
    eccLevel: 'M', // L, M, Q, H
    margin: 2
  };

  // DOM Elements
  const deviceIdInput = document.getElementById('deviceIdInput');
  const popPinInput = document.getElementById('popPinInput');
  const transportInput = document.getElementById('transportInput');
  const eccSelect = document.getElementById('eccSelect');
  const darkColorInput = document.getElementById('darkColorInput');
  const lightColorInput = document.getElementById('lightColorInput');
  const marginInput = document.getElementById('marginInput');
  const payloadDisplay = document.getElementById('payloadDisplay');
  const qrCanvas = document.getElementById('qrCanvas');
  const stickerId = document.getElementById('stickerId');
  const stickerPin = document.getElementById('stickerPin');
  const stickerTag = document.getElementById('stickerTag');
  const toast = document.getElementById('toast');
  const toastMessage = document.getElementById('toastMessage');

  // Preload logo for badge compositor
  const logoImage = new Image();
  logoImage.src = 'assets/icon.svg';

  // Initialize
  function init() {
    bindEvents();
    loadPreset('esp32s');
  }

  function bindEvents() {
    // Presets
    document.querySelectorAll('.preset-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const presetKey = btn.dataset.preset;
        loadPreset(presetKey);
      });
    });

    // Format tabs
    document.querySelectorAll('.tab-btn').forEach(tab => {
      tab.addEventListener('click', (e) => {
        document.querySelectorAll('.tab-btn').forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        state.format = tab.dataset.format;
        updateQr();
      });
    });

    // Form Inputs
    deviceIdInput.addEventListener('input', (e) => {
      state.deviceId = e.target.value.trim();
      highlightCustomPreset();
      updateQr();
    });

    popPinInput.addEventListener('input', (e) => {
      state.popPin = e.target.value.trim();
      highlightCustomPreset();
      updateQr();
    });

    transportInput.addEventListener('change', (e) => {
      state.transport = e.target.value;
      highlightCustomPreset();
      updateQr();
    });

    // Random PIN button
    document.getElementById('randomPinBtn').addEventListener('click', () => {
      state.popPin = generateRandomPin();
      popPinInput.value = state.popPin;
      highlightCustomPreset();
      updateQr();
      showToast('Generated random PoP PIN: ' + state.popPin);
    });

    // Style Customizations
    eccSelect.addEventListener('change', (e) => {
      state.eccLevel = e.target.value;
      updateQr();
    });

    darkColorInput.addEventListener('input', (e) => {
      state.darkColor = e.target.value;
      updateQr();
    });

    lightColorInput.addEventListener('input', (e) => {
      state.lightColor = e.target.value;
      updateQr();
    });

    marginInput.addEventListener('input', (e) => {
      state.margin = parseInt(e.target.value, 10) || 2;
      document.getElementById('marginVal').textContent = state.margin;
      updateQr();
    });

    // Copy Payload Button
    document.getElementById('copyPayloadBtn').addEventListener('click', () => {
      const payload = buildPayload();
      navigator.clipboard.writeText(payload).then(() => {
        showToast('Payload string copied to clipboard!');
      }).catch(() => {
        showToast('Failed to copy payload');
      });
    });

    // Export Buttons
    document.getElementById('downloadPngBtn').addEventListener('click', () => downloadQrPng(600));
    document.getElementById('downloadHighResPngBtn').addEventListener('click', () => downloadQrPng(1200));
    document.getElementById('downloadSvgBtn').addEventListener('click', downloadQrSvg);
    document.getElementById('downloadBadgeBtn').addEventListener('click', downloadStickerBadge);
    document.getElementById('copyBadgeBtn').addEventListener('click', copyBadgeToClipboard);
    document.getElementById('printBadgeBtn').addEventListener('click', () => {
      window.print();
    });
  }

  function loadPreset(presetKey) {
    state.currentPreset = presetKey;
    const preset = PRESETS[presetKey];
    if (!preset) return;

    state.deviceId = preset.id;
    state.popPin = preset.pop;
    state.transport = preset.transport;

    deviceIdInput.value = state.deviceId;
    popPinInput.value = state.popPin;
    transportInput.value = state.transport;

    document.querySelectorAll('.preset-btn').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.preset === presetKey);
    });

    updateQr();
  }

  function highlightCustomPreset() {
    document.querySelectorAll('.preset-btn').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.preset === 'custom');
    });
    state.currentPreset = 'custom';
  }

  function generateRandomPin() {
    const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
    let pin = 'SEC_';
    for (let i = 0; i < 4; i++) {
      pin += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return pin;
  }

  function buildPayload() {
    const id = state.deviceId || 'NODE';
    const pop = state.popPin || '0000';
    const tr = state.transport || 'ble';

    switch (state.format) {
      case 'compact':
        return `v1;${id};${pop};${tr}`;
      case 'json':
        return JSON.stringify({ id: id, pop: pop, transport: tr });
      case 'raw':
        return pop;
      default:
        return `v1;${id};${pop};${tr}`;
    }
  }

  function updateQr() {
    const payload = buildPayload();
    payloadDisplay.textContent = payload;

    // Update sticker label text
    stickerId.textContent = state.deviceId || 'UNKNOWN DEVICE';
    stickerPin.textContent = `PIN: ${state.popPin || 'NONE'}`;

    let transportName = 'BLUETOOTH (BLE)';
    if (state.transport === 'lan') transportName = 'WI-FI (LAN)';
    else if (state.transport === 'softap') transportName = 'ACCESS POINT';
    stickerTag.textContent = transportName;

    // Render QR Code onto #qrCanvas
    try {
      // 0 = auto-detect minimum type number
      const qr = qrcode(0, state.eccLevel);
      qr.addData(payload, 'Byte');
      qr.make();

      const moduleCount = qr.getModuleCount();
      const margin = state.margin;
      const cellSize = Math.max(4, Math.floor(220 / (moduleCount + margin * 2)));
      const canvasSize = (moduleCount + margin * 2) * cellSize;

      qrCanvas.width = canvasSize;
      qrCanvas.height = canvasSize;
      const ctx = qrCanvas.getContext('2d');

      // Fill background
      ctx.fillStyle = state.lightColor;
      ctx.fillRect(0, 0, canvasSize, canvasSize);

      // Fill modules
      ctx.fillStyle = state.darkColor;
      for (let row = 0; row < moduleCount; row++) {
        for (let col = 0; col < moduleCount; col++) {
          if (qr.isDark(row, col)) {
            ctx.fillRect(
              (col + margin) * cellSize,
              (row + margin) * cellSize,
              cellSize,
              cellSize
            );
          }
        }
      }

      state.currentQr = qr;
    } catch (err) {
      console.error('QR generation error:', err);
      showToast('Error generating QR code. Check input length.');
    }
  }

  // Generate an offscreen canvas containing only the QR code at specified resolution
  function renderQrToCanvas(targetSize) {
    if (!state.currentQr) return null;

    const qr = state.currentQr;
    const moduleCount = qr.getModuleCount();
    const margin = state.margin;
    const totalModules = moduleCount + margin * 2;
    const cellSize = Math.floor(targetSize / totalModules);
    const actualSize = totalModules * cellSize;

    const canvas = document.createElement('canvas');
    canvas.width = actualSize;
    canvas.height = actualSize;
    const ctx = canvas.getContext('2d');

    ctx.fillStyle = state.lightColor;
    ctx.fillRect(0, 0, actualSize, actualSize);

    ctx.fillStyle = state.darkColor;
    for (let row = 0; row < moduleCount; row++) {
      for (let col = 0; col < moduleCount; col++) {
        if (qr.isDark(row, col)) {
          ctx.fillRect(
            (col + margin) * cellSize,
            (row + margin) * cellSize,
            cellSize,
            cellSize
          );
        }
      }
    }

    return canvas;
  }

  // Download QR Code as PNG
  function downloadQrPng(size) {
    const canvas = renderQrToCanvas(size);
    if (!canvas) return;

    canvas.toBlob((blob) => {
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.download = `QR_${state.deviceId}_${size}px.png`;
      a.href = url;
      a.click();
      URL.revokeObjectURL(url);
      showToast(`Downloaded ${size}px QR PNG!`);
    }, 'image/png');
  }

  // Download QR Code as SVG vector
  function downloadQrSvg() {
    if (!state.currentQr) return;
    const qr = state.currentQr;
    const moduleCount = qr.getModuleCount();
    const margin = state.margin;
    const cellSize = 10;
    const totalSize = (moduleCount + margin * 2) * cellSize;

    let svg = `<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="${totalSize}" height="${totalSize}" viewBox="0 0 ${totalSize} ${totalSize}">\n`;
    svg += `  <rect width="100%" height="100%" fill="${state.lightColor}"/>\n`;
    svg += `  <path fill="${state.darkColor}" d="`;

    for (let row = 0; row < moduleCount; row++) {
      for (let col = 0; col < moduleCount; col++) {
        if (qr.isDark(row, col)) {
          const x = (col + margin) * cellSize;
          const y = (row + margin) * cellSize;
          svg += `M${x},${y}h${cellSize}v${cellSize}h-${cellSize}z `;
        }
      }
    }

    svg += `"/>\n</svg>`;

    const blob = new Blob([svg], { type: 'image/svg+xml;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.download = `QR_${state.deviceId}.svg`;
    a.href = url;
    a.click();
    URL.revokeObjectURL(url);
    showToast('Downloaded Vector SVG QR Code!');
  }

  // Composite complete high-DPI Sticker Badge onto offscreen Canvas (1000 x 1250 px)
  function renderStickerBadgeCanvas() {
    const W = 1000;
    const H = 1250;
    const canvas = document.createElement('canvas');
    canvas.width = W;
    canvas.height = H;
    const ctx = canvas.getContext('2d');

    // Solid crisp white label background
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, W, H);

    // Subtle label border
    ctx.strokeStyle = '#e5e7eb';
    ctx.lineWidth = 12;
    ctx.strokeRect(6, 6, W - 12, H - 12);

    // Top Brand Header
    const headerY = 70;
    if (logoImage.complete && logoImage.naturalWidth > 0) {
      try {
        ctx.drawImage(logoImage, 140, headerY - 10, 75, 75);
      } catch (_) {}
    }

    ctx.fillStyle = '#111827';
    ctx.font = 'bold 38px system-ui, -apple-system, sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText('SMART WALL SOCKET', 240, headerY + 28);

    ctx.fillStyle = '#6b7280';
    ctx.font = 'bold 22px system-ui, -apple-system, sans-serif';
    ctx.fillText('PROOF OF POSSESSION (PoP)', 240, headerY + 60);

    // QR Code in Center
    const qrSize = 640;
    const qrX = (W - qrSize) / 2;
    const qrY = 175;
    const qrCanvasObj = renderQrToCanvas(qrSize);
    if (qrCanvasObj) {
      ctx.drawImage(qrCanvasObj, qrX, qrY, qrSize, qrSize);
    }

    // Dashed divider line
    const dividerY = 860;
    ctx.beginPath();
    ctx.strokeStyle = '#d1d5db';
    ctx.lineWidth = 4;
    ctx.setLineDash([12, 10]);
    ctx.moveTo(80, dividerY);
    ctx.lineTo(W - 80, dividerY);
    ctx.stroke();
    ctx.setLineDash([]);

    // Device Identification
    ctx.textAlign = 'center';
    ctx.fillStyle = '#111827';
    ctx.font = 'bold 44px Monaco, "Cascadia Code", "Fira Code", monospace';
    ctx.fillText(state.deviceId || 'NODE_UNKNOWN', W / 2, dividerY + 65);

    // PoP PIN Text
    ctx.fillStyle = '#374151';
    ctx.font = 'bold 36px Monaco, "Cascadia Code", "Fira Code", monospace';
    ctx.fillText(`PIN: ${state.popPin || 'NONE'}`, W / 2, dividerY + 125);

    // Transport Badge pill
    let transportLabel = 'BLUETOOTH SETUP';
    if (state.transport === 'lan') transportLabel = 'WI-FI LAN PAIRING';
    else if (state.transport === 'softap') transportLabel = 'AP SETUP MODE';

    const badgeW = 380;
    const badgeH = 50;
    const badgeX = (W - badgeW) / 2;
    const badgeY = dividerY + 160;

    ctx.fillStyle = '#f3f4f6';
    ctx.beginPath();
    ctx.roundRect(badgeX, badgeY, badgeW, badgeH, 25);
    ctx.fill();

    ctx.fillStyle = '#1f2937';
    ctx.font = 'bold 22px system-ui, -apple-system, sans-serif';
    ctx.fillText(transportLabel, W / 2, badgeY + 34);

    // Footer instruction
    ctx.fillStyle = '#9ca3af';
    ctx.font = '500 22px system-ui, -apple-system, sans-serif';
    ctx.fillText('Scan with SocketMan Android App to claim', W / 2, H - 55);

    return canvas;
  }

  // Download complete Sticker Badge as PNG
  function downloadStickerBadge() {
    const badgeCanvas = renderStickerBadgeCanvas();
    badgeCanvas.toBlob((blob) => {
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.download = `Sticker_${state.deviceId}.png`;
      a.href = url;
      a.click();
      URL.revokeObjectURL(url);
      showToast('Downloaded High-DPI Sticker Badge!');
    }, 'image/png');
  }

  // Copy Badge Image to Clipboard
  function copyBadgeToClipboard() {
    if (!navigator.clipboard || !window.ClipboardItem) {
      showToast('Clipboard API not supported in this browser.');
      return;
    }

    const badgeCanvas = renderStickerBadgeCanvas();
    badgeCanvas.toBlob((blob) => {
      try {
        const item = new ClipboardItem({ 'image/png': blob });
        navigator.clipboard.write([item]).then(() => {
          showToast('Sticker badge copied to clipboard!');
        }).catch((err) => {
          console.error('Clipboard copy failed:', err);
          showToast('Failed to copy to clipboard.');
        });
      } catch (e) {
        showToast('Clipboard write failed.');
      }
    }, 'image/png');
  }

  function showToast(msg) {
    if (!toast) return;
    toastMessage.textContent = msg;
    toast.classList.add('show');
    clearTimeout(toast.timeoutId);
    toast.timeoutId = setTimeout(() => {
      toast.classList.remove('show');
    }, 2800);
  }

  // Boot
  document.addEventListener('DOMContentLoaded', init);
})();
