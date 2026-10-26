#include "web_server.hpp"
#include "config.hpp"
#include "../common/log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <unistd.h>

extern "C" {
    long __syscall(long sysno, ...);
}

typedef int (*pthread_create_fn)(void** thread, void* attr, void* (*start_routine)(void*), void* arg);

static volatile bool g_web_running = false;
volatile bool g_should_unload = false;
static long g_server_fd = -1;
static void* g_web_thread = nullptr;

#define SYS_read        3
#define SYS_write       4
#define SYS_close       6
#define SYS_accept      30
#define SYS_socket      97
#define SYS_bind        104
#define SYS_setsockopt  105
#define SYS_listen      106

#define AF_INET         2
#define SOCK_STREAM     1
#define SOL_SOCKET      0xffff
#define SO_REUSEADDR    0x0004
#define SO_RCVTIMEO     0x1006

struct bsd_sockaddr_in {
    uint8_t  sin_len;
    uint8_t  sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char     sin_zero[8];
};

struct bsd_timeval {
    long tv_sec;
    long tv_usec;
};

// ---------------------------------------------------------------------------
// Embedded HTML/CSS/JS Page
// ---------------------------------------------------------------------------
static const char s_html_page[] = R"rawhtml(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PS5 Telemetry Overlay</title>
<style>
:root {
  --bg-main: #0b0e14;
  --bg-card: #151b26;
  --bg-card-hover: #1c2433;
  --border-color: #263245;
  --accent-blue: #0070d1;
  --accent-glow: #00a2ff;
  --accent-green: #00e676;
  --text-main: #f0f4f8;
  --text-dim: #8b9bb4;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background-color: var(--bg-main);
  color: var(--text-main);
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  padding: 20px 16px 40px;
  display: flex;
  justify-content: center;
}
.container {
  width: 100%;
  max-width: 840px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}
.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--border-color);
}
.title-group h1 {
  font-size: 24px;
  font-weight: 700;
  display: flex;
  align-items: center;
  gap: 10px;
  letter-spacing: -0.5px;
}
.title-group p {
  color: var(--text-dim);
  font-size: 14px;
  margin-top: 4px;
}
.status-pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  background: rgba(0, 230, 118, 0.12);
  border: 1px solid rgba(0, 230, 118, 0.3);
  color: var(--accent-green);
  padding: 6px 14px;
  border-radius: 20px;
  font-size: 13px;
  font-weight: 600;
}
.status-dot {
  width: 8px;
  height: 8px;
  background: var(--accent-green);
  border-radius: 50%;
  box-shadow: 0 0 8px var(--accent-green);
}
.card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: 14px;
  padding: 20px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}
.card-title {
  font-size: 17px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 8px;
}

/* Screen Preview */
.preview-container {
  position: relative;
  width: 100%;
  aspect-ratio: 16 / 9;
  background: linear-gradient(135deg, #090c10 0%, #101928 50%, #080d14 100%);
  border: 2px solid var(--border-color);
  border-radius: 10px;
  overflow: hidden;
  box-shadow: inset 0 0 40px rgba(0,0,0,0.8);
}
.preview-screen-label {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  color: rgba(255, 255, 255, 0.08);
  font-size: 32px;
  font-weight: 900;
  letter-spacing: 4px;
  text-transform: uppercase;
  pointer-events: none;
}
.preview-overlay-bar {
  position: absolute;
  left: 0;
  width: 100%;
  display: flex;
  align-items: center;
  transition: all 0.2s ease;
  pointer-events: none;
  white-space: nowrap;
}
.preview-metric {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-weight: 700;
}
.preview-metric-val {
  color: #ffffff;
}
.preview-sep {
  color: rgba(255, 255, 255, 0.35);
  margin: 0 8px;
}

/* Metrics List & Reordering */
.metrics-list {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.metric-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--border-color);
  border-radius: 10px;
  padding: 12px 14px;
  transition: background 0.15s;
}
.metric-row:hover {
  background: var(--bg-card-hover);
}
.metric-left {
  display: flex;
  align-items: center;
  gap: 12px;
}
.order-badge {
  background: rgba(255, 255, 255, 0.08);
  color: var(--text-dim);
  font-size: 12px;
  font-weight: 700;
  width: 26px;
  height: 26px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.metric-info h4 {
  font-size: 15px;
  font-weight: 600;
}
.metric-info span {
  font-size: 12px;
  color: var(--text-dim);
}
.metric-right {
  display: flex;
  align-items: center;
  gap: 12px;
}
.btn-reorder {
  background: rgba(255, 255, 255, 0.07);
  border: 1px solid var(--border-color);
  color: var(--text-main);
  width: 32px;
  height: 32px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  font-size: 12px;
  transition: all 0.15s;
}
.btn-reorder:hover:not(:disabled) {
  background: var(--accent-blue);
  border-color: var(--accent-blue);
}
.btn-reorder:disabled {
  opacity: 0.25;
  cursor: not-allowed;
}
.color-input-wrapper {
  position: relative;
  display: flex;
  align-items: center;
  gap: 6px;
}
.color-input-wrapper input[type="color"] {
  -webkit-appearance: none;
  border: none;
  width: 32px;
  height: 32px;
  border-radius: 6px;
  cursor: pointer;
  background: transparent;
}
.color-input-wrapper input[type="color"]::-webkit-color-swatch-wrapper {
  padding: 0;
}
.color-input-wrapper input[type="color"]::-webkit-color-swatch {
  border: 1px solid rgba(255,255,255,0.25);
  border-radius: 6px;
}
.color-hex {
  font-family: monospace;
  font-size: 13px;
  width: 74px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border-color);
  color: var(--text-main);
  padding: 4px 6px;
  border-radius: 6px;
  text-transform: uppercase;
}

