// =================================================================
// 1. KONFIGURASI WIFIMANAGER & BLYNK
// =================================================================
#define BLYNK_TEMPLATE_ID "Your_ID"
#define BLYNK_TEMPLATE_NAME "Your_Project_Name"
#define BLYNK_AUTH_TOKEN "Your_Auth_Token"
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
LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(ds18b20Pin);
DallasTemperature sensorSuhu(&oneWire);
BlynkTimer timer;

WiFiManager wifiManager;
bool wifiConnected = false;

#define TANK_HEIGHT 70.0
int halamanLCD = 0;
unsigned long lastLCDSwitch = 0;
const unsigned long LCD_INTERVAL = 3000; // 3 detik

// Status Aktuator
bool isSirkulasiON = false;
bool isNutA_ON = false;
bool isNutB_ON = false;

// Nilai Sensor
float pHValue = 7.0;
float tdsValue = 0.0;
float suhuAir = 25.0;
float levelAir = 0.0;

// =================================================================
// 5. KALIBRASI SENSOR pH (DATA REAL ANDA)
// =================================================================
const float pH_V4 = 1.405;  const float pH_PH4 = 4.00;
const float pH_V7 = 0.933;  const float pH_PH7 = 6.86;
const float pH_V9 = 0.552;  const float pH_PH9 = 9.18;

// =================================================================
// 6. KONSTANTA TDS
// =================================================================
#define VREF 3.3
#define ADC_RESOLUTION 4095.0

// =================================================================
// 7. BUFFER UNTUK AVERAGING
// =================================================================
#define AVG_SAMPLES 20
int phADCBuf[AVG_SAMPLES];
int phIdx = 0;
float phEMA = 7.0;

int tdsADCBuf[AVG_SAMPLES];
int tdsIdx = 0;

// =================================================================
// 8. FUNGSI KALIBRASI
// =================================================================

// ---------- FUNGSI KALIBRASI pH ----------
float calculatePH(float voltage) {
    if (voltage >= pH_V7) {
        return pH_PH4 + (pH_PH7 - pH_PH4) * (pH_V4 - voltage) / (pH_V4 - pH_V7);
    } else {
        return pH_PH7 + (pH_PH9 - pH_PH7) * (pH_V7 - voltage) / (pH_V7 - pH_V9);
    }
}

// ---------- FUNGSI KALIBRASI TDS (DFRobot) ----------
float calculateTDS_DFRobot(float voltage, float temp) {
    float tempCoeff = 1.0 + 0.02 * (temp - 25.0);
    float compVoltage = voltage / tempCoeff;
    
    float tdsValue = (133.42 * compVoltage * compVoltage * compVoltage
                    - 255.86 * compVoltage * compVoltage
                    + 857.39 * compVoltage) * 0.5;
    
    if (tdsValue < 0) tdsValue = 0;
    if (tdsValue > 9999) tdsValue = 9999;
    
    return tdsValue;
}

// =================================================================
// 9. FUNGSI WIFIMANAGER
// =================================================================
void initWiFi() {
    wifiManager.setConfigPortalTimeout(120);
    wifiManager.setDebugOutput(false);
    
    Serial.println("[WIFI] Starting WiFiManager...");
    Serial.println("[WIFI] Hotspot: HidroMon (password: hidro123)");
    
    bool connected = wifiManager.autoConnect("HidroMon", "hidro123");
    if (connected) {
        wifiConnected = true;
        Serial.printf("[WIFI] Connected to: %s\n", WiFi.SSID().c_str());
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        wifiConnected = false;
        Serial.println("[WIFI] Timeout - running OFFLINE mode");
    }
}

// =================================================================
// 10. KONTROL MANUAL DARI APLIKASI BLYNK
// =================================================================

// Relay Sirkulasi (Normal: HIGH=ON, LOW=OFF)
BLYNK_WRITE(VPIN_PumpSirkulasi) {
    isSirkulasiON = param.asInt();
    digitalWrite(RelaySirkulasi, isSirkulasiON ? HIGH : LOW);
}

