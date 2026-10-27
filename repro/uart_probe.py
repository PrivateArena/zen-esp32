import os, sys, time, termios, tty, fcntl, select
port=sys.argv[1]; baud=int(sys.argv[2]) if len(sys.argv)>2 else 115200
fd=os.open(port, os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
attrs=termios.tcgetattr(fd)
ispeed=ospeed=getattr(termios, 'B%d'%baud)
attrs[0]=0; attrs[1]=ispeed; attrs[2]=0; attrs[3]=0; attrs[4]=ospeed; attrs[5]=0; attrs[6]=[0]*32
termios.tcsetattr(fd, termios.TCSANOW, attrs)
tty.setraw(fd)
TIOCMGET=0x5415; TIOCMSET=0x5418
import struct
def ctl():
    b=fcntl.ioctl(fd, TIOCMGET, b'\0'*4); return struct.unpack('I',b)[0]
def setctl(v):
    fcntl.ioctl(fd, TIOCMSET, struct.pack('I', v))
m=ctl()
# pulse RTS (EN) and DTR (IO0 low => download mode) then release both => run
setctl((m & ~termios.TIOCM_RTS & ~termios.TIOCM_DTR)); time.sleep(0.2)
setctl(m & ~termios.TIOCM_RTS | termios.TIOCM_DTR); time.sleep(0.05)
setctl(m | termios.TIOCM_RTS | termios.TIOCM_DTR)
buf=b""; t0=time.time()
while time.time()-t0 < float(sys.argv[3] if len(sys.argv)>3 else 4):
    r,_,_=select.select([fd],[],[],0.2)
    if r:
        try: buf+=os.read(fd,4096)
        except BlockingIOError: pass
os.close(fd)
sys.stdout.write(buf.decode('utf-8','replace'))