/* Switches & Controls */
.switch {
  position: relative;
  display: inline-block;
  width: 44px;
  height: 24px;
}
.switch input { opacity: 0; width: 0; height: 0; }
.slider {
  position: absolute;
  cursor: pointer;
  top: 0; left: 0; right: 0; bottom: 0;
  background-color: #2a3445;
  transition: .2s;
  border-radius: 24px;
}
.slider:before {
  position: absolute;
  content: "";
  height: 18px;
  width: 18px;
  left: 3px;
  bottom: 3px;
  background-color: white;
  transition: .2s;
  border-radius: 50%;
}
input:checked + .slider {
  background-color: var(--accent-blue);
}
input:checked + .slider:before {
  transform: translateX(20px);
}

/* Grids for Position & Style */
.grid-2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}
@media (max-width: 600px) {
  .grid-2 { grid-template-columns: 1fr; }
}

.segment-group {
  display: flex;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  padding: 4px;
  gap: 4px;
}
.segment-btn {
  flex: 1;
  background: transparent;
  border: none;
  color: var(--text-dim);
  padding: 8px 12px;
  font-size: 14px;
  font-weight: 600;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.15s;
}
.segment-btn.active {
  background: var(--accent-blue);
  color: white;
  box-shadow: 0 2px 6px rgba(0, 112, 209, 0.4);
}

.form-group {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.form-label {
  display: flex;
  justify-content: space-between;
  font-size: 14px;
  font-weight: 500;
  color: var(--text-main);
}
.form-val {
  color: var(--accent-glow);
  font-weight: 600;
  font-family: monospace;
}
input[type="range"] {
  -webkit-appearance: none;
  width: 100%;
  height: 6px;
  background: #2a3445;
  border-radius: 4px;
  outline: none;
}
input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 18px;
  height: 18px;
  background: var(--accent-blue);
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 0 6px rgba(0, 112, 209, 0.6);
  border: 2px solid white;
}

