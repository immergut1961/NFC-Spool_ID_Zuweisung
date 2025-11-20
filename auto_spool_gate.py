import serial
import time
import requests

# === KONFIGURATION ===
serial_port = '/dev/pico_nfc'                # Passe ggf. eigenen Serialport an
baudrate = 115200
moonraker_host = 'http://localhost:7125'     # Passe ggf. IP/Port an
macro_name = 'LOAD_SPOOL_AT_GATE'
macro_template = '{macro} SPOOLID={spoolid} GATE={gate}'
timeout_secs = 20

gate_sensor_names = [f'mmu_pre_gate_{i}' for i in range(14)]

def get_all_gate_sensor_states():
    objects = {f'filament_switch_sensor {name}': None for name in gate_sensor_names}
    try:
        r = requests.post(f"{moonraker_host}/printer/objects/query", json={"objects": objects}, timeout=2)
        result = r.json().get("result", {}).get("status", {})
        states = {}
        for name in gate_sensor_names:
            key = f'filament_switch_sensor {name}'
            states[name] = result.get(key, {}).get('filament_detected', None)
        return states
    except Exception as ex:
        print(f'Error fetching MMU gate sensors: {ex}')
        return {}

def post_gcode(gcode_line):
    try:
        requests.post(
            f'{moonraker_host}/printer/gcode/script',
            headers={'Content-Type': 'application/json'},
            json={'script': gcode_line},
            timeout=3
        )
        print(f'[INFO] GCode ausgeführt: {gcode_line}')
    except Exception as ex:
        print(f'Error sending GCODE: {ex}')

def main():
    print(f'Listening NFC-Serial on {serial_port}...')
    try:
        ser = serial.Serial(serial_port, baudrate=baudrate, timeout=1)
    except Exception as e:
        print(f'[ERROR] Kann Seriellen Port nicht öffnen: {e}')
        return
    last_spoolid = None
    spoolid_time = None
    prev_states = get_all_gate_sensor_states()
    while True:
        try:
            line = ser.readline().decode(errors='ignore').strip()
        except Exception as e:
            print(f'[ERROR] Serial Read: {e}')
            break
        if line.startswith('SET_SPOOL_ID ID='):
            spoolid = line.split('SET_SPOOL_ID ID=')[1].strip()
            last_spoolid = spoolid
            spoolid_time = time.time()
            print(f'[INFO] SpoolID erkannt: {spoolid}')
            print(f'[INFO] Warte {timeout_secs} Sekunden auf Gate...')
            prev_states = get_all_gate_sensor_states()
            print("[DEBUG] Sensorstatus direkt nach Scan:", prev_states)
        if last_spoolid is not None:
            timeleft = timeout_secs - int(time.time() - spoolid_time)
            if timeleft <= 0:
                print('[INFO] Timeout – keine Gate-Zuordnung erfolgt')
                last_spoolid = None
                spoolid_time = None
            else:
                curr_states = get_all_gate_sensor_states()
                print("[DEBUG] Sensorstatus:", curr_states)
                for idx, sensor_name in enumerate(gate_sensor_names):
                    if prev_states.get(sensor_name) == False and curr_states.get(sensor_name) == True:
                        print(f'[INFO] GATE FLANKE: {idx} ("{sensor_name}") → Macro senden!')
                        macro_line = macro_template.format(
                            macro=macro_name, spoolid=last_spoolid, gate=idx
                        )
                        post_gcode(macro_line)
                        last_spoolid = None
                        spoolid_time = None
                        break
                prev_states = curr_states
                time.sleep(1)

if __name__ == '__main__':
    main()
