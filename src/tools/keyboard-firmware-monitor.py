import serial
import serial.tools.list_ports
import time
from datetime import datetime, timedelta
import re

def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found!")
        return []
    print("Available serial ports:")
    for i, port in enumerate(ports):
        print(f"  {i}: {port.device} - {port.description}")
    return ports

def pick_port(ports):
    while True:
        try:
            idx = int(input("Select port index: "))
            if 0 <= idx < len(ports):
                return ports[idx].device
        except ValueError:
            pass
        print("Invalid selection. Try again.")

def parse_response(resp):
    # Look for pattern like :0FFD362A:...
    match = re.search(r':0FFD362A:([0-9A-Fa-f]+)', resp)
    if not match:
        return None

    hex_data = match.group(1)
    if len(hex_data) < 12:
        print("Response too short.")
        return None

    # First 6 bytes (12 hex digits)
    data_bytes = bytes.fromhex(hex_data[:12])
    days_lsb = data_bytes[0]
    days_msb = data_bytes[1]
    days = days_lsb + (days_msb << 8)

    git_bytes = data_bytes[2:6]

    git_commit_hex = ''.join(f'{b:02X}' for b in git_bytes)

    # Convert days since 2020-01-01
    fw_date = datetime(2020, 1, 1) + timedelta(days=days)
    if days == 16383:
        return "Keyboard firmware too old to report version, or keyboard not connected."
    fw_date_str = fw_date.strftime('%Y-%m-%d')

    return f"Firmware Date: {fw_date_str}, GIT Commit: {git_commit_hex}"

def main():
    ports = list_serial_ports()
    if not ports:
        return

    port_name = pick_port(ports)

    try:
        with serial.Serial(
            port=port_name,
            baudrate=2000000,
            timeout=0.5,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        ) as ser:
            print(f"Opened {port_name} at 2,000,000 bps.")

            while True:
                ser.write(b"mffd362a\r\n")
                # print(f"> Sent: mffd362a")
                time.sleep(1)
                while ser.in_waiting:
                    line = ser.readline().decode(errors='ignore').strip()
                    if line:
                        # print(f"< Received: {line}")
                        result = parse_response(line)
                        if result:
                             print(f"* Parsed: {result}")

    except serial.SerialException as e:
        print(f"Error opening/using serial port: {e}")

if __name__ == '__main__':
    main()