/* Actions */
.action-bar {
  display: flex;
  gap: 12px;
  position: sticky;
  bottom: 16px;
  z-index: 100;
}
.btn-save {
  flex: 2;
  background: linear-gradient(135deg, #0070d1 0%, #005bb5 100%);
  border: 1px solid #0084f7;
  color: #ffffff;
  padding: 14px 24px;
  font-size: 16px;
  font-weight: 700;
  border-radius: 10px;
  cursor: pointer;
  box-shadow: 0 4px 16px rgba(0, 112, 209, 0.4);
  transition: all 0.2s;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}
.btn-save:hover {
  background: linear-gradient(135deg, #0084f7 0%, #0070d1 100%);
  box-shadow: 0 6px 22px rgba(0, 112, 209, 0.6);
  transform: translateY(-1px);
}
.btn-reset {
  flex: 1;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid var(--border-color);
  color: var(--text-dim);
  padding: 14px 18px;
  font-size: 15px;
  font-weight: 600;
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.15s;
}
.btn-reset:hover {
  background: rgba(255, 255, 255, 0.12);
  color: var(--text-main);
}
.btn-unload {
  width: 100%;
  background: linear-gradient(135deg, #d32f2f 0%, #9a0007 100%);
  border: 1px solid #ff5252;
  color: #ffffff;
  padding: 14px 20px;
  font-size: 15px;
  font-weight: 700;
  border-radius: 10px;
  cursor: pointer;
  box-shadow: 0 4px 16px rgba(211, 47, 47, 0.4);
  transition: all 0.2s;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}
.btn-unload:hover {
  background: linear-gradient(135deg, #f44336 0%, #d32f2f 100%);
  box-shadow: 0 6px 22px rgba(244, 67, 54, 0.6);
  transform: translateY(-1px);
}

/* Toast */
.toast {
  position: fixed;
  bottom: 84px;
  left: 50%;
  transform: translateX(-50%) translateY(20px);
  background: #00c853;
  color: #000;
  font-weight: 700;
  padding: 12px 24px;
  border-radius: 30px;
  box-shadow: 0 4px 20px rgba(0, 200, 83, 0.4);
  opacity: 0;
  pointer-events: none;
  transition: all 0.3s cubic-bezier(0.18, 0.89, 0.32, 1.28);
  z-index: 1000;
}
.toast.show {
  opacity: 1;
  transform: translateX(-50%) translateY(0);
}
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="title-group">
      <h1>🎮 PS5 Telemetry Overlay</h1>
      <p>Панель управления и настройка отображения оверлея</p>
    </div>
    <div class="status-pill">
      <span class="status-dot"></span>
      <span>PS5 ShellUI (v1.0.19)</span>
    </div>
  </div>

  <!-- Live Screen Preview -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">🖥️ Предпросмотр на экране PS5</div>
      <div style="font-size: 13px; color: var(--text-dim);">Масштаб 16:9</div>
    </div>
    <div class="preview-container" id="previewContainer">
      <div class="preview-screen-label">PLAYSTATION 5</div>
      <div class="preview-overlay-bar" id="previewBar"></div>
    </div>
  </div>

  <!-- Telemetry Metrics & Order -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">📊 Данные телеметрии и их порядок</div>
      <div style="font-size: 13px; color: var(--text-dim);">Используйте ▲ / ▼ для сортировки</div>
    </div>
    <div class="metrics-list" id="metricsList"></div>
  </div>

  <!-- Screen Position -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">📍 Расположение на экране</div>
    </div>
    <div class="grid-2">
      <div class="form-group">
        <label class="form-label">По вертикали</label>
        <div class="segment-group" id="posVSegment">
          <button type="button" class="segment-btn active" data-val="0">Сверху</button>
          <button type="button" class="segment-btn" data-val="1">Снизу</button>
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">По горизонтали</label>
        <div class="segment-group" id="posHSegment">
          <button type="button" class="segment-btn active" data-val="0">Слева</button>
          <button type="button" class="segment-btn" data-val="1">По центру</button>
          <button type="button" class="segment-btn" data-val="2">Справа</button>
        </div>
      </div>
      <div class="form-group">
        <div class="form-label">
          <span>Смещение по X</span>
          <span class="form-val" id="valOffsetX">24 px</span>
        </div>
        <input type="range" id="inputOffsetX" min="0" max="400" step="4" value="24">
      </div>
      <div class="form-group">
        <div class="form-label">
          <span>Смещение по Y</span>
          <span class="form-val" id="valOffsetY">0 px</span>
        </div>
        <input type="range" id="inputOffsetY" min="0" max="200" step="2" value="0">
      </div>
    </div>
  </div>

  <!-- Appearance & Styling -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">🎨 Внешний вид и размер</div>
    </div>
    <div class="grid-2">
      <div class="form-group">
        <div class="form-label">
          <span>Размер шрифта</span>
          <span class="form-val" id="valFontSize">18 pt</span>
        </div>
        <input type="range" id="inputFontSize" min="12" max="32" step="1" value="18">
      </div>
      <div class="form-group">
        <div class="form-label">
          <span>Высота плашки</span>
          <span class="form-val" id="valBarHeight">34 px</span>
        </div>
        <input type="range" id="inputBarHeight" min="24" max="56" step="2" value="34">
      </div>
      <div class="form-group">
        <div class="form-label">
          <span>Прозрачность фона</span>
          <span class="form-val" id="valBgOpacity">70%</span>
        </div>
        <input type="range" id="inputBgOpacity" min="0" max="100" step="5" value="70">
      </div>
      <div class="form-group">
        <label class="form-label">Цвет фоновой плашки</label>
        <div class="color-input-wrapper">
          <input type="color" id="inputBgColor" value="#000000">
          <input type="text" class="color-hex" id="inputBgColorHex" value="#000000" maxlength="7">
          <label class="switch" style="margin-left: auto;">
            <input type="checkbox" id="inputBgVisible" checked>
            <span class="slider"></span>
          </label>
        </div>
      </div>
    </div>
  </div>

  <!-- Unload / Exit Overlay Card -->
  <div class="card" style="border-color: rgba(255, 75, 75, 0.35); background: linear-gradient(180deg, #1c151b 0%, #151b26 100%);">
    <div class="card-header">
      <div class="card-title" style="color: #ff5252;">🛑 Завершение и выгрузка оверлея</div>
    </div>
    <p style="font-size: 14px; color: var(--text-dim); line-height: 1.5; margin-bottom: 16px;">
      Полностью выгружает модуль оверлея из памяти SceShellUI, удаляет HUD с экрана и освобождает сетевой порт. После выгрузки можно запустить новую версию ELF через Netcat без перезагрузки консоли.
    </p>
    <button type="button" class="btn-unload" id="btnUnload" onclick="unloadOverlay()">
      ⚠️ Выгрузить оверлей из SceShellUI
    </button>
  </div>

  <!-- Action Bar -->
  <div class="action-bar">
    <button class="btn-save" id="btnSave">💾 Сохранить и применить на PS5</button>
    <button class="btn-reset" id="btnReset">↺ По умолчанию</button>
  </div>
</div>

<div class="toast" id="toast">Настройки успешно применены!</div>

<script>
const METRIC_DEFS = {
  fps:  { name: 'FPS',  desc: 'Частота кадров в секунду (Framerate)', demoVal: '120' },
  cpu:  { name: 'CPU',  desc: 'Температура процессора (Zen 2 Tctl)', demoVal: '62°C' },
  load: { name: 'LOAD', desc: 'Общая загрузка процессора (CPU %)', demoVal: '28%' },
  cclk: { name: 'CCLK', desc: 'Частота ядер процессора (Zen 2 CPU Clock)', demoVal: '3500 MHz' },
  gpu:  { name: 'GPU',  desc: 'Температура видеочипа (SoC / GPU)', demoVal: '66°C' },
  gclk: { name: 'GCLK', desc: 'Частота видеоядра (RDNA 2 GFXCLK)', demoVal: '2233 MHz' },
  ram:  { name: 'RAM',  desc: 'Использование системной памяти', demoVal: '4.8/16 GB' },
  vram: { name: 'VRAM', desc: 'Использование видеопамяти (VRAM)', demoVal: '3.4 GB' },
  ssd:  { name: 'SSD',  desc: 'Температура накопителя M.2 NVMe SSD', demoVal: '44°C' },
  fan:  { name: 'FAN',  desc: 'Скорость вращения кулера консоли', demoVal: '38%' },
  pwr:  { name: 'PWR',  desc: 'Энергопотребление SoC APU в Ваттах', demoVal: '185 W' },
  ip:   { name: 'IP',   desc: 'Локальный IP-адрес консоли', demoVal: '192.168.1.105' }
};

let currentConfig = {
  enabled: true,
  pos_v: 0,
  pos_h: 0,
  offset_x: 24,
  offset_y: 0,
  font_size: 18,
  bar_height: 34,
  bg_visible: true,
  bg_color: '#000000',
  bg_opacity: 0.70,
  order: ['fps', 'cpu', 'load', 'cclk', 'gpu', 'gclk', 'ram', 'vram', 'ssd', 'fan', 'pwr', 'ip'],
  metrics: {
    fps:  { enabled: true, color: '#FFEB3B' },
    cpu:  { enabled: true, color: '#66FF66' },
    load: { enabled: true, color: '#FFA726' },
    cclk: { enabled: true, color: '#80D8FF' },
    gpu:  { enabled: true, color: '#B366FF' },
    gclk: { enabled: true, color: '#EA80FC' },
    ram:  { enabled: true, color: '#FFB34D' },
    vram: { enabled: true, color: '#E040FB' },
    ssd:  { enabled: true, color: '#00E5FF' },
    fan:  { enabled: true, color: '#33E0FF' },
    pwr:  { enabled: true, color: '#FF5252' },
    ip:   { enabled: true, color: '#76FF03' }
  }
};

function renderMetricsList() {
  const container = document.getElementById('metricsList');
  container.innerHTML = '';

  currentConfig.order.forEach((key, index) => {
    const def = METRIC_DEFS[key];
    const mCfg = currentConfig.metrics[key];
    const row = document.createElement('div');
    row.className = 'metric-row';
    row.innerHTML = `
      <div class="metric-left">
        <div class="order-badge">#${index + 1}</div>
        <div class="metric-info">
          <h4>${def.name}</h4>
          <span>${def.desc}</span>
        </div>
      </div>
      <div class="metric-right">
        <div class="color-input-wrapper">
          <input type="color" data-key="${key}" value="${mCfg.color}">
          <input type="text" class="color-hex" data-key="${key}" value="${mCfg.color}" maxlength="7">
        </div>
        <button class="btn-reorder" data-dir="up" data-idx="${index}" ${index === 0 ? 'disabled' : ''}>▲</button>
        <button class="btn-reorder" data-dir="down" data-idx="${index}" ${index === currentConfig.order.length - 1 ? 'disabled' : ''}>▼</button>
        <label class="switch">
          <input type="checkbox" data-key="${key}" ${mCfg.enabled ? 'checked' : ''}>
          <span class="slider"></span>
        </label>
      </div>
    `;
    container.appendChild(row);
  });

  // Attach event handlers
  container.querySelectorAll('input[type="checkbox"]').forEach(cb => {
    cb.addEventListener('change', (e) => {
      currentConfig.metrics[e.target.dataset.key].enabled = e.target.checked;
      updatePreview();
    });
  });

  container.querySelectorAll('input[type="color"]').forEach(cp => {
    cp.addEventListener('input', (e) => {
      const key = e.target.dataset.key;
      const hex = e.target.value.toUpperCase();
      currentConfig.metrics[key].color = hex;
      container.querySelector(`input.color-hex[data-key="${key}"]`).value = hex;
      updatePreview();
    });
  });

  container.querySelectorAll('input.color-hex').forEach(inp => {
    inp.addEventListener('input', (e) => {
      const key = e.target.dataset.key;
      let val = e.target.value.trim();
      if (!val.startsWith('#')) val = '#' + val;
      if (/^#[0-9A-Fa-f]{6}$/.test(val)) {
        currentConfig.metrics[key].color = val.toUpperCase();
        container.querySelector(`input[type="color"][data-key="${key}"]`).value = val;
        updatePreview();
      }
    });
  });

  container.querySelectorAll('.btn-reorder').forEach(btn => {
    btn.addEventListener('click', (e) => {
      const dir = e.currentTarget.dataset.dir;
      const idx = parseInt(e.currentTarget.dataset.idx, 10);
      const targetIdx = dir === 'up' ? idx - 1 : idx + 1;
      if (targetIdx >= 0 && targetIdx < currentConfig.order.length) {
        const tmp = currentConfig.order[idx];
        currentConfig.order[idx] = currentConfig.order[targetIdx];
        currentConfig.order[targetIdx] = tmp;
        renderMetricsList();
        updatePreview();
      }
    });
  });
}

function updatePreview() {
  const bar = document.getElementById('previewBar');
  const preview = document.getElementById('previewContainer');
  const pw = preview.clientWidth;
  const ph = preview.clientHeight;
  const scale = pw / 1920.0;

  // Background styling
  const r = parseInt(currentConfig.bg_color.slice(1, 3), 16) || 0;
  const g = parseInt(currentConfig.bg_color.slice(3, 5), 16) || 0;
  const b = parseInt(currentConfig.bg_color.slice(5, 7), 16) || 0;
  bar.style.backgroundColor = currentConfig.bg_visible
    ? `rgba(${r}, ${g}, ${b}, ${currentConfig.bg_opacity})`
    : 'transparent';

  const scaledH = Math.max(16, currentConfig.bar_height * scale);
  bar.style.height = `${scaledH}px`;

  // Vertical position
  if (currentConfig.pos_v === 1) {
    bar.style.top = 'auto';
    bar.style.bottom = `${currentConfig.offset_y * scale}px`;
  } else {
    bar.style.bottom = 'auto';
    bar.style.top = `${currentConfig.offset_y * scale}px`;
  }

  // Horizontal alignment
  const scaledX = currentConfig.offset_x * scale;
  if (currentConfig.pos_h === 1) {
    bar.style.justifyContent = 'center';
    bar.style.paddingLeft = '0px';
    bar.style.paddingRight = '0px';
  } else if (currentConfig.pos_h === 2) {
    bar.style.justifyContent = 'flex-end';
    bar.style.paddingLeft = '0px';
    bar.style.paddingRight = `${scaledX}px`;
  } else {
    bar.style.justifyContent = 'flex-start';
    bar.style.paddingLeft = `${scaledX}px`;
    bar.style.paddingRight = '0px';
  }

  const fontSize = Math.max(8, currentConfig.font_size * scale);
  bar.style.fontSize = `${fontSize}px`;

  // Render items
  bar.innerHTML = '';
  const enabledItems = currentConfig.order.filter(k => currentConfig.metrics[k].enabled);
  enabledItems.forEach((key, idx) => {
    const def = METRIC_DEFS[key];
    const mCfg = currentConfig.metrics[key];
    const el = document.createElement('span');
    el.className = 'preview-metric';
    el.innerHTML = `<span style="color:${mCfg.color}">${def.name}</span><span class="preview-metric-val">${def.demoVal}</span>`;
    bar.appendChild(el);

    if (idx < enabledItems.length - 1) {
      const sep = document.createElement('span');
      sep.className = 'preview-sep';
      sep.textContent = '|';
      bar.appendChild(sep);
    }
  });
}

function syncInputsFromConfig() {
  document.querySelectorAll('#posVSegment .segment-btn').forEach(btn => {
    btn.classList.toggle('active', parseInt(btn.dataset.val, 10) === currentConfig.pos_v);
  });
  document.querySelectorAll('#posHSegment .segment-btn').forEach(btn => {
    btn.classList.toggle('active', parseInt(btn.dataset.val, 10) === currentConfig.pos_h);
  });

  document.getElementById('inputOffsetX').value = currentConfig.offset_x;
  document.getElementById('valOffsetX').textContent = `${currentConfig.offset_x} px`;
  document.getElementById('inputOffsetY').value = currentConfig.offset_y;
  document.getElementById('valOffsetY').textContent = `${currentConfig.offset_y} px`;

  document.getElementById('inputFontSize').value = currentConfig.font_size;
  document.getElementById('valFontSize').textContent = `${currentConfig.font_size} pt`;
  document.getElementById('inputBarHeight').value = currentConfig.bar_height;
  document.getElementById('valBarHeight').textContent = `${currentConfig.bar_height} px`;

  document.getElementById('inputBgOpacity').value = Math.round(currentConfig.bg_opacity * 100);
  document.getElementById('valBgOpacity').textContent = `${Math.round(currentConfig.bg_opacity * 100)}%`;

  document.getElementById('inputBgColor').value = currentConfig.bg_color;
  document.getElementById('inputBgColorHex').value = currentConfig.bg_color.toUpperCase();
  document.getElementById('inputBgVisible').checked = currentConfig.bg_visible;

  renderMetricsList();
  updatePreview();
}

function bindControls() {
  document.querySelectorAll('#posVSegment .segment-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      currentConfig.pos_v = parseInt(btn.dataset.val, 10);
      syncInputsFromConfig();
    });
  });

  document.querySelectorAll('#posHSegment .segment-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      currentConfig.pos_h = parseInt(btn.dataset.val, 10);
      syncInputsFromConfig();
    });
  });

  document.getElementById('inputOffsetX').addEventListener('input', (e) => {
    currentConfig.offset_x = parseFloat(e.target.value);
    document.getElementById('valOffsetX').textContent = `${currentConfig.offset_x} px`;
    updatePreview();
  });

  document.getElementById('inputOffsetY').addEventListener('input', (e) => {
    currentConfig.offset_y = parseFloat(e.target.value);
    document.getElementById('valOffsetY').textContent = `${currentConfig.offset_y} px`;
    updatePreview();
  });

  document.getElementById('inputFontSize').addEventListener('input', (e) => {
    currentConfig.font_size = parseInt(e.target.value, 10);
    document.getElementById('valFontSize').textContent = `${currentConfig.font_size} pt`;
    updatePreview();
  });

  document.getElementById('inputBarHeight').addEventListener('input', (e) => {
    currentConfig.bar_height = parseFloat(e.target.value);
    document.getElementById('valBarHeight').textContent = `${currentConfig.bar_height} px`;
    updatePreview();
  });

  document.getElementById('inputBgOpacity').addEventListener('input', (e) => {
    currentConfig.bg_opacity = parseFloat(e.target.value) / 100.0;
    document.getElementById('valBgOpacity').textContent = `${e.target.value}%`;
    updatePreview();
  });

  document.getElementById('inputBgColor').addEventListener('input', (e) => {
    currentConfig.bg_color = e.target.value.toUpperCase();
    document.getElementById('inputBgColorHex').value = currentConfig.bg_color;
    updatePreview();
  });
  document.getElementById('inputBgColorHex').addEventListener('input', (e) => {
    let val = e.target.value.trim();
    if (!val.startsWith('#')) val = '#' + val;
    if (/^#[0-9A-Fa-f]{6}$/.test(val)) {
      currentConfig.bg_color = val.toUpperCase();
      document.getElementById('inputBgColor').value = val;
      updatePreview();
    }
  });

  document.getElementById('inputBgVisible').addEventListener('change', (e) => {
    currentConfig.bg_visible = e.target.checked;
    updatePreview();
  });

  window.addEventListener('resize', updatePreview);
  document.getElementById('btnSave').addEventListener('click', saveConfigToServer);
  document.getElementById('btnReset').addEventListener('click', resetConfig);
}

