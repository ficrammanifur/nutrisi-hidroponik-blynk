// =================================================================
// 1. KONFIGURASI WIFIMANAGER & BLYNK
// =================================================================
#define BLYNK_TEMPLATE_ID "TMPL6ENxF696M"
#define BLYNK_TEMPLATE_NAME "Monitoring Hidroponik"
#define BLYNK_AUTH_TOKEN "9m6jA4bhAQCT-vRrYLtxhq-QMYnVCTBh"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiManager.h>
#include <LiquidCrystal_I2C.h>
#include <BlynkSimpleEsp32.h>
#include <OneWire.h>
#include <DallasTemperature.h>

char auth[] = BLYNK_AUTH_TOKEN;

// =================================================================
// 2. DEFINISI PIN HARDWARE
// =================================================================
#define phSensorPin 34       // ADC1_CH6
#define TdsSensorPin 35      // ADC1_CH7
#define ds18b20Pin 4         // Digital 1-Wire
#define trigPin 5            // Ultrasonik TRIG
#define echoPin 18           // Ultrasonik ECHO

#define RelaySirkulasi 19    // Pompa Sirkulasi
#define RelayNutA 25         // Dosing Nutrisi A
#define RelayNutB 26         // Dosing Nutrisi B

// =================================================================
// 3. DEFINISI VIRTUAL PIN BLYNK
// =================================================================
#define VPIN_Ph             V1 
#define VPIN_tds            V2 
#define VPIN_Suhu           V3 
#define VPIN_LevelAir       V4 
#define VPIN_PumpSirkulasi  V5 
#define VPIN_PumpNutA       V6
#define VPIN_PumpNutB       V7

// =================================================================
// 4. VARIABEL GLOBAL & OBJEK
// =================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2); // Konfigurasi LCD 16x2
OneWire oneWire(ds18b20Pin);
DallasTemperature sensorSuhu(&oneWire);
BlynkTimer timer;

WiFiManager wifiManager;
bool wifiConnected = false;

#define TANK_HEIGHT 70       // Ketinggian tandon penuh (dalam cm)
int sensorIndex = 0;         // Index untuk multiplexing layar LCD

// Status Aktuator
bool isSirkulasiON = false;
bool isNutA_ON = false;
bool isNutB_ON = false;

// =================================================================
// 5. KALIBRASI SENSOR (Diambil dari kode kedua)
// =================================================================

// ---------- KALIBRASI pH (3 titik) ----------
// Nama variabel diubah agar tidak bentrok dengan Blynk
const float pH_V4 = 1.405;  // Tegangan untuk pH 4.0
const float pH_PH4 = 4.00;
const float pH_V7 = 0.933;  // Tegangan untuk pH 6.86
const float pH_PH7 = 6.86;
const float pH_V9 = 0.552;  // Tegangan untuk pH 9.18
const float pH_PH9 = 9.18;

// ---------- KONSTANTA TDS ----------
#define VREF 3.3
#define ADC_RESOLUTION 4095.0

// ---------- BUFFER UNTUK AVERAGING ----------
#define AVG_SAMPLES 20
int phADCBuf[AVG_SAMPLES];
int phIdx = 0;
float phEMA = 7.0;
float phFiltered = 7.0;

int tdsADCBuf[AVG_SAMPLES];
int tdsIdx = 0;

// =================================================================
// 6. FUNGSI KALIBRASI (Diambil dari kode kedua)
// =================================================================

// ---------- FUNGSI KALIBRASI pH ----------
float calculatePH(float voltage) {
    // Interpolasi linear 3 titik kalibrasi
    if (voltage >= pH_V7) {
        // Antara pH 4.0 dan 6.86
        return pH_PH4 + (pH_PH7 - pH_PH4) * (pH_V4 - voltage) / (pH_V4 - pH_V7);
    } else {
        // Antara pH 6.86 dan 9.18
        return pH_PH7 + (pH_PH9 - pH_PH7) * (pH_V7 - voltage) / (pH_V7 - pH_V9);
    }
}

// ---------- FUNGSI KALIBRASI TDS (DFRobot) ----------
float calculateTDS_DFRobot(float voltage, float temp) {
    // Kompensasi suhu
    float tempCoeff = 1.0 + 0.02 * (temp - 25.0);
    float compVoltage = voltage / tempCoeff;
    
    // Formula DFRobot untuk TDS
    float tdsValue = (133.42 * compVoltage * compVoltage * compVoltage
                    - 255.86 * compVoltage * compVoltage
                    + 857.39 * compVoltage) * 0.5;
    
    if (tdsValue < 0) tdsValue = 0;
    if (tdsValue > 9999) tdsValue = 9999;
    
    return tdsValue;
}

