<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>SID Player Pico Pinouts</title>
  <style>
    :root {
      --bg: #111827;
      --bg-alt: #020617;
      --card: #0b1120;
      --border: #1f2937;
      --text: #e5e7eb;
      --muted: #9ca3af;
      --accent: #38bdf8;
      --table-header: #111827;
      --table-row-alt: #020617;
      --code-bg: #020617;
      --radius: 8px;
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      padding: 0;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: radial-gradient(circle at top, #1f2937 0, #020617 55%, #000 100%);
      color: var(--text);
    }

    .page {
      max-width: 960px;
      margin: 40px auto;
      padding: 0 16px 40px;
    }

    .title {
      font-size: 1.8rem;
      font-weight: 600;
      margin-bottom: 8px;
    }

    .subtitle {
      font-size: 0.95rem;
      color: var(--muted);
      margin-bottom: 24px;
    }

    .card {
      background: linear-gradient(145deg, var(--card), var(--bg-alt));
      border-radius: 16px;
      border: 1px solid var(--border);
      padding: 20px 22px 24px;
      box-shadow:
        0 18px 40px rgba(0, 0, 0, 0.7),
        0 0 0 1px rgba(15, 23, 42, 0.9);
    }

    .card-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 18px;
    }

    .card-title {
      font-size: 1.1rem;
      font-weight: 600;
    }

    .card-note {
      font-size: 0.85rem;
      color: var(--muted);
    }

    .section {
      margin-top: 18px;
      padding-top: 16px;
      border-top: 1px solid rgba(31, 41, 55, 0.8);
    }

    .section:first-of-type {
      border-top: none;
      padding-top: 0;
      margin-top: 0;
    }

    .section-title {
      font-size: 0.95rem;
      font-weight: 600;
      color: var(--accent);
      margin-bottom: 8px;
    }

    .section-subtitle {
      font-size: 0.85rem;
      color: var(--muted);
      margin-bottom: 10px;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.85rem;
      border-radius: var(--radius);
      overflow: hidden;
      border: 1px solid var(--border);
      background: rgba(15, 23, 42, 0.9);
    }

    thead {
      background: var(--table-header);
    }

    th,
    td {
      padding: 8px 10px;
      text-align: left;
      border-bottom: 1px solid rgba(31, 41, 55, 0.9);
    }

    th {
      font-weight: 600;
      color: var(--muted);
      font-size: 0.8rem;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }

    tbody tr:nth-child(even) {
      background: var(--table-row-alt);
    }

    tbody tr:last-child td {
      border-bottom: none;
    }

    code {
      font-family: "JetBrains Mono", ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
      font-size: 0.8rem;
      background: var(--code-bg);
      padding: 2px 4px;
      border-radius: 4px;
      border: 1px solid rgba(55, 65, 81, 0.8);
    }

    .pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 4px 10px;
      border-radius: 999px;
      background: rgba(15, 118, 110, 0.15);
      border: 1px solid rgba(45, 212, 191, 0.4);
      color: #a5f3fc;
      font-size: 0.75rem;
    }

    .pill-dot {
      width: 7px;
      height: 7px;
      border-radius: 999px;
      background: #22c55e;
      box-shadow: 0 0 8px rgba(34, 197, 94, 0.9);
    }

    .footer-note {
      margin-top: 18px;
      font-size: 0.8rem;
      color: var(--muted);
    }
  </style>
</head>
<body>
  <div class="page">
    <div class="title">SID Player Pico – Pinout Reference</div>
    <div class="subtitle">
      Here are all the pinouts pulled directly from the code.
    </div>

    <div class="card">
      <div class="card-header">
        <div class="card-title">Hardware mapping overview</div>
        <div class="pill">
          <span class="pill-dot"></span>
          <span>Raspberry Pi Pico 2</span>
        </div>
      </div>
      <div class="card-note">
        SID bus, SD card, TFT display, and button wiring for the SIDKick Pico build.
      </div>

      <!-- SID Bus -->
      <div class="section">
        <div class="section-title">SID Bus → SIDKick Pico</div>
        <div class="section-subtitle">
          Primary data, address, and control lines for the SID-compatible interface.
        </div>
        <table>
          <thead>
            <tr>
              <th>Pico 2 GPIO</th>
              <th>Signal</th>
              <th>Notes</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>GP0–GP7</td>
              <td>D0–D7</td>
              <td>Data bus</td>
            </tr>
            <tr>
              <td>GP8–GP12</td>
              <td>A0–A4</td>
              <td>Address bus (5‑bit)</td>
            </tr>
            <tr>
              <td>GP13</td>
              <td><code>/CS</code></td>
              <td>Chip select (active low)</td>
            </tr>
            <tr>
              <td>GP15</td>
              <td><code>/RES</code></td>
              <td>Reset (active low)</td>
            </tr>
            <tr>
              <td>GP16</td>
              <td>phi2</td>
              <td>PWM clock ~985 kHz</td>
            </tr>
            <tr>
              <td>GND</td>
              <td>R/W</td>
              <td>Tied to GND (write‑only)</td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- SD Card -->
      <div class="section">
        <div class="section-title">SD Card → SPI0</div>
        <div class="section-subtitle">
          Standard SPI0 wiring for SD storage.
        </div>
        <table>
          <thead>
            <tr>
              <th>Pico 2 GPIO</th>
              <th>Signal</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>GP17</td>
              <td><code>/CS</code></td>
            </tr>
            <tr>
              <td>GP18</td>
              <td>SCK</td>
            </tr>
            <tr>
              <td>GP19</td>
              <td>MOSI</td>
            </tr>
            <tr>
              <td>GP20</td>
              <td>MISO</td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- TFT Display -->
      <div class="section">
        <div class="section-title">TFT Display (ST7735 160×128) → SPI1</div>
        <div class="section-subtitle">
          SPI1 wiring for the 160×128 ST7735 TFT module.
        </div>
        <table>
          <thead>
            <tr>
              <th>Pico 2 GPIO</th>
              <th>Signal</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>GP26</td>
              <td>SCK</td>
            </tr>
            <tr>
              <td>GP27</td>
              <td>SDA (MOSI)</td>
            </tr>
            <tr>
              <td>GP28</td>
              <td>DC</td>
            </tr>
            <tr>
              <td>3.3V</td>
              <td>RES (wired direct, no GPIO)</td>
            </tr>
            <tr>
              <td>3.3V</td>
              <td>BL backlight (wired direct, no GPIO)</td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- Button -->
      <div class="section">
        <div class="section-title">Button</div>
        <div class="section-subtitle">
          Single front‑panel control for track navigation.
        </div>
        <table>
          <thead>
            <tr>
              <th>Pico 2 GPIO</th>
              <th>Signal</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>GP22</td>
              <td>Next‑track button (active low, internal pull‑up)</td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="footer-note">
        This page is a visual pinout reference only—always double‑check against the source code and schematic before wiring.
      </div>
    </div>
  </div>
</body>
</html>