function showToast(msg) {
  const toast = document.getElementById('toast');
  toast.textContent = msg;
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2800);
}

async function loadConfigFromServer() {
  try {
    const res = await fetch('/api/config');
    if (res.ok) {
      const data = await res.json();
      currentConfig = data;
    }
  } catch (err) {
    console.log('Running in preview mode');
  }
  syncInputsFromConfig();
}

async function saveConfigToServer() {
  const btn = document.getElementById('btnSave');
  if (btn && btn.disabled) return;
  if (btn) {
    btn.disabled = true;
    btn.textContent = '⏳ Сохранение...';
  }
  try {
    const res = await fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(currentConfig)
    });
    if (res.ok) {
      showToast('Настройки успешно применены на PS5!');
    } else {
      showToast('Ошибка сохранения: ' + res.status);
    }
  } catch (err) {
    showToast('Ошибка сети при сохранении');
  } finally {
    if (btn) {
      setTimeout(() => {
        btn.disabled = false;
        btn.textContent = '💾 Сохранить и применить на PS5';
      }, 600);
    }
  }
}

async function resetConfig() {
  if (!confirm('Сбросить все настройки оверлея по умолчанию?')) return;
  try {
    const res = await fetch('/api/reset', { method: 'POST' });
    if (res.ok) {
      await loadConfigFromServer();
      showToast('Настройки сброшены по умолчанию!');
      return;
    }
  } catch (err) {}
  currentConfig = {
    enabled: true,
    pos_v: 0,
    pos_h: 0,
    offset_x: 24,
    offset_y: 0,
    font_size: 18,
    bar_height: 34,
    bg_visible: true,
    bg_color: '#000000',
    bg_opacity: 0.70,
    order: ['fps', 'cpu', 'gpu', 'ram', 'fan'],
    metrics: {
      fps: { enabled: true, color: '#FFEB3B' },
      cpu: { enabled: true, color: '#66FF66' },
      gpu: { enabled: true, color: '#B366FF' },
      ram: { enabled: true, color: '#FFB34D' },
      fan: { enabled: true, color: '#33E0FF' }
    }
  };
  syncInputsFromConfig();
  showToast('Сброшено по умолчанию');
}

