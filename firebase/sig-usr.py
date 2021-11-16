import signal, os, sys, time
from threading import Event

terminate = Event()

def signalHandler(sig, frame):
    print("SIGUSR1 received, set terminate")
    # sys.exit(0)
    terminate.set()

signal.signal(signal.SIGUSR1, signalHandler)
print(f"send SIGUSR1 to PID {os.getpid()} to terminate")

ret = terminate.wait(10)
if ret:
    print("terminated by signal")
else:
    print("timed out")

#signal.pause()
