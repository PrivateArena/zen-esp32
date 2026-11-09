#!/usr/bin/env python3
"""Pulse EN (RTS) with IO0 (DTR) released, so the chip restarts its app.

The auto-reset circuit on these boards wires DTR->IO0 and RTS->EN, so both asserted
means "hold in reset with boot strap low" = download mode. Releasing DTR first is what
keeps this a plain restart. Usage: hardreset.py /dev/ttyACM0
"""
import os, sys, time, termios, fcntl, struct

fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
def get():
    return struct.unpack('I', fcntl.ioctl(fd, termios.TIOCMGET, b'\0' * 4))[0]
def put(v):
    fcntl.ioctl(fd, termios.TIOCMSET, struct.pack('I', v))

run  = lambda m: m & ~termios.TIOCM_RTS & ~termios.TIOCM_DTR      # EN high, IO0 high
hold = lambda m: (m | termios.TIOCM_RTS) & ~termios.TIOCM_DTR     # EN low,  IO0 high
m = get()
put(run(m));  time.sleep(0.05)
put(hold(m)); time.sleep(0.15)
put(run(m));  time.sleep(0.05)
os.close(fd)