async function unloadOverlay() {
  if (!confirm('Вы уверены, что хотите завершить и выгрузить оверлей из памяти SceShellUI?\n\nHUD исчезнет с экрана, а сетевой порт освободится для загрузки нового ELF.')) return;
  const btn = document.getElementById('btnUnload');
  if (btn) {
    btn.disabled = true;
    btn.textContent = '⏳ Выгрузка...';
  }
  try {
    await fetch('/api/unload', { method: 'POST' });
    showToast('Оверлей успешно выгружен!');
    setTimeout(() => {
      document.body.innerHTML = '<div style="display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;color:#fff;text-align:center;padding:24px;font-family:sans-serif;">' +
        '<div style="font-size:48px;margin-bottom:16px;">🛑</div>' +
        '<h1 style="color:#00e676;margin-bottom:12px;font-size:24px;">Оверлей успешно выгружен</h1>' +
        '<p style="color:#8b9bb4;max-width:540px;font-size:16px;line-height:1.5;">Модуль оверлея завершил работу в SceShellUI. Память очищена, порт освобожден.<br><br>Теперь можно отправить новую версию ELF через Netcat / Payload Sender без перезагрузки консоли.</p>' +
        '</div>';
    }, 1000);
  } catch (err) {
    showToast('Запрос на выгрузку отправлен!');
  }
}

