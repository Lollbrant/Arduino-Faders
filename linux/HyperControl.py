#!/usr/bin/python3

import serial
import json
import sys
import subprocess
import time
import glob
import logging

# --- Configuration ---
UPDATE_INTERVAL = 0.1 # Seconds

FADER_CONTROL = {
    1: "alsa_output",
    2: "upmix",
    3: "Firefox",
    4: "Chromium",
    5: "Brave"
}

# --- Internal variables ---
last_run_volume = 0
last_run_applications = 0
node_ids = {}
last_fader_value = {}
buffer = ""
pw_result_old = ""
auto_set_volume = False

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")

def find_serial_port(expected_id="HC001", baudrate=115200, timeout=1):
    ports = glob.glob('/dev/ttyACM*') + glob.glob('/dev/serial/by-id/*')

    for port in ports:
        try:
            with serial.Serial(port, baudrate=baudrate, timeout=timeout) as ser:
                time.sleep(0.5)
                line = ser.readline().decode('utf-8').strip()
                if not line:
                    continue
                data = json.loads(line)
                if 'id' in data and data['id'] == expected_id:
                    logging.info(f"Found HyperControl device on port {port}: {line}")
                    return port
        except (serial.SerialException, UnicodeDecodeError):
            continue
    logging.error("No matching HyperControl device found")
    sys.exit(1)

def get_all_node_ids(name, pw_data=None):
    if pw_data is None:
        result = subprocess.run(['pw-dump'], capture_output=True, text=True)
        pw_data = json.loads(result.stdout)
    nodes = []

    for obj in pw_data:
        props = obj.get('info', {}).get('props', {})
        media_class = props.get('media.class', '')
        if media_class == 'Audio/Sink' and name.lower() in props.get('node.name','').lower():
            nodes.append(obj.get('id'))
        if props.get('application.name', '').lower() == name.lower():
            if media_class.startswith("Stream/Output"):
                nodes.append(obj.get('id'))
    return nodes

def set_volume(fader_id, volume):
    vol = max(0.0, min(1.0, volume))

    for node_id in node_ids[fader_id]:
        result = subprocess.run(['wpctl', 'set-volume', str(node_id), str(vol)], check=False, capture_output=True, text=True)
        if result.returncode:
            logging.warning(f"Failed to set volume for node {node_id}, retrying...")
            node_ids[fader_id] = get_all_node_ids(FADER_CONTROL[fader_id])
            subprocess.run(['wpctl', 'set-volume', str(node_id), str(vol)], check=False)

# --- Open serial port ---
SERIAL_PORT = find_serial_port()
BAUD_RATE = 115200
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    logging.info(f"Connected to {SERIAL_PORT} at {BAUD_RATE} baud.")
except serial.SerialException as e:
    logging.exception(f"Error opening serial port: {e}")
    sys.exit(1)

# --- Main loop ---
try:
    while True:
        now = time.time()
        chunk = ser.read(ser.in_waiting or 1).decode('utf-8', errors='ignore')
        buffer += chunk
    
        while '\n' in buffer:
            line, buffer = buffer.split('\n',1)
            line = line.strip()

            if not line:
                continue
            try:
                data = json.loads(line)

                # Process faders
                if not 'faders' in data:
                    continue

                if now - last_run_volume >= UPDATE_INTERVAL:
                    last_run_volume = now
                    if now - last_run_applications >= (UPDATE_INTERVAL * 3):
                        last_run_applications = now
                        result = subprocess.run(['pw-dump'], capture_output=True, text=True)
                        pw_data = json.loads(result.stdout)
                        for fid, app in FADER_CONTROL.items():
                            node_ids[fid] = get_all_node_ids(app, pw_data)
                        auto_set_volume = True

                    for fader in data['faders']:
                        fid = fader['id']
                        val = fader['value']
                        lastVal = last_fader_value.get(fid, 0)

                        if abs(val - lastVal) > 2 or auto_set_volume:
                            volume = val / 1023.0
                            set_volume(fid, volume)
                            last_fader_value[fid] = val
                    auto_set_volume = False

            except json.JSONDecodeError:
                if False:
                    logging.info(f"Invalid JSON: {line}")
except KeyboardInterrupt:
    logging.exception("Exiting.")
    sys.exit(1)
except serial.SerialException as e:
    logging.exception(f"Serial error: {e}")
    sys.exit(1)
except OSError as e:
    logging.exception(f"OS error: {e}")
    sys.exit(1)
finally:
    logging.exception("Exiting now...")
    ser.close()
    sys.exit(1)
