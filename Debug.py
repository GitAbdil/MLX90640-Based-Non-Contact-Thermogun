import serial
import time

# Pastikan Port Benar
SERIAL_PORT = 'COM14' 
BAUD_RATE = 115200  # Sesuai kode ESP32 Anda

try:
    print(f"Membuka port {SERIAL_PORT}...")
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print("Port terbuka. Menunggu data dari ESP32...")
    
    while True:
        if ser.in_waiting > 0:
            try:
                # Baca satu baris
                line = ser.readline().decode('utf-8').strip()
                print(f"Data Diterima: {line[:50]}...") # Tampilkan 50 karakter pertama
            except:
                print("Data error (bukan text)")
        else:
            # Jika tidak ada data, print titik setiap detik untuk tanda script jalan
            print(".", end="", flush=True)
            time.sleep(1)
            
except KeyboardInterrupt:
    print("\nKeluar.")
except Exception as e:
    print(f"Error: {e}")