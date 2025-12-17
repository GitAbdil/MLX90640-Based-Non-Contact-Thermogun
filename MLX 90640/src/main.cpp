#include <Arduino.h>
#include <TFT_eSPI.h> 
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(); 

// --- HARDWARE PIN ---
// TX Sensor -> GPIO 21 (ESP32)
// RX Sensor -> GPIO 22 (ESP32)
#define RX_PIN 21 
#define TX_PIN 22 

// --- TUNING AKURASI ---
// Offset kita nol-kan karena Raw sudah akurat menurut pengujianmu
float calibrationOffset = 0.0; 

// Ambang batas demam (Sesuai standar medis untuk suhu permukaan dahi)
// Jika Raw, biasanya 35.5 ke atas di dahi sudah tergolong hangat/demam
float feverThreshold = 37.0;   

// --- VARIABEL SYSTEM ---
float pixelMap[768]; 
float minTemp = 25.0; // Range visualisasi dipersempit agar kontras wajah lebih jelas
float maxTemp = 38.0;

// Variabel untuk algoritma smoothing
float displayedTemp = 0.0; 
float rawMaxTemp = 0.0;

uint16_t camColors[256];
uint8_t buffer[2000];
int bufIdx = 0;
unsigned long lastDataTime = 0;

// Palet Warna: Ironbow (Lebih standar industri daripada pelangi biasa)
// Hitam/Biru (Dingin) -> Ungu -> Oranye -> Kuning -> Putih (Panas)
void createPalette() {
  for (int i = 0; i < 256; i++) {
    uint8_t r, g, b;
    if (i < 64) { 
      r = 0; g = 0; b = i * 4; // Hitam ke Biru
    } else if (i < 128) { 
      r = (i - 64) * 4; g = 0; b = 255; // Biru ke Ungu
    } else if (i < 192) { 
      r = 255; g = (i - 128) * 4; b = 255 - ((i - 128) * 4); // Ungu ke Oranye
    } else { 
      r = 255; g = 255; b = (i - 192) * 4; // Kuning ke Putih
    }
    camColors[i] = tft.color565(r, g, b);
  }
}

void drawThermalImage() {
  tft.setViewport(40, 18, 240, 135);

  // 1. Gambar Heatmap
  for (int y = 0; y < 24; y++) {
    for (int x = 0; x < 32; x++) {
      int xPos = x * 240 / 32;
      int yPos = y * 135 / 24;
      int rectW = ((x + 1) * 240 / 32) - xPos;
      int rectH = ((y + 1) * 135 / 24) - yPos;

      float val = pixelMap[y * 32 + x];
      // Constrain agar warna tidak noise oleh benda sangat dingin/panas
      int colorIdx = map(constrain((int)val, (int)minTemp, (int)maxTemp), (int)minTemp, (int)maxTemp, 0, 255);
      tft.fillRect(xPos, yPos, rectW, rectH, camColors[colorIdx]);
    }
  }
  
  // 2. Crosshair (Bidikan Tengah)
  int centerX = 120; int centerY = 67;
  tft.drawFastHLine(centerX - 10, centerY, 20, TFT_WHITE);
  tft.drawFastVLine(centerX, centerY - 10, 20, TFT_WHITE);
  
  // 3. HUD Display (Tampilan Angka)
  // Background kotak pojok kiri atas
  tft.fillRect(5, 5, 90, 50, TFT_BLACK); 
  tft.drawRect(5, 5, 90, 50, TFT_WHITE);

  // Logika Warna Status
  if (displayedTemp >= feverThreshold) {
      tft.setTextColor(TFT_RED, TFT_BLACK); // Demam
      tft.drawRect(4, 4, 92, 52, TFT_RED);  // Border merah tebal
  } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK); // Normal
  }
  
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.print("TEMP (C):");
  
  tft.setTextSize(3); // Font Besar agar mudah dibaca
  tft.setCursor(10, 25);
  tft.print(displayedTemp, 1); // 1 angka desimal (presisi)
}

void showNoSignal() {
  tft.setViewport(40, 18, 240, 135);
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(40, 50);
  tft.println("SENSOR ERROR");
}

void setup() {
  Serial.begin(115200);

  // Backlight LCD
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH); 

  tft.init();
  tft.setRotation(1);
  
  // Setup Viewport
  tft.setViewport(40, 18, 240, 135);
  tft.fillScreen(TFT_BLACK);
  
  // Loading Screen
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(60, 60);
  tft.println("Init System...");

  // Init Serial Sensor (GY-MCU90640)
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // Kirim Command Start
  byte cmd[] = {0xA5, 0x45, 0xEA};
  Serial1.write(cmd, 3);
  
  createPalette();
  lastDataTime = millis();
}

void loop() {
  while (Serial1.available()) {
    buffer[bufIdx++] = Serial1.read();
    lastDataTime = millis();
    
    if (bufIdx >= 2000) bufIdx = 0; 
    
    if (bufIdx >= 1544) {
      for (int i = 0; i < bufIdx - 1540; i++) {
        if (buffer[i] == 0x5A && buffer[i+1] == 0x5A) {
          int dataStart = i + 4; 
          if ((bufIdx - dataStart) < 1536) continue; 

          float currentMax = -100.0;

          // Parsing Data
          for (int p = 0; p < 768; p++) {
            uint8_t low  = buffer[dataStart + (p * 2)];
            uint8_t high = buffer[dataStart + (p * 2) + 1];
            int16_t raw = (int16_t)(high << 8 | low);
            float temp = raw / 100.0; 
            
            // Filter Glitch Ekstrem
            if(temp > -10 && temp < 100) {
              pixelMap[p] = temp;
              // Cari suhu maksimum (Hotspot Tracking)
              if (temp > currentMax) currentMax = temp;
            } else {
               pixelMap[p] = 30.0; // Nilai aman jika error
            }
          }

          rawMaxTemp = currentMax + calibrationOffset;

          // --- ALGORITMA SMART SMOOTHING ---
          // Jika ini data pertama, langsung pakai
          if (displayedTemp == 0) displayedTemp = rawMaxTemp;

          float diff = rawMaxTemp - displayedTemp;

          // Jika perubahan suhunya BESAR (> 0.5 derajat), update cepat (Fast Response)
          // Ini berguna saat Anda pindah dari menembak dinding ke menembak dahi orang
          if (abs(diff) > 0.5) {
             displayedTemp += diff * 0.6; // Ambil 60% perubahan langsung
          } 
          // Jika perubahan suhunya KECIL (sedang mengukur dahi stabil), update lambat (Smooth)
          // Ini membuat angka tidak loncat-loncat (36.1 -> 36.2 -> 36.1)
          else {
             displayedTemp += diff * 0.1; // Hanya ambil 10% perubahan (Filter noise)
          }

          drawThermalImage();
          bufIdx = 0; 
          break; 
        }
      }
    }
  }

  // Timeout Check (3 detik)
  if (millis() - lastDataTime > 3000) {
    showNoSignal();
    lastDataTime = millis();
    byte cmd[] = {0xA5, 0x45, 0xEA}; // Re-init sensor
    Serial1.write(cmd, 3);
  }
}