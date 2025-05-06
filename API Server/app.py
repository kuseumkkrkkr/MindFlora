from flask import Flask, request, jsonify, render_template, redirect, url_for
import secrets
import sqlite3
import os

app = Flask(__name__)
DB_FILE = 'device_data.db'

def init_db():
    with sqlite3.connect(DB_FILE) as conn:
        cur = conn.cursor()
        cur.execute('''
            CREATE TABLE IF NOT EXISTS devices (
                api_key TEXT PRIMARY KEY,
                sensor1 INTEGER,
                sensor2 INTEGER,
                sensor3 INTEGER,
                sensor4 INTEGER,
                onoff TEXT,
                led INTEGER
            )
        ''')
        conn.commit()

@app.route('/')
def home():
    return redirect(url_for('dev_dashboard'))

@app.route('/dev', methods=['GET'])
def dev_dashboard():
    with sqlite3.connect(DB_FILE) as conn:
        cur = conn.cursor()
        cur.execute("SELECT * FROM devices")
        devices = cur.fetchall()
    return render_template('dev.html', devices=devices)

@app.route('/generate_key', methods=['POST'])
def generate_key():
    new_key = secrets.token_hex(4).upper()
    with sqlite3.connect(DB_FILE) as conn:
        cur = conn.cursor()
        cur.execute('''
            INSERT INTO devices (api_key, sensor1, sensor2, sensor3, sensor4, onoff, led)
            VALUES (?, 0, 0, 0, 0, 'O', 0)
        ''', (new_key,))
        conn.commit()
    return redirect(url_for('dev_dashboard'))

@app.route('/post_binary', methods=['POST'])
def post_binary():
    data = request.get_data()
    
    if len(data) < 10:  
        return "Invalid data format", 400
    
    api_key = data[:8].decode()
    type_byte = data[8]
    payload_len = data[9]
    
    # Ensure we have enough data for the payload
    if len(data) < 10 + payload_len:
        return "Incomplete payload", 400
        
    payload = data[10:10+payload_len]

    with sqlite3.connect(DB_FILE) as conn:
        cur = conn.cursor()
        cur.execute("SELECT 1 FROM devices WHERE api_key = ?", (api_key,))
        if cur.fetchone() is None:
            return "Invalid API Key", 403

        if type_byte == 0x01:  # Sensor
            if len(payload) != 4:
                return "Expected 4 sensor values", 400
            cur.execute('''
                UPDATE devices SET sensor1=?, sensor2=?, sensor3=?, sensor4=? WHERE api_key=?
            ''', (payload[0], payload[1], payload[2], payload[3], api_key))

        elif type_byte == 0x02:  # OnOff
            onoff_val = 'O' if payload[0] == 0x01 else 'X'
            cur.execute('UPDATE devices SET onoff=? WHERE api_key=?', (onoff_val, api_key))

        elif type_byte == 0x03:  # LED
            led_val = payload[0]  
            cur.execute('UPDATE devices SET led=? WHERE api_key=?', (led_val, api_key))
        else:
            return "Unknown Type", 400

        conn.commit()
    return "OK", 200

@app.route('/get_binary/<api_key>', methods=['GET'])
def get_binary(api_key):
    with sqlite3.connect(DB_FILE) as conn:
        cur = conn.cursor()
        cur.execute('SELECT sensor1, sensor2, sensor3, sensor4, onoff, led FROM devices WHERE api_key=?', (api_key,))
        row = cur.fetchone()
        if row is None:
            return "Invalid API Key", 403
        sensor = row[:4]
        onoff = row[4]
        led = row[5]

    data = bytearray()

    data.extend(api_key.encode())
    data.append(0x01)
    data.append(4)
    data.extend(sensor)

    data.extend(api_key.encode())
    data.append(0x02)
    data.append(1)
    data.extend(onoff.encode())

    data.extend(api_key.encode())
    data.append(0x03)
    data.append(1)
    data.append(led)

    return bytes(data)
if __name__ == '__main__':
    init_db()
    app.run(host='0.0.0.0', port=5000)