// =================================================================
// 7. FUNGSI WIFIMANAGER
// =================================================================
void initWiFi() {
    wifiManager.setConfigPortalTimeout(120);
    wifiManager.setDebugOutput(false);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" WiFi Setup");
    lcd.setCursor(0, 1);
    lcd.print("Hotspot: HidroMon");
    delay(2000);
    
    Serial.println("[WIFI] Starting WiFiManager...");
    Serial.println("[WIFI] If not connected, open hotspot 'HidroMon'");
    
    bool connected = wifiManager.autoConnect("HidroMon", "hidro123");
    if (connected) {
        wifiConnected = true;
        Serial.printf("[WIFI] Connected to: %s\n", WiFi.SSID().c_str());
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" WiFi OK!");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP().toString());
        delay(2000);
    } else {
        wifiConnected = false;
        Serial.println("[WIFI] Timeout - running OFFLINE mode");
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" WiFi OFFLINE");
        lcd.setCursor(0, 1);
        lcd.print("Running Offline");
        delay(2000);
    }
}

// =================================================================
// 8. KONTROL MANUAL DARI APLIKASI BLYNK
// =================================================================
// Pastikan mode tombol di aplikasi Blynk diset ke "SWITCH", bukan "PUSH"
BLYNK_WRITE(VPIN_PumpSirkulasi) {
  isSirkulasiON = param.asInt();
  digitalWrite(RelaySirkulasi, isSirkulasiON ? HIGH : LOW);
}

BLYNK_WRITE(VPIN_PumpNutA) {
  isNutA_ON = param.asInt();
  digitalWrite(RelayNutA, isNutA_ON ? HIGH : LOW);
}

BLYNK_WRITE(VPIN_PumpNutB) {
  isNutB_ON = param.asInt();
  digitalWrite(RelayNutB, isNutB_ON ? HIGH : LOW);
}

// =================================================================
// 9. FUNGSI PEMBACAAN SENSOR & UPDATE LCD 16x2
// =================================================================
void sendSensor() {
    // TAMPILAN 0: SENSOR SUHU AIR
    if (sensorIndex == 0) {
      sensorSuhu.requestTemperatures();
      float suhuAir = sensorSuhu.getTempCByIndex(0);
      Blynk.virtualWrite(VPIN_Suhu, suhuAir);

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Suhu Air:");
      lcd.setCursor(0, 1); lcd.print(suhuAir, 1); lcd.print((char)223); lcd.print("C");
      
      // Tampilkan status WiFi di pojok
      if (wifiConnected) {
        lcd.setCursor(15, 0);
        lcd.print("W");
      }
    }

    // TAMPILAN 1: SENSOR ULTRASONIK (JARAK & LEVEL AKTUAL)
    else if (sensorIndex == 1) {
      digitalWrite(trigPin, LOW); delayMicroseconds(2);
      digitalWrite(trigPin, HIGH); delayMicroseconds(10);
      digitalWrite(trigPin, LOW);

      // Timeout 30.000 mikrodetik (maksimal deteksi sekitar 5 meter)
      long duration = pulseIn(echoPin, HIGH, 30000); 
      float distance_cm;

      // Cek Error / Timeout (Jika gagal baca, asumsikan kosong)
      if (duration == 0) {
        distance_cm = TANK_HEIGHT; 
      } else {
        distance_cm = (float)duration / 58.2;
      }

      // Cek Batas Maksimal Jarak (Mencegah Bottleneck pembacaan tembus lantai tandon)
      if (distance_cm > TANK_HEIGHT) {
        distance_cm = TANK_HEIGHT;
      }

      // Kalkulasi Level Air Aktual
      float levelAir = TANK_HEIGHT - distance_cm;
      
      // Mencegah level air menjadi angka negatif 
      if (levelAir < 0) {
        levelAir = 0;
      }

      Blynk.virtualWrite(VPIN_LevelAir, levelAir);
      
      // Update Layar
      lcd.clear();
      lcd.setCursor(0, 0); 
      lcd.print("Jarak: "); lcd.print(distance_cm, 1); lcd.print("cm"); 
      lcd.setCursor(0, 1); 
      lcd.print("Level: "); lcd.print(levelAir, 1); lcd.print("cm");
      
      if (wifiConnected) {
        lcd.setCursor(15, 0);
        lcd.print("W");
      }
    }

    // TAMPILAN 2: SENSOR pH (DENGAN KALIBRASI 3 TITIK)
    else if (sensorIndex == 2) {
      // Baca ADC dan masukkan ke buffer
      phADCBuf[phIdx] = analogRead(phSensorPin);
      phIdx = (phIdx + 1) % AVG_SAMPLES;
      
      // Hitung rata-rata
      long sum = 0;
      for (int i = 0; i < AVG_SAMPLES; i++) {
        sum += phADCBuf[i];
      }
      
      float avgADC = sum / (float)AVG_SAMPLES;
      float voltage = avgADC * (VREF / ADC_RESOLUTION);
      float phRaw = calculatePH(voltage);  // Menggunakan kalibrasi 3 titik
      
      // Filter EMA untuk smoothing
      phEMA = (0.85 * phEMA) + (0.15 * phRaw);
      phFiltered = constrain(phEMA, 0.0, 14.0);
      float pHValue = phFiltered;
      
      Blynk.virtualWrite(VPIN_Ph, pHValue);
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("pH Larutan:");
      lcd.setCursor(0, 1); lcd.print(pHValue, 2);  // Tampilkan 2 desimal
      
      if (wifiConnected) {
        lcd.setCursor(15, 0);
        lcd.print("W");
      }
    }

    // TAMPILAN 3: SENSOR TDS/EC (DENGAN FORMULA DFRobot)
    else if (sensorIndex == 3) {
      // Baca ADC dan masukkan ke buffer
      int adcValue = analogRead(TdsSensorPin);
      tdsADCBuf[tdsIdx] = adcValue;
      tdsIdx = (tdsIdx + 1) % AVG_SAMPLES;
      
      // Hitung rata-rata
      long sum = 0;
      for (int i = 0; i < AVG_SAMPLES; i++) {
        sum += tdsADCBuf[i];
      }
      int avgADC = sum / AVG_SAMPLES;
      
      float voltage = avgADC * (VREF / ADC_RESOLUTION);
      sensorSuhu.requestTemperatures();
      float suhuAir = sensorSuhu.getTempCByIndex(0);  // Baca suhu untuk kompensasi
      float tdsValue = calculateTDS_DFRobot(voltage, suhuAir);  // Menggunakan formula DFRobot
      
      Blynk.virtualWrite(VPIN_tds, tdsValue);
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Nutrisi TDS:");
      lcd.setCursor(0, 1); lcd.print(tdsValue, 0); lcd.print(" PPM");
      
      if (wifiConnected) {
        lcd.setCursor(15, 0);
        lcd.print("W");
      }
    }

    // TAMPILAN 4: STATUS AKTUATOR
    else if (sensorIndex == 4) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("S:"); lcd.print(isSirkulasiON ? "ON " : "OFF");
      lcd.print(" NA:"); lcd.print(isNutA_ON ? "ON" : "OFF");
      
      lcd.setCursor(0, 1);
      lcd.print("NB:"); lcd.print(isNutB_ON ? "ON " : "OFF");
      
      if (wifiConnected) {
        lcd.setCursor(15, 0);
        lcd.print("W");
      }
    }

    // Lanjut ke halaman sensor berikutnya
    sensorIndex++; 
    if (sensorIndex > 4) { 
      sensorIndex = 0; 
    }
}

