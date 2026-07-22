#!/usr/bin/env python3
"""
MJPEG server for CYD TV — run on your PC, then point the CYD at:
  http://YOUR_PC_IP:8080/

Usage:
  pip install opencv-python
  python stream_server.py                    # webcam
  python stream_server.py path/to/video.mp4  # loop a video file
"""

import sys
import socket
from http.server import BaseHTTPRequestHandler, HTTPServer

try:
    import cv2
except ImportError:
    print("Install OpenCV first:  pip install opencv-python")
    sys.exit(1)

PORT = 8080
SOURCE = 0 if len(sys.argv) < 2 else sys.argv[1]
BOUNDARY = b"--cydframe"


def open_capture():
    if isinstance(SOURCE, str):
        return cv2.VideoCapture(SOURCE)
    return cv2.VideoCapture(int(SOURCE))


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path not in ("/", "/stream", "/stream.mjpg"):
            self.send_error(404)
            return

        self.send_response(200)
        self.send_header("Age", "0")
        self.send_header("Cache-Control", "no-cache, private")
        self.send_header("Pragma", "no-cache")
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=cydframe")
        self.end_headers()

        cap = open_capture()
        if not cap.isOpened():
            print("Could not open video source:", SOURCE)
            return

        print("Streaming to", self.client_address[0])

        try:
            while True:
                ok, frame = cap.read()
                if not ok:
                    if isinstance(SOURCE, str):
                        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                        continue
                    break

                frame = cv2.resize(frame, (320, 240))
                ok, jpg = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 70])
                if not ok:
                    continue

                data = jpg.tobytes()
                self.wfile.write(BOUNDARY + b"\r\n")
                self.wfile.write(b"Content-Type: image/jpeg\r\n")
                self.wfile.write(f"Content-Length: {len(data)}\r\n\r\n".encode())
                self.wfile.write(data)
                self.wfile.write(b"\r\n")
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            cap.release()

    def log_message(self, fmt, *args):
        pass


def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


if __name__ == "__main__":
    ip = local_ip()
    print("CYD TV stream server")
    print("Source:", "webcam" if SOURCE == 0 else SOURCE)
    print(f"URL for cyd_tv.ino:  http://{ip}:{PORT}/")
    print("Press Ctrl+C to stop")
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
