#include <LiquidCrystal_I2C.h>

// =================================================================
// 1. DEFINISI PIN & VARIABEL ULTRASONIK
// =================================================================
#define trigPin 5            // Ultrasonik TRIG terhubung ke GPIO 5
#define echoPin 18           // Ultrasonik ECHO terhubung ke GPIO 18

// Ketinggian tandon penuh disesuaikan dengan box hidroponik
#define TANK_HEIGHT 40       

// Konfigurasi LCD 16x2 (Alamat I2C umumnya 0x27)
LiquidCrystal_I2C lcd(0x27, 16, 2); 

void setup() {
  // Mulai komunikasi serial untuk debugging di laptop
  Serial.begin(115200);
  
  // Konfigurasi Pin
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Inisialisasi Layar LCD
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0); 
  lcd.print("TEST ULTRASONIK");
  lcd.setCursor(0, 1);
  lcd.print("Mulai membaca...");
  delay(2000); 
  
  lcd.clear();
}

void loop() {
  // ---------------------------------------------------------
  // 1. TRIGGER SENSOR (Kirim Sinyal Suara)
  // ---------------------------------------------------------
  digitalWrite(trigPin, LOW); 
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); 
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // ---------------------------------------------------------
  // 2. BACA PANTULAN & KALKULASI JARAK
  // ---------------------------------------------------------
  // Timeout 30.000 mikrodetik agar tidak nge-hang jika sensor tidak memantul
  long duration = pulseIn(echoPin, HIGH, 30000); 
  float distance_cm;

  // Cek Error / Timeout (Jika gagal baca, asumsikan jarak mentok ke dasar box)
  if (duration == 0) {
    distance_cm = TANK_HEIGHT; 
  } else {
    distance_cm = (float)duration / 58.2;
  }

  // Cek Batas Maksimal Jarak (Mencegah Bottleneck pembacaan tembus dasar)
  if (distance_cm > TANK_HEIGHT) {
    distance_cm = TANK_HEIGHT;
  }

  // ---------------------------------------------------------
  // 3. KALKULASI LEVEL AIR AKTUAL
  // ---------------------------------------------------------
  float levelAir = TANK_HEIGHT - distance_cm;
  
  // Mencegah level air menjadi angka negatif 
  if (levelAir < 0) {
    levelAir = 0;
  }

  // ---------------------------------------------------------
  // 4. TAMPILKAN HASIL KE SERIAL MONITOR
  // ---------------------------------------------------------
  Serial.print("Jarak: "); 
  Serial.print(distance_cm); 
  Serial.print(" cm | Level Air Aktual: "); 
  Serial.print(levelAir); 
  Serial.println(" cm");

  // ---------------------------------------------------------
  // 5. TAMPILKAN HASIL KE LCD 16x2
  // ---------------------------------------------------------
  lcd.setCursor(0, 0); 
  lcd.print("Jarak: "); lcd.print(distance_cm, 1); lcd.print("cm   "); 
  
  lcd.setCursor(0, 1); 
  lcd.print("Level: "); lcd.print(levelAir, 1); lcd.print("cm   ");

  // Jeda 1 detik sebelum membaca ulang
  delay(1000); 
}