// =================================================================
// SETUP & LOOP
// =================================================================
void setup() {
  Serial.begin(115200);
  
  // Inisialisasi Sensor Suhu
  sensorSuhu.begin();

  // Inisialisasi buffer
  for (int i = 0; i < AVG_SAMPLES; i++) {
    phADCBuf[i] = 2048;
    tdsADCBuf[i] = 0;
  }

  // Konfigurasi Pin I/O
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(RelaySirkulasi, OUTPUT);
  pinMode(RelayNutA, OUTPUT);
  pinMode(RelayNutB, OUTPUT);

  // Paksa semua relay OFF saat booting
  digitalWrite(RelaySirkulasi, LOW);
  digitalWrite(RelayNutA, LOW);
  digitalWrite(RelayNutB, LOW);

  // Inisialisasi Layar LCD 16x2
  lcd.init();
  lcd.backlight();
  
  // Splash Screen
  lcd.setCursor((16-10)/2, 0); 
  lcd.print("HIDROPONIK");
  lcd.setCursor((16-10)/2, 1); 
  lcd.print("IOT SYSTEM");
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Init WiFi...");
  delay(1000);

  // ============================================================
  // INISIALISASI WIFIMANAGER (GANTI Blynk.begin)
  // ============================================================
  initWiFi();
  
  // Mulai koneksi Blynk setelah WiFi terkoneksi
  if (wifiConnected) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting Blynk");
    lcd.setCursor(0, 1);
    lcd.print("Please Wait...");
    
    Blynk.config(auth);
    if (Blynk.connect()) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Blynk Connected!");
      lcd.setCursor(0, 1);
      lcd.print("Ready");
      delay(1500);
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Blynk Failed!");
      lcd.setCursor(0, 1);
      lcd.print("Running Offline");
      delay(1500);
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Running Offline");
    lcd.setCursor(0, 1);
    lcd.print("No WiFi");
    delay(1500);
  }

  // Jalankan fungsi sendSensor setiap 1000ms (1 detik)
  timer.setInterval(1000L, sendSensor); 
  
  // Informasi kalibrasi di Serial
  Serial.println("\n========================================");
  Serial.println(" HIDROPONIK MONITORING SYSTEM");
  Serial.println("========================================");
  Serial.println("[WIFI] Using WiFiManager");
  Serial.printf("[WIFI] Status: %s\n", wifiConnected ? "Connected" : "Offline");
  if (wifiConnected) {
    Serial.printf("[WIFI] SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.println("[PH] Kalibrasi 3 titik:");
  Serial.printf("  pH 4.00 -> V=%.3fV\n", pH_V4);
  Serial.printf("  pH 6.86 -> V=%.3fV\n", pH_V7);
  Serial.printf("  pH 9.18 -> V=%.3fV\n", pH_V9);
  Serial.println("[TDS] Menggunakan formula DFRobot dengan kompensasi suhu");
  Serial.println("========================================\n");
  
  lcd.clear();
}

void loop() {
  if (wifiConnected) {
    Blynk.run();
  }
  timer.run();
}
