#!/usr/bin/env python3
import serial
import time
import sys
import argparse
import select

# Protocol constants
SYNC = 0x7E

def format_hex_pretty(data):
    return ".".join(f"{b:02X}" for b in data)

def get_timestamp():
    return time.strftime("%H:%M:%S", time.localtime()) + f".{int(time.time() * 1000) % 1000:03d}"

def log_packet(packet, direction):
    ts = get_timestamp()
    hex_data = format_hex_pretty(packet)
    length = len(packet)
    print(f"[{ts}] [{direction}] [{hex_data}] [{length}]")

def handle_rx_packet(ser, packet):
    # Reponse to MAC ADDRESS_REQUEST (0x04)
    if (packet[3] == 0x04):
        response = bytearray([0x7E, 0x7E, 0x1A, 0x44, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x61])
        ser.write(response)
        log_packet(response, "TX")

def main():
    parser = argparse.ArgumentParser(description="Gree AC Serial Sniffer")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port to use (default: /dev/ttyUSB0)")
    args = parser.parse_args()

    port = args.port
    baud = 4800

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            parity=serial.PARITY_EVEN,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS,
            timeout=0.1
        )
    except Exception as e:
        print(f"[{get_timestamp()}] Failed to open serial port {port}: {e}")
        sys.exit(1)

    print(f"[{get_timestamp()}] Listening on {port} (4800 8E1)...")
    print("Press ENTER to exit cleanly.")

    ser.reset_input_buffer()
    buffer = bytearray()

    try:
        while True:
            # Multiplex serial port and stdin
            # We use a timeout so the loop stays alive and can respond to signals if needed
            rlist, _, _ = select.select([ser, sys.stdin], [], [], 0.1)

            if sys.stdin in rlist:
                sys.stdin.readline()
                print("Exiting...")
                break

            if ser in rlist:
                # Read whatever is available
                try:
                    chunk = ser.read(ser.in_waiting or 1)
                except serial.SerialException as e:
                    print(f"\n[{get_timestamp()}] Serial error: {e}")
                    break

                if chunk:
                    buffer.extend(chunk)

                # Process buffer for packets
                while len(buffer) >= 4: # Min packet: 7E 7E LEN CMD CRC (at least 5 bytes, but 4 to start looking)
                    # Find first 7E 7E
                    sync_idx = buffer.find(b'\x7E\x7E')
                    if sync_idx == -1:
                        # No sync pair found.
                        # Keep the last byte if it's 7E, otherwise clear.
                        if buffer and buffer[-1] == SYNC:
                            buffer = buffer[-1:]
                        else:
                            buffer.clear()
                        break

                    # Remove anything before first 7E 7E
                    if sync_idx > 0:
                        buffer = buffer[sync_idx:]

                    # Now buffer starts with 7E 7E.
                    # Skip any additional 7Es to find the LEN byte.
                    len_idx = 2
                    while len_idx < len(buffer) and buffer[len_idx] == SYNC:
                        len_idx += 1

                    if len_idx >= len(buffer):
                        # We have 7E 7E... but no non-7E byte yet.
                        # Wait for more data.
                        break

                    frame_len = buffer[len_idx]
                    # The total packet length:
                    # syncs + extra syncs + LEN + (CMD + Payload + CRC)
                    # The value of frame_len covers (CMD + Payload + CRC)
                    total_packet_len = len_idx + 1 + frame_len

                    if len(buffer) < total_packet_len:
                        # Full packet not yet received.
                        break

                    # We have a full packet!
                    packet = buffer[:total_packet_len]

                    # Log it
                    log_packet(packet, "RX")

                    # Call placeholder for potential TX response
                    handle_rx_packet(ser, packet)

                    # Remove processed packet from buffer
                    buffer = buffer[total_packet_len:]

    except KeyboardInterrupt:
        print("\nStopping...")
    except Exception as e:
        print(f"\n[{get_timestamp()}] Unexpected error: {e}")
    finally:
        ser.close()
        print(f"[{get_timestamp()}] Serial port closed.")

if __name__ == "__main__":
    main()
