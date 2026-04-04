# send repeated keepalive to BMS over serial
import time
import serial

def main():
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    keepalive_msg = b'\xff\x07KeepAliv\x20\xe8'

    while True:
        ser.write(keepalive_msg)
        print("Sent keepalive to BMS")
        time.sleep(1)  # Send keepalive every second

if __name__ == "__main__":
    main()