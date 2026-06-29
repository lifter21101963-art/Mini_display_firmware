#!/usr/bin/env python3

import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


UDP_PORT = 5005
HTTP_PORT = 8000

state_lock = threading.Lock()
latest = {
    "x": 0.0,
    "y": 0.0,
    "z": 0.0,
    "mac": "",
    "hasPosition": False,
    "updatedAt": None,
}
trail = []


def udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"Listening for ESP telemetry on UDP {UDP_PORT}")

    while True:
        data, addr = sock.recvfrom(1024)
        try:
            payload = json.loads(data.decode("utf-8", errors="ignore"))
            x = float(payload.get("x", 0.0))
            y = float(payload.get("y", 0.0))
            z = float(payload.get("z", 0.0))
            mac = str(payload.get("mac", ""))
        except Exception as exc:
            print(f"Bad packet from {addr}: {exc}")
            continue

        with state_lock:
            latest["x"] = x
            latest["y"] = y
            latest["z"] = z
            latest["mac"] = mac
            latest["hasPosition"] = True
            latest["updatedAt"] = time.time()
            trail.append({"x": x, "z": z})
            if len(trail) > 1200:
                del trail[: len(trail) - 1200]


class Handler(BaseHTTPRequestHandler):
    def _send_json(self, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/position":
            with state_lock:
                payload = dict(latest)
            self._send_json(payload)
            return

        if self.path == "/api/trail":
            with state_lock:
                payload = {"points": list(trail), "hasPosition": latest["hasPosition"]}
            self._send_json(payload)
            return

        page = """<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>GT7 Position</title>
  <style>
    body { margin:0; font-family: Arial, Helvetica, sans-serif; background: radial-gradient(circle at top, #172033, #07090d 65%); color:#eaf2ff; }
    .wrap { max-width: 760px; margin: 0 auto; padding: 24px; }
    .card { background: rgba(10,15,24,.92); border: 1px solid rgba(143,183,255,.22); border-radius: 18px; padding: 24px; box-shadow: 0 18px 50px rgba(0,0,0,.35); }
    h1 { margin: 0 0 10px; font-size: 28px; }
    .sub { color:#8ea4c7; margin: 0 0 20px; font-size: 14px; }
    .grid { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:14px; }
    .axis { padding:18px; border-radius:14px; background:linear-gradient(180deg,#111a2a,#0d1320); border:1px solid rgba(255,255,255,.06); }
    .label { font-size:13px; color:#8ea4c7; text-transform:uppercase; letter-spacing:1.8px; }
    .value { margin-top:10px; font-size:34px; font-weight:700; font-variant-numeric: tabular-nums; }
    .status { margin-top:18px; font-size:13px; color:#9cb2d5; }
    .mac { margin-top:12px; font-size:13px; color:#b7c8e6; word-break:break-all; }
    .map { margin-top:18px; border-radius:18px; overflow:hidden; background:linear-gradient(180deg,#07101c,#04070b); border:1px solid rgba(143,183,255,.18); }
    canvas { display:block; width:100%; height:560px; }
    .dot { display:inline-block; width:10px; height:10px; border-radius:50%; background:#4cff88; margin-right:8px; box-shadow:0 0 10px #4cff88; }
    @media (max-width:640px) { .grid { grid-template-columns:1fr; } .value { font-size:30px; } canvas { height:360px; } }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1>GT7 Position</h1>
      <p class="sub">Lokalny serwer na PC odbiera dane z ESP przez UDP.</p>
      <div class="grid">
        <div class="axis"><div class="label">X</div><div class="value" id="x">-</div></div>
        <div class="axis"><div class="label">Y</div><div class="value" id="y">-</div></div>
        <div class="axis"><div class="label">Z</div><div class="value" id="z">-</div></div>
      </div>
      <div class="map">
        <canvas id="track"></canvas>
      </div>
      <div class="status"><span class="dot"></span><span id="status">Oczekiwanie na dane telemetryczne...</span></div>
      <div class="mac">MAC: <span id="mac">-</span></div>
    </div>
  </div>
  <script>
    const trail = [];
    const maxTrail = 1200;
    const metersPerPixel = 2.0;
    const canvas = document.getElementById('track');
    const ctx = canvas.getContext('2d');

    function resizeCanvas() {
      const rect = canvas.getBoundingClientRect();
      const scale = window.devicePixelRatio || 1;
      const width = Math.max(1, Math.floor(rect.width * scale));
      const height = Math.max(1, Math.floor(rect.height * scale));
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
    }

    function drawTrack() {
      resizeCanvas();
      const w = canvas.width;
      const h = canvas.height;
      ctx.clearRect(0, 0, w, h);

      // Grid background
      ctx.fillStyle = '#05080d';
      ctx.fillRect(0, 0, w, h);
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.lineWidth = 1;
      const gridStep = Math.max(40, Math.floor(Math.min(w, h) / 10));
      for (let x = 0; x <= w; x += gridStep) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, h);
        ctx.stroke();
      }
      for (let y = 0; y <= h; y += gridStep) {
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
      }

      if (trail.length < 2) {
        ctx.fillStyle = 'rgba(255,255,255,0.65)';
        ctx.font = `${Math.max(14, Math.floor(h / 22))}px Arial`;
        ctx.fillText('Czekam na pierwsze punkty trasy...', 18, 32);
        return;
      }

      let minX = trail[0].x;
      let maxX = trail[0].x;
      let minZ = trail[0].z;
      let maxZ = trail[0].z;
      for (const p of trail) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.z < minZ) minZ = p.z;
        if (p.z > maxZ) maxZ = p.z;
      }

      const trailWidth = Math.max(1e-6, maxX - minX);
      const trailHeight = Math.max(1e-6, maxZ - minZ);
      const scaledWidth = trailWidth / metersPerPixel;
      const scaledHeight = trailHeight / metersPerPixel;
      const offsetX = (w - scaledWidth) / 2;
      const offsetY = (h - scaledHeight) / 2;

      function mapPoint(p) {
        return {
          x: offsetX + ((maxX - p.x) / metersPerPixel),
          y: offsetY + ((maxZ - p.z) / metersPerPixel),
        };
      }

      const path = trail.map(mapPoint);

      // Trail line
      ctx.beginPath();
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      ctx.lineWidth = Math.max(2, Math.floor(Math.min(w, h) / 180));
      ctx.strokeStyle = '#4bb7ff';
      ctx.shadowColor = 'rgba(75,183,255,0.35)';
      ctx.shadowBlur = 10;
      ctx.moveTo(path[0].x, path[0].y);
      for (let i = 1; i < path.length; i += 1) {
        ctx.lineTo(path[i].x, path[i].y);
      }
      ctx.stroke();
      ctx.shadowBlur = 0;

      // Current car marker as a dot
      const current = path[path.length - 1];
      const pulse = 0.5 + 0.5 * Math.sin(Date.now() / 180);
      const dotRadius = Math.max(5, Math.floor(Math.min(w, h) / 60)) * (0.92 + pulse * 0.12);
      ctx.beginPath();
      ctx.fillStyle = '#4cff88';
      ctx.shadowColor = 'rgba(76,255,136,0.75)';
      ctx.shadowBlur = 14 + pulse * 8;
      ctx.arc(current.x, current.y, dotRadius, 0, Math.PI * 2);
      ctx.fill();
      ctx.shadowBlur = 0;

      // Label bound to the marker so it moves with the dot
      ctx.font = `700 ${Math.max(7, Math.floor(Math.min(w, h) / 56))}px Arial`;
      ctx.textBaseline = 'middle';
      ctx.fillStyle = '#ffffff';
      ctx.shadowColor = 'rgba(0,0,0,0.85)';
      ctx.shadowBlur = 8;
      ctx.fillText('102 APR_Gbech63_pl', current.x + dotRadius + 10, current.y);
      ctx.shadowBlur = 0;

      // Center labels
      ctx.fillStyle = 'rgba(255,255,255,0.75)';
      ctx.font = `${Math.max(12, Math.floor(h / 28))}px Arial`;
      ctx.fillText('Tor jazdy: X/Z', 18, 28);
    }

    async function refresh() {
      try {
        const res = await fetch('/api/position', { cache: 'no-store' });
        const data = await res.json();
        document.getElementById('x').textContent = Number(data.x || 0).toFixed(3);
        document.getElementById('y').textContent = Number(data.y || 0).toFixed(3);
        document.getElementById('z').textContent = Number(data.z || 0).toFixed(3);
        document.getElementById('mac').textContent = data.mac || '-';
        document.getElementById('status').textContent = data.hasPosition ? 'Aktualizowane na żywo' : 'Brak jeszcze danych telemetrycznych';
        if (data.hasPosition) {
          const last = trail[trail.length - 1];
          if (!last || last.x !== data.x || last.z !== data.z) {
            trail.push({ x: Number(data.x || 0), z: Number(data.z || 0) });
            if (trail.length > maxTrail) {
              trail.splice(0, trail.length - maxTrail);
            }
          }
          drawTrack();
        }
      } catch (e) {
        document.getElementById('status').textContent = 'Brak połączenia z serwerem';
      }
    }
    window.addEventListener('resize', drawTrack);
    refresh();
    setInterval(refresh, 100);
  </script>
</body>
</html>
"""
        body = page.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        return


def main():
    threading.Thread(target=udp_listener, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    print(f"Open http://localhost:{HTTP_PORT}")
    server.serve_forever()


if __name__ == "__main__":
    main()
