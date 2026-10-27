#!/usr/bin/env python3
"""Capture raw bytes from a serial device without pyserial (termios raw read)."""
import os, sys, time, termios, tty

path = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0

fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
try:
    attrs = termios.tcgetattr(fd)
    attrs[4] = termios.B115200          # ispeed
    attrs[5] = termios.B115200         # ospeed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
except Exception as e:
    print("tcsetattr:", e, file=sys.stderr)

tty.setraw(fd)
buf = bytearray()
end = time.time() + secs
while time.time() < end:
    try:
        chunk = os.read(fd, 4096)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", "replace"))
            sys.stdout.flush()
    except BlockingIOError:
        time.sleep(0.05)
    except OSError as e:
        print("read err", e, file=sys.stderr)
        break
print(f"\n--- captured {len(buf)} bytes ---", file=sys.stderr)
