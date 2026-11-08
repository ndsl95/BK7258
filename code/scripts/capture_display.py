"""Capture the WB5E UART log without changing DTR/RTS or resetting the board."""
import argparse
import time
from pathlib import Path
import serial
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM11')
parser.add_argument('--seconds', type=float, default=20)
parser.add_argument('--output', required=True)
args = parser.parse_args()
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
uart = serial.Serial()
uart.port = args.port
uart.baudrate = 115200
uart.timeout = 0.2
uart.dtr = False
uart.rts = False
uart.open()
try:
    with path.open('w', encoding='utf-8') as log:
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            message = uart.read(max(1, uart.in_waiting)).decode('utf-8', errors='replace')
            if message:
                log.write(message)
                log.flush()
                print(message, end='', flush=True)
finally:
    uart.close()