// Relay Nut A (AKTIF LOW: LOW=ON, HIGH=OFF) - DIBALIK
BLYNK_WRITE(VPIN_PumpNutA) {
    isNutA_ON = param.asInt();
    if (isNutA_ON) {
        digitalWrite(RelayNutA, LOW);   // ON (aktif LOW)
    } else {
        digitalWrite(RelayNutA, HIGH);  // OFF
    }
}

// Relay Nut B (AKTIF LOW: LOW=ON, HIGH=OFF) - DIBALIK
BLYNK_WRITE(VPIN_PumpNutB) {
    isNutB_ON = param.asInt();
    if (isNutB_ON) {
        digitalWrite(RelayNutB, LOW);   // ON (aktif LOW)
    } else {
        digitalWrite(RelayNutB, HIGH);  // OFF
    }
}

// =================================================================
// 11. FUNGSI PEMBACAAN SENSOR
// =================================================================

// ---------- BACA pH ----------
float bacaPH() {
    phADCBuf[phIdx] = analogRead(phSensorPin);
    phIdx = (phIdx + 1) % AVG_SAMPLES;
    
    long sum = 0;
    for (int i = 0; i < AVG_SAMPLES; i++) {
        sum += phADCBuf[i];
    }
    
    float avgADC = sum / (float)AVG_SAMPLES;
    float voltage = avgADC * (VREF / ADC_RESOLUTION);
    float phRaw = calculatePH(voltage);
    
    phEMA = (0.85 * phEMA) + (0.15 * phRaw);
    return constrain(phEMA, 0.0, 14.0);
}

// ---------- BACA TDS ----------
float bacaTDS(float suhu) {
    int adcValue = analogRead(TdsSensorPin);
    tdsADCBuf[tdsIdx] = adcValue;
    tdsIdx = (tdsIdx + 1) % AVG_SAMPLES;
    
    long sum = 0;
    for (int i = 0; i < AVG_SAMPLES; i++) {
        sum += tdsADCBuf[i];
    }
    int avgADC = sum / AVG_SAMPLES;
    
    float voltage = avgADC * (VREF / ADC_RESOLUTION);
    return calculateTDS_DFRobot(voltage, suhu);
}

// ---------- BACA SUHU ----------
float bacaSuhu() {
    sensorSuhu.requestTemperatures();
    float t = sensorSuhu.getTempCByIndex(0);
    if (t > -50 && t < 125) {
        return t;
    }
    return 25.0;
}

// ---------- BACA ULTRASONIK ----------
float bacaUltrasonik() {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    
    long duration = pulseIn(echoPin, HIGH, 30000);
    float distance_cm;
    
    if (duration == 0) {
        distance_cm = TANK_HEIGHT;
    } else {
        distance_cm = (float)duration / 58.2;
    }
    
    if (distance_cm > TANK_HEIGHT) {
        distance_cm = TANK_HEIGHT;
    }
    
    return distance_cm;
}

// ---------- BACA SEMUA SENSOR ----------
void bacaSemuaSensor() {
    suhuAir = bacaSuhu();
    pHValue = bacaPH();
    tdsValue = bacaTDS(suhuAir);
    
    float jarak = bacaUltrasonik();
    levelAir = TANK_HEIGHT - jarak;
    if (levelAir < 0) levelAir = 0;
    
    // Kirim ke Blynk
    if (wifiConnected) {
        Blynk.virtualWrite(VPIN_Ph, pHValue);
        Blynk.virtualWrite(VPIN_tds, tdsValue);
        Blynk.virtualWrite(VPIN_Suhu, suhuAir);
        Blynk.virtualWrite(VPIN_LevelAir, levelAir);
    }
}

