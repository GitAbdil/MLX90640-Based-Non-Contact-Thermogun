#include <Arduino.h>
#include <TFT_eSPI.h> 
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(); 

// --- PIN KONEKSI SENSOR ---
#define RX_PIN 21 
#define TX_PIN 22 

// Buffer Data
float pixelMap[768]; 
float minTemp = 20.0;
float maxTemp = 40.0;
uint16_t camColors[256];
uint8_t buffer[2000];
int bufIdx = 0;

// --- FUNGSI 1: TAMPILKAN KE LAYAR ESP32 ---
void drawThermalImage() {
  // Resolusi 320x170 (Landscape)
  // Sensor 32x24 -> Scale Up
  int rectW = 10; // Lebar kotak pixel di layar
  int rectH = 7;  // Tinggi kotak pixel di layar

  for (int y = 0; y < 24; y++) {
    for (int x = 0; x < 32; x++) {
      float val = pixelMap[y * 32 + x];
      
      // Mapping suhu ke warna
      int colorIdx = map(constrain((int)val, (int)minTemp, (int)maxTemp), (int)minTemp, (int)maxTemp, 0, 255);
      
      // Gambar kotak
      tft.fillRect(x * rectW, y * rectH, rectW, rectH, camColors[colorIdx]);
    }
  }
  
  // Tampilkan Suhu Max/Min di Samping Kanan
  // Pastikan area ini tidak menimpa gambar thermal
  tft.fillRect(260, 0, 60, 170, TFT_BLACK); // Clear area teks
  tft.setTextColor(TFT_WHITE, TFT_BLACK); 
  tft.setTextSize(1);
  tft.setCursor(265, 10);
  tft.print("Max:"); 
  tft.setCursor(265, 20);
  tft.print(maxTemp, 1);
  
  tft.setCursor(265, 40);
  tft.print("Min:"); 
  tft.setCursor(265, 50);
  tft.print(minTemp, 1);
}

// --- FUNGSI 2: KIRIM DATA KE PYTHON (LAPTOP) ---
void sendToPython() {
  // Format: nilai1,nilai2,nilai3,....,nilai768\n
  // Kita kirim data mentah agar Python bisa melakukan interpolasi (halus)
  for (int i = 0; i < 768; i++) {
    Serial.print(pixelMap[i], 2); // 2 angka di belakang koma
    if (i < 767) {
      Serial.print(","); // Pemisah
    }
  }
  Serial.println(); // Baris baru tanda akhir frame
}

void setup() {
  // PENTING: Baudrate tinggi untuk komunikasi ke Laptop
  Serial.begin(460800); 

  // Nyalakan Layar
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH); 

  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);
  
  // Layar Sambutan
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 50);
  tft.println("Thermal Dual Mode");
  tft.setTextSize(1);
  tft.setCursor(10, 80);
  tft.println("LCD: ON | USB Serial: ON");
  
  // Init Serial Sensor (UART ke Sensor)
  // Tetap 115200 sesuai spesifikasi sensor GY-MCU90640
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // Kirim perintah Start ke Sensor
  byte cmd[] = {0xA5, 0x45, 0xEA};
  Serial1.write(cmd, 3);
  
  delay(1500);
  tft.fillScreen(TFT_BLACK);

  // Buat Palet Warna (Thermal Rainbow) untuk LCD
  for (int i = 0; i < 256; i++) {
    uint8_t r, g, b;
    if (i < 128) { 
        r = 0; g = i * 2; b = 255 - (i * 2);
    } else { 
        r = (i - 128) * 2; g = 255 - ((i - 128) * 2); b = 0;
    }
    camColors[i] = tft.color565(r, g, b);
  }
}

void loop() {
  // Baca Data Serial dari Sensor
  while (Serial1.available()) {
    buffer[bufIdx++] = Serial1.read();
    
    // Safety agar buffer tidak overflow
    if (bufIdx >= 2000) bufIdx = 0;
    
    // Cek Header Sensor: 0x5A 0x5A (Sesuai protokol GY-MCU90640)
    // Frame lengkap biasanya sekitar 1544 byte
    if (bufIdx >= 1544) {
      for (int i = 0; i < bufIdx - 1540; i++) {
        if (buffer[i] == 0x5A && buffer[i+1] == 0x5A) {
          int dataStart = i + 4; 
          
          // Pastikan sisa data cukup untuk 768 pixel (768 * 2 byte = 1536)
          if ((bufIdx - dataStart) < 1536) continue; 

          float currentMax = -100;
          float currentMin = 100;

          // Parsing data
          for (int p = 0; p < 768; p++) {
            uint8_t low  = buffer[dataStart + (p * 2)];
            uint8_t high = buffer[dataStart + (p * 2) + 1];
            int16_t raw = (int16_t)(high << 8 | low);
            float temp = raw / 100.0;
            
            // Filter Data Sampah (Kadang ada glitch 0 atau sangat tinggi)
            if(temp > -40 && temp < 300) {
              pixelMap[p] = temp;
              if (temp > currentMax) currentMax = temp;
              if (temp < currentMin) currentMin = temp;
            } else {
               // Jika error, pakai nilai tetangga atau default 25
               pixelMap[p] = 25.0; 
            }
          }
          
          minTemp = currentMin;
          maxTemp = currentMax;

          // --- TUGAS 1: Update Layar ESP32 ---
          drawThermalImage();

          // --- TUGAS 2: Kirim ke Laptop (Python) ---
          sendToPython();

          // Reset Buffer untuk frame berikutnya
          bufIdx = 0; 
          break; 
        }
      }
    }
  }
}