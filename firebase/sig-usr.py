#!/usr/bin/python3

### with this shebang the script name shows up in process list,
### if run from command line as ./sig-usr.py

import signal, os, sys, time
from threading import Event

terminate = Event()

def signalHandler(sig, frame):
    print("SIGUSR1 received, set terminate")
    # sys.exit(0)
    terminate.set()

signal.signal(signal.SIGUSR1, signalHandler)
print(f"send SIGUSR1 to PID {os.getpid()} to terminate")

ret = terminate.wait(60)
if ret:
    print("terminated by signal")
else:
    print("timed out")

#signal.pause()