// =================================================================
// 12. FUNGSI UPDATE LCD (3 Halaman)
// =================================================================
void updateLCD() {
    unsigned long now = millis();
    if (now - lastLCDSwitch >= LCD_INTERVAL) {
        lastLCDSwitch = now;
        halamanLCD++;
        if (halamanLCD > 2) halamanLCD = 0;
    }
    
    lcd.clear();
    
    switch (halamanLCD) {
        // ==================== HALAMAN 1 ====================
        case 0:
            // Baris 0: pH
            lcd.setCursor(0, 0);
            lcd.print("pH:");
            lcd.print(pHValue, 2);
            
            // Baris 1: TDS + Suhu
            lcd.setCursor(0, 1);
            lcd.print("TDS:");
            lcd.print(tdsValue, 0);
            lcd.print(" ");
            lcd.print(suhuAir, 1);
            lcd.print((char)223);
            lcd.print("C");
            break;
        
        // ==================== HALAMAN 2 ====================
        case 1:
            // Baris 0: Level Air
            lcd.setCursor(0, 0);
            lcd.print("Level:");
            lcd.print(levelAir, 1);
            lcd.print("cm");
            
            // Baris 1: Status Pompa Sirkulasi
            lcd.setCursor(0, 1);
            lcd.print("Pump : ");
            lcd.print(isSirkulasiON ? "ON " : "OFF");
            break;
        
        // ==================== HALAMAN 3 ====================
        case 2:
            // Baris 0: Status Nutrisi A & B
            lcd.setCursor(0, 0);
            lcd.print("NutA:");
            lcd.print(isNutA_ON ? "ON " : "OFF");
            lcd.print(" NutB:");
            lcd.print(isNutB_ON ? "ON " : "OFF");
            
            // Baris 1: Mode (Online/Offline)
            lcd.setCursor(0, 1);
            lcd.print("Mode: ");
            lcd.print(wifiConnected ? "ONLINE " : "OFFLINE");
            break;
    }
}

// =================================================================
// 13. SETUP
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
    
    // Konfigurasi Pin
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    pinMode(RelaySirkulasi, OUTPUT);
    pinMode(RelayNutA, OUTPUT);
    pinMode(RelayNutB, OUTPUT);
    
    // Paksa semua relay OFF saat booting
    digitalWrite(RelaySirkulasi, LOW);   // OFF
    digitalWrite(RelayNutA, HIGH);        // OFF (karena aktif LOW)
    digitalWrite(RelayNutB, HIGH);        // OFF (karena aktif LOW)
    
    // Inisialisasi LCD
    lcd.init();
    lcd.backlight();
    
    // Splash Screen
    lcd.setCursor(0, 0);
    lcd.print(" HIDROPONIK ");
    lcd.setCursor(0, 1);
    lcd.print(" MONITORING ");
    delay(2000);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" Init WiFi...");
    delay(1000);
    
    // Inisialisasi WiFiManager
    initWiFi();
    
    // Koneksi Blynk
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
            lcd.print("Offline Mode");
            delay(1500);
        }
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Offline Mode");
        lcd.setCursor(0, 1);
        lcd.print("No WiFi");
        delay(1500);
    }
    
    // Timer untuk baca sensor setiap 1 detik
    timer.setInterval(1000L, bacaSemuaSensor);
    
    // Timer untuk update LCD setiap 100ms (tapi switch halaman tiap 3 detik)
    timer.setInterval(100L, updateLCD);
    
    // Informasi di Serial
    Serial.println("\n========================================");
    Serial.println(" HIDROPONIK MONITORING SYSTEM");
    Serial.println("========================================");
    Serial.println("[WIFI] WiFiManager Active");
    Serial.printf("[WIFI] Status: %s\n", wifiConnected ? "Connected" : "Offline");
    if (wifiConnected) {
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    }
    Serial.println("[PH] 3-Point Calibration:");
    Serial.printf("  pH 4.00 -> V=%.3fV\n", pH_V4);
    Serial.printf("  pH 6.86 -> V=%.3fV\n", pH_V7);
    Serial.printf("  pH 9.18 -> V=%.3fV\n", pH_V9);
    Serial.println("[TDS] DFRobot Formula with Temp Compensation");
    Serial.println("[RELAY] Nut A & B: Active LOW (LOW=ON, HIGH=OFF)");
    Serial.println("[LCD] 3 Pages, switch every 3 seconds");
    Serial.println("========================================\n");
    
    lcd.clear();
}

// =================================================================
// 14. LOOP
// =================================================================
void loop() {
    if (wifiConnected) {
        Blynk.run();
    }
    timer.run();
}
