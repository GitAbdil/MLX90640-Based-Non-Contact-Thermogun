import serial
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import threading

# --- KONFIGURASI ---
SERIAL_PORT = 'COM14'   # GANTI SESUAI PORT ANDA
BAUD_RATE = 115200   # HARUS SAMA dengan ESP32
WIDTH = 32
HEIGHT = 24

# --- SETUP SERIAL ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    ser.reset_input_buffer() # Bersihkan sampah data lama saat start
    print(f"Terhubung ke {SERIAL_PORT}")
except Exception as e:
    print(f"GAGAL: {e}")
    exit()

# --- SETUP PLOT ---
fig, ax = plt.subplots(figsize=(10, 8))
# Inisialisasi array kosong
data_matrix = np.zeros((HEIGHT, WIDTH))

# Setup Heatmap
# vmin dan vmax dikosongkan agar auto-scale, atau set manual (misal 20-40)
heatmap = ax.imshow(data_matrix, cmap='inferno', interpolation='bicubic', vmin=20, vmax=45)
plt.colorbar(heatmap, label='Suhu (°C)')
title_text = ax.set_title("Thermal Camera Real-Time")

# --- BUFFER TERAKHIR ---
# Kita gunakan variabel global untuk menyimpan frame terakhir yang valid
last_frame = np.zeros((HEIGHT, WIDTH))

def update(frame):
    global last_frame
    
    # Baca semua data yang ada di buffer serial sekaligus
    # Ini mencegah lag jika data ESP32 lebih cepat dari Python
    try:
        # Loop sampai buffer kosong, ambil data paling baru saja
        raw_line = None
        while ser.in_waiting:
            raw_line = ser.readline()
            
        if raw_line:
            decoded_line = raw_line.decode('utf-8', errors='ignore').strip()
            
            # Parsing CSV
            if ',' in decoded_line:
                raw_data = [float(x) for x in decoded_line.split(',')]
                
                # Validasi jumlah pixel (Harus 768)
                if len(raw_data) == WIDTH * HEIGHT:
                    # Reshape ke 2D Matrix
                    matrix = np.reshape(raw_data, (HEIGHT, WIDTH))
                    
                    # Update Gambar
                    heatmap.set_data(matrix)
                    
                    # Optional: Update judul dengan suhu Max
                    max_temp = np.max(matrix)
                    title_text.set_text(f"Thermal Camera - Max Temp: {max_temp:.1f}°C")
                    
                    last_frame = matrix
                    return [heatmap, title_text]
                    
    except ValueError:
        pass # Abaikan data corrupt (potongan kalimat)
    except Exception as e:
        print(f"Error: {e}")

    # Jika tidak ada data baru, kembalikan frame lama (agar tidak putih)
    return [heatmap, title_text]

# --- JALANKAN ANIMASI ---
# interval=30 berarti update setiap 30ms (sekitar 33 FPS)
ani = FuncAnimation(fig, update, interval=30, blit=True, cache_frame_data=False)

plt.show()