#!/usr/bin/env python3
import serial
import time
import sys
import argparse

# Protocol constants from gree_ac_cnt.h
SYNC = 0x7E
CMD_IN_UNIT_REPORT = 0x31
CMD_OUT_PARAMS_SET = 0x01
CMD_OUT_SYNC_TIME = 0x03
CMD_OUT_MAC_REPORT = 0x04
CMD_OUT_UNKNOWN_1 = 0x02
CMD_IN_UNKNOWN_1 = 0x44
CMD_IN_UNKNOWN_2 = 0x33

COMMAND_NAMES = {
    CMD_IN_UNIT_REPORT: "UNIT_REPORT",
    CMD_OUT_PARAMS_SET: "PARAMS_SET",
    CMD_OUT_SYNC_TIME: "SYNC_TIME",
    CMD_OUT_MAC_REPORT: "MAC_REPORT",
    CMD_OUT_UNKNOWN_1: "UNKNOWN_OUT_1",
    CMD_IN_UNKNOWN_1: "UNKNOWN_IN_1",
    CMD_IN_UNKNOWN_2: "UNKNOWN_IN_2",
}

MODES = {0: "Auto", 1: "Cool", 2: "Dry", 3: "Fan", 4: "Heat"}
FAN_SPEEDS = {0x08: "Auto", 0x09: "Minimum", 0x0A: "Low", 0x0B: "Medium", 0x0C: "High", 0x0D: "Maximum"}

VSWING_MODES = {
    0: "Off", 1: "Swing-Full", 2: "Constant-Up", 3: "Constant-Mid-Up",
    4: "Constant-Middle", 5: "Constant-Mid-Down", 6: "Constant-Down",
    7: "Swing-Down", 8: "Swing-Mid-Down", 9: "Swing-Middle",
    10: "Swing-Mid-Up", 11: "Swing-Up"
}

HSWING_MODES = {
    0: "Off", 1: "Swing-Full", 2: "Constant-Left", 3: "Constant-Mid-Left",
    4: "Constant-Middle", 5: "Constant-Mid-Right", 6: "Constant-Right"
}

def format_hex_pretty(data):
    return ".".join(f"{b:02X}" for b in data)

def parse_0x31(data):
    # data is the payload starting from index 4 (after 0x7E 0x7E LEN CMD)
    # until before checksum.
    if len(data) < 45:
        return "Payload too short for UNIT_REPORT"

    # Power and Mode
    pwr_byte = data[4]
    power = "ON" if (pwr_byte & 0x80) else "OFF"
    mode_val = (pwr_byte & 0x70) >> 4
    mode = MODES.get(mode_val, f"Unknown({mode_val})")

    # Target Temp
    temp_set_byte = data[5]
    target_temp = ((temp_set_byte & 0xF0) >> 4) + 16

    # Current Temp
    temp_act_byte = data[42]
    current_temp = temp_act_byte - 40

    # Fan Speed
    fan_spd_byte = data[18] & 0x0F
    fan_speed = FAN_SPEEDS.get(fan_spd_byte, f"Unknown({fan_spd_byte:02X})")

    # Turbo/Quiet
    turbo = "ON" if (data[6] & 0x01) else "OFF"
    quiet = "OFF"
    if data[16] & 0x08:
        quiet = "ON"
    elif data[16] & 0x04:
        quiet = "Auto"

    # Swings
    vswing_val = (data[8] & 0xF0) >> 4
    vswing = VSWING_MODES.get(vswing_val, f"Unknown({vswing_val})")
    hswing_val = data[8] & 0x07
    hswing = HSWING_MODES.get(hswing_val, f"Unknown({hswing_val})")

    # Others
    light = "ON" if (data[6] & 0x02) else "OFF"
    ionizer = "ON" if ((data[6] & 0x04) or (data[0] & 0x04)) else "OFF"
    beeper = "ON" if not (data[40] & 0x01) else "OFF"
    xfan = "ON" if (data[6] & 0x08) else "OFF"
    sleep = "ON" if (data[4] & 0x08) else "OFF"
    powersave = "ON" if (data[11] & 0x40) else "OFF"
    ifeel = "ON" if (data[9] & 0x40) else "OFF"
    unit = "F" if (data[7] & 0x80) else "C"

    return (f"Power: {power}, Mode: {mode}, Target: {target_temp}{unit}, Current: {current_temp}{unit}, "
            f"Fan: {fan_speed}, Turbo: {turbo}, Quiet: {quiet}, "
            f"V-Swing: {vswing}, H-Swing: {hswing}, "
            f"Light: {light}, Ionizer: {ionizer}, Beeper: {beeper}, X-Fan: {xfan}, "
            f"Sleep: {sleep}, Powersave: {powersave}, I-Feel: {ifeel}")

def get_timestamp():
    return time.strftime("%H:%M:%S", time.localtime()) + f".{int(time.time() * 1000) % 1000:03d}"

def parse_packet(packet):
    if len(packet) < 5:
        return

    ts = get_timestamp()
    cmd_id = packet[3]
    cmd_name = COMMAND_NAMES.get(cmd_id, f"UNKNOWN(0x{cmd_id:02X})")

    # Determine if it looks like TX (from WiFi) or RX (from IU)
    # Based on gree_ac_cnt.h definitions:
    # CMD_OUT... are likely from WiFi (0x01, 0x02, 0x03, 0x04)
    # CMD_IN... are likely from IU (0x31, 0x33, 0x44)
    direction = "???"
    if cmd_id in [0x01, 0x02, 0x03, 0x04]:
        direction = "TX"
    elif cmd_id in [0x31, 0x33, 0x44]:
        direction = "RX"

    print(f"[{ts}][{direction}] {cmd_name} - {format_hex_pretty(packet)}")

    if cmd_id == CMD_IN_UNIT_REPORT:
        payload = packet[4:-1]
        try:
            print(f"[{ts}]      Details: {parse_0x31(payload)}")
        except Exception as e:
            print(f"[{ts}]      Error parsing UNIT_REPORT: {e}")
    elif cmd_id == CMD_OUT_PARAMS_SET:
        # 0x01 is almost same format as 0x31
        payload = packet[4:-1]
        try:
            # We can reuse parse_0x31 for most fields in PARAMS_SET as they share offsets
            print(f"[{ts}]      Details: {parse_0x31(payload)}")
        except Exception as e:
            print(f"[{ts}]      Error parsing PARAMS_SET: {e}")
    print("-" * 80)

def main():
    parser = argparse.ArgumentParser(description="Gree AC Serial Sniffer")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port to use (default: /dev/ttyUSB0)")
    args = parser.parse_args()

    port = args.port
    baud = 4800

    ts = get_timestamp()
    print(f"[{ts}] Opening {port} at {baud} baud (EVEN parity)...")
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
        print(f"[{ts}] Failed to open serial port: {e}")
        sys.exit(1)

    ts = get_timestamp()
    print(f"[{ts}] Listening for Gree AC traffic (waiting for next sync to process)...")
    buffer = bytearray()

    try:
        while True:
            char = ser.read(1)
            if char:
                buffer.append(char[0])
            else:
                continue

            # Find all sync markers
            sync_indices = []
            for i in range(len(buffer) - 1):
                if buffer[i] == SYNC and buffer[i+1] == SYNC:
                    # Avoid overlapping pairs (e.g., 7E 7E 7E)
                    if not sync_indices or i >= sync_indices[-1] + 2:
                        sync_indices.append(i)

            if len(sync_indices) >= 2:
                # We have at least two sync markers.
                # The data from the first sync to the second sync is our potential packet.
                start = sync_indices[0]
                end = sync_indices[1]
                packet = buffer[start:end]

                # Identify where the LEN byte starts (skipping extra leading 7Es)
                len_idx = 2
                while len_idx < len(packet) and packet[len_idx] == SYNC:
                    len_idx += 1

                # Check if we have enough bytes for a header (LEN, CMD)
                if len_idx < len(packet) - 1:
                    frame_len = packet[len_idx]
                    # The CRC should be at index len_idx + frame_len
                    crc_idx = len_idx + frame_len

                    if crc_idx < len(packet):
                        recv_crc = packet[crc_idx]
                        calc_crc = 0
                        for i in range(len_idx, crc_idx):
                            calc_crc = (calc_crc + packet[i]) & 0xFF

                        if calc_crc == recv_crc:
                            # Construct a "clean" packet starting with exactly 7E 7E
                            # and ending exactly at CRC
                            clean_packet = bytearray([SYNC, SYNC]) + packet[len_idx:crc_idx + 1]
                            parse_packet(clean_packet)
                        else:
                            ts = get_timestamp()
                            print(f"[{ts}][ERR] Checksum mismatch! Calc: 0x{calc_crc:02X}, Recv: 0x{recv_crc:02X}")
                            print(f"[{ts}]      Packet: {format_hex_pretty(packet)}")
                    else:
                        # We have a next sync, but the packet described by LEN
                        # is longer than what we have between syncs.
                        ts = get_timestamp()
                        print(f"[{ts}][ERR] Incomplete packet or invalid length!")
                        print(f"[{ts}]      Packet: {format_hex_pretty(packet)}")

                # Remove processed data up to the start of the next packet
                buffer = buffer[end:]

            elif len(sync_indices) == 1 and sync_indices[0] > 0:
                # Garbage before the first sync, discard it
                buffer = buffer[sync_indices[0]:]

            if len(buffer) > 1024: # Safety limit
                # If no sync found in 1KB, something is wrong
                buffer.clear()

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