window.addEventListener('DOMContentLoaded', () => {
  bindControls();
  loadConfigFromServer();
});
</script>
</body>
</html>
)rawhtml";

// ---------------------------------------------------------------------------
// Socket helper: write all bytes
// ---------------------------------------------------------------------------
static void socket_send_all(long fd, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        long written = __syscall(SYS_write, fd, data + total, len - total);
        if (written <= 0) break;
        total += (size_t)written;
    }
}

// ---------------------------------------------------------------------------
// Lightweight JSON helper routines
// ---------------------------------------------------------------------------
static void serialize_config_json(const OverlayConfig* cfg, char* out, size_t max_len) {
    if (!cfg || !out || max_len == 0) return;
    int len = snprintf(out, max_len,
        "{"
        "\"enabled\":%s,"
        "\"pos_v\":%d,"
        "\"pos_h\":%d,"
        "\"offset_x\":%.1f,"
        "\"offset_y\":%.1f,"
        "\"font_size\":%d,"
        "\"bar_height\":%.1f,"
        "\"bg_visible\":%s,"
        "\"bg_color\":\"%s\","
        "\"bg_opacity\":%.2f,"
        "\"order\":[",
        cfg->enabled ? "true" : "false",
        cfg->position_vertical,
        cfg->position_horizontal,
        cfg->offset_x,
        cfg->offset_y,
        cfg->font_size,
        cfg->bar_height,
        cfg->bg_visible ? "true" : "false",
        cfg->bg_hex_color,
        cfg->bg_opacity
    );

    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)cfg->order[i];
        if (mt < 0 || mt >= METRIC_COUNT) continue;
        len += snprintf(out + len, max_len - len, "%s\"%s\"", (i > 0) ? "," : "", metric_type_to_str(mt));
    }

    len += snprintf(out + len, max_len - len, "],\"metrics\":{");
    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)i;
        len += snprintf(out + len, max_len - len,
            "%s\"%s\":{\"enabled\":%s,\"color\":\"%s\"}",
            (i > 0) ? "," : "",
            metric_type_to_str(mt),
            cfg->metrics[mt].enabled ? "true" : "false",
            cfg->metrics[mt].hex_color
        );
    }
    len += snprintf(out + len, max_len - len, "}}");
}

static const char* json_find_key(const char* json, const char* key) {
    if (!json || !key) return nullptr;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t pat_len = strlen(pat);
    const char* p = json;
    while ((p = strstr(p, pat)) != nullptr) {
        const char* val = p + pat_len;
        while (*val == ' ' || *val == '\t' || *val == '\r' || *val == '\n') val++;
        if (*val == ':') {
            val++;
            while (*val == ' ' || *val == '\t' || *val == '\r' || *val == '\n') val++;
            return val;
        }
        // Not a JSON key (might be an array string element), continue search
        p += pat_len;
    }
    return nullptr;
}

static bool json_get_bool(const char* json, const char* key, bool def) {
    const char* p = json_find_key(json, key);
    if (!p) return def;
    if (strncmp(p, "true", 4) == 0 || *p == '1') return true;
    if (strncmp(p, "false", 5) == 0 || *p == '0') return false;
    return def;
}

static int json_get_int(const char* json, const char* key, int def) {
    const char* p = json_find_key(json, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return def;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    int val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    return neg ? -val : val;
}

static float json_get_float(const char* json, const char* key, float def) {
    const char* p = json_find_key(json, key);
    if (!p) return def;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return def;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    float val = 0.0f;
    float fact = 1.0f;
    bool point = false;
    while (*p) {
        if (*p == '.') { point = true; p++; continue; }
        if (*p >= '0' && *p <= '9') {
            if (point) fact /= 10.0f;
            val = val * 10.0f + (float)(*p - '0');
            p++;
        } else {
            break;
        }
    }
    val *= fact;
    return neg ? -val : val;
}

static bool json_get_string(const char* json, const char* key, char* out, size_t max_len) {
    const char* p = json_find_key(json, key);
    if (!p || *p != '"') return false;
    p++; // Skip opening quote
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < max_len) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static void parse_config_json(const char* json, OverlayConfig* cfg) {
    if (!json || !cfg) return;

    cfg->enabled = json_get_bool(json, "enabled", cfg->enabled);
    cfg->position_vertical = json_get_int(json, "pos_v", cfg->position_vertical);
    cfg->position_horizontal = json_get_int(json, "pos_h", cfg->position_horizontal);
    cfg->offset_x = json_get_float(json, "offset_x", cfg->offset_x);
    cfg->offset_y = json_get_float(json, "offset_y", cfg->offset_y);

    int fs = json_get_int(json, "font_size", cfg->font_size);
    if (fs >= 10 && fs <= 48) cfg->font_size = fs;

    float bh = json_get_float(json, "bar_height", cfg->bar_height);
    if (bh >= 20.0f && bh <= 80.0f) cfg->bar_height = bh;

    cfg->bg_visible = json_get_bool(json, "bg_visible", cfg->bg_visible);

    char hex[16];
    if (json_get_string(json, "bg_color", hex, sizeof(hex)) && hex[0] == '#' && strlen(hex) >= 7) {
        strncpy(cfg->bg_hex_color, hex, 7);
        cfg->bg_hex_color[7] = '\0';
        hex_to_rgb(cfg->bg_hex_color, &cfg->bg_r, &cfg->bg_g, &cfg->bg_b);
    }

    float op = json_get_float(json, "bg_opacity", cfg->bg_opacity);
    if (op >= 0.0f && op <= 1.0f) cfg->bg_opacity = op;

    // Parse order array: "order": ["fps", "cpu", ...]
    const char* o_ptr = json_find_key(json, "order");
    if (o_ptr && *o_ptr == '[') {
        o_ptr++;
        int idx = 0;
        bool seen[METRIC_COUNT] = {false};
        while (*o_ptr && *o_ptr != ']' && idx < METRIC_COUNT) {
            while (*o_ptr == ' ' || *o_ptr == '\t' || *o_ptr == ',' || *o_ptr == '\r' || *o_ptr == '\n') o_ptr++;
            if (*o_ptr == '"') {
                o_ptr++;
                char name[16] = {0};
                int ni = 0;
                while (*o_ptr && *o_ptr != '"' && ni < 15) {
                    name[ni++] = *o_ptr++;
                }
                if (*o_ptr == '"') o_ptr++;
                MetricType mt = str_to_metric_type(name);
                if (!seen[mt]) {
                    cfg->order[idx++] = (int)mt;
                    seen[mt] = true;
                }
            } else {
                break;
            }
        }
        for (int m = 0; m < METRIC_COUNT && idx < METRIC_COUNT; m++) {
            if (!seen[m]) {
                cfg->order[idx++] = m;
                seen[m] = true;
            }
        }
    }

    // Parse each metric from "metrics": { "fps": { "enabled": true, "color": "#..." }, ... }
    const char* m_root = json_find_key(json, "metrics");
    const char* search_in = m_root ? m_root : json;

    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricType mt = (MetricType)i;
        const char* key_name = metric_type_to_str(mt);
        const char* m_ptr = json_find_key(search_in, key_name);
        if (m_ptr && *m_ptr == '{') {
            const char* end_obj = strchr(m_ptr, '}');
            if (end_obj) {
                // Copy metric sub-object into buffer
                char sub[256];
                size_t sub_len = (size_t)(end_obj - m_ptr + 1);
                if (sub_len < sizeof(sub)) {
                    memcpy(sub, m_ptr, sub_len);
                    sub[sub_len] = '\0';

                    cfg->metrics[mt].enabled = json_get_bool(sub, "enabled", cfg->metrics[mt].enabled);
                    LOG_INFO("Parsed metric [%s]: enabled=%d", key_name, cfg->metrics[mt].enabled ? 1 : 0);

                    char col[16];
                    if (json_get_string(sub, "color", col, sizeof(col)) && col[0] == '#' && strlen(col) >= 7) {
                        strncpy(cfg->metrics[mt].hex_color, col, 7);
                        cfg->metrics[mt].hex_color[7] = '\0';
                        hex_to_rgb(cfg->metrics[mt].hex_color, &cfg->metrics[mt].r, &cfg->metrics[mt].g, &cfg->metrics[mt].b);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// HTTP Request Dispatcher
// ---------------------------------------------------------------------------
static void handle_client_http(long client_fd, const char* req, size_t req_len) {
    (void)req_len;

    // Check request method and path
    if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html", 15) == 0) {
        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            sizeof(s_html_page) - 1);
        socket_send_all(client_fd, hdr, hlen);
        socket_send_all(client_fd, s_html_page, sizeof(s_html_page) - 1);
        return;
    }

    if (strncmp(req, "GET /api/config", 15) == 0) {
        char json_buf[2048];
        serialize_config_json(&g_config, json_buf, sizeof(json_buf));
        size_t jlen = strlen(json_buf);

        char hdr[256];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            jlen);
        socket_send_all(client_fd, hdr, hlen);
        socket_send_all(client_fd, json_buf, jlen);
        return;
    }

    if (strncmp(req, "POST /api/config", 16) == 0) {
        const char* body = strstr(req, "\r\n\r\n");
        if (body) {
            body += 4;
            OverlayConfig new_cfg = g_config;
            parse_config_json(body, &new_cfg);
            config_save(&new_cfg, "/data/overlay/config.ini");
            g_config = new_cfg;
            g_config_changed = true;
            LOG_INFO("Config updated via HTTP Web Settings and marked dirty!");
        }

        const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}";
        socket_send_all(client_fd, resp, sizeof(resp) - 1);
        return;
    }

    if (strncmp(req, "POST /api/reset", 15) == 0) {
        OverlayConfig new_cfg;
        config_set_defaults(&new_cfg);
        config_save(&new_cfg, "/data/overlay/config.ini");
        g_config = new_cfg;
        g_config_changed = true;
        LOG_INFO("Config reset to defaults via HTTP Web Settings!");

        const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}";
        socket_send_all(client_fd, resp, sizeof(resp) - 1);
        return;
    }

    if (strncmp(req, "POST /api/unload", 16) == 0) {
        const char resp[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n{\"status\":\"unloaded\"}";
        socket_send_all(client_fd, resp, sizeof(resp) - 1);
        __syscall(SYS_close, client_fd);
        LOG_INFO("Unload requested via Web UI. Setting g_should_unload = true...");
        g_should_unload = true;
        return;
    }

    if (strncmp(req, "GET /favicon.ico", 16) == 0) {
        const char resp[] = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
        socket_send_all(client_fd, resp, sizeof(resp) - 1);
        return;
    }

    // Default 404
    const char resp[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nNot Found\n";
    socket_send_all(client_fd, resp, sizeof(resp) - 1);
}

// ---------------------------------------------------------------------------
// Worker thread entry point
// ---------------------------------------------------------------------------
static void* web_server_worker(void* arg) {
    int requested_port = (int)(intptr_t)arg;
    LOG_INFO("Web server worker started. Attempting to bind port %d...", requested_port);

    int candidate_ports[] = { requested_port, 8585, 8282, 8989, 9876 };
    int num_candidates = sizeof(candidate_ports) / sizeof(candidate_ports[0]);
    int actual_port = 0;

    for (int i = 0; i < num_candidates; i++) {
        int try_port = candidate_ports[i];
        if (i > 0 && try_port == requested_port) continue;

        long s = __syscall(SYS_socket, AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            LOG_ERROR("Failed to create socket: %ld", s);
            return nullptr;
        }

        int opt = 1;
        __syscall(SYS_setsockopt, s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct bsd_sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_len = sizeof(addr);
        addr.sin_family = AF_INET;
        addr.sin_port = (uint16_t)(((try_port & 0xFF) << 8) | ((try_port >> 8) & 0xFF)); // htons
        addr.sin_addr = 0; // INADDR_ANY (0.0.0.0)

        long br = __syscall(SYS_bind, s, &addr, sizeof(addr));
        if (br == 0) {
            long lr = __syscall(SYS_listen, s, 10);
            if (lr == 0) {
                g_server_fd = s;
                actual_port = try_port;
                LOG_INFO("Web Server listening on http://0.0.0.0:%d", actual_port);
                break;
            }
        } else {
            LOG_WARN("Port %d is busy/unavailable (code %ld). Trying next candidate...", try_port, br);
        }
        __syscall(SYS_close, s);
    }

    if (actual_port == 0 || g_server_fd < 0) {
        LOG_ERROR("Failed to bind web server to any port!");
        return nullptr;
    }
    g_web_running = true;

    while (g_web_running) {
        struct bsd_sockaddr_in client_addr;
        uint32_t client_len = sizeof(client_addr);
        long cfd = __syscall(SYS_accept, g_server_fd, &client_addr, &client_len);
        if (cfd < 0) {
            usleep(20000);
            continue;
        }

        // Set 3-second receive timeout on client socket
        struct bsd_timeval tv = { 3, 0 };
        __syscall(SYS_setsockopt, cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char req_buf[8192];
        size_t total_read = 0;
        int content_len = -1;
        const char* body_start = nullptr;

        while (total_read < sizeof(req_buf) - 1) {
            long n = __syscall(SYS_read, cfd, req_buf + total_read, sizeof(req_buf) - 1 - total_read);
            if (n <= 0) break;
            total_read += n;
            req_buf[total_read] = '\0';

            if (!body_start) {
                body_start = strstr(req_buf, "\r\n\r\n");
                if (body_start) {
                    body_start += 4;
                    // Find Content-Length case-insensitively
                    const char* p = req_buf;
                    while (p && p < body_start) {
                        if (strncasecmp(p, "Content-Length:", 15) == 0) {
                            p += 15;
                            while (*p == ' ' || *p == '\t') p++;
                            content_len = atoi(p);
                            break;
                        }
                        p = strchr(p, '\n');
                        if (p) p++;
                    }
                    if (content_len < 0) {
                        // No Content-Length (e.g. GET request)
                        content_len = 0;
                    }
                }
            }

            if (body_start && content_len >= 0) {
                size_t body_received = total_read - (size_t)(body_start - req_buf);
                if (body_received >= (size_t)content_len) {
                    break;
                }
            }
        }

        if (total_read > 0) {
            handle_client_http(cfd, req_buf, total_read);
        }

        __syscall(SYS_close, cfd);
    }

    if (g_server_fd >= 0) {
        __syscall(SYS_close, g_server_fd);
        g_server_fd = -1;
    }
    LOG_INFO("Web Server worker terminated.");
    return nullptr;
}


 // ---------------------------------------------------------------------------
 // Public API - Web Server Disabled
 // ---------------------------------------------------------------------------
bool web_server_start(int port, uint64_t pthread_create_addr) {
    (void)port;
    (void)pthread_create_addr;

    // Completely disable the web server.
    // No threads will be created and no ports will be opened.
    g_web_running = false;

    if (g_server_fd >= 0) {
        __syscall(SYS_close, g_server_fd);
        g_server_fd = -1;
    }

    g_web_thread = nullptr;

    LOG_INFO("Web Server disabled. No ports will be opened.");
    return false;
}

void web_server_stop(void) {
    g_web_running = false;

    if (g_server_fd >= 0) {
        __syscall(SYS_close, g_server_fd);
        g_server_fd = -1;
    }

    LOG_INFO("Web Server stopped.");
}
