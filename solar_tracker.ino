#define BLYNK_TEMPLATE_ID   "TMPL64aXqIq-u"      // ID unik template proyek Blynk milikmu
#define BLYNK_TEMPLATE_NAME "Solar Tracker"      // Nama proyek di Blynk
#define BLYNK_AUTH_TOKEN    "R8hSLZ1LiUt_eYZZ3-z2HGqNwptS6O8b" // Kode otentikasi agar ESP32 bisa masuk ke akun Blynkmu
#define BLYNK_PRINT Serial                        // Mengaktifkan fitur cetak log debug Blynk ke Serial Monitor

#include <ESP32Servo.h>     // Library untuk mengendalikan motor servo khusus chip ESP32
#include <Wire.h>           // Library untuk komunikasi I2C (digunakan oleh OLED dan INA219)
#include <Adafruit_GFX.h>   // Library grafis dasar dari Adafruit untuk membuat text/gambar
#include <Adafruit_SSD1306.h> // Library khusus untuk mengendalikan driver layar OLED SSD1306
#include <Adafruit_INA219.h> // Library untuk sensor INA219 (sensor pengukur arus/tegangan/daya)
#include <WiFi.h>           // Library internal ESP32 untuk mengaktifkan fitur Wi-Fi
#include <WiFiClient.h>     // Library pendukung untuk membuat koneksi client internet
#include <BlynkSimpleEsp32.h> // Library utama untuk menghubungkan ESP32 dengan Blynk lewat Wi-Fi
#include "ThingSpeak.h"     // Library untuk mengirim data ke platform cloud ThingSpeak
#include <FirebaseESP32.h>  // Library untuk menghubungkan ESP32 dengan database Firebase

#define SCREEN_WIDTH 128     // Menentukan lebar layar OLED yaitu 128 piksel
#define SCREEN_HEIGHT 64     // Menentukan tinggi layar OLED yaitu 64 piksel
#define OLED_RESET -1        // Menggunakan pin reset internal ESP32 (-1 berarti tidak ada pin fisik khusus reset)
#define SCREEN_ADDRESS 0x3C  // Alamat I2C standar untuk layar OLED berukuran 0.96 inci

// Menyimpan alamat URL Realtime Database Firebase tempat data akan disimpan
#define FIREBASE_HOST "https://solar-tracker-ad472-default-rtdb.asia-southeast1.firebasedatabase.app/" 
#define FIREBASE_AUTH "AIzaSyA4u7_LixyZ-YHoGWZp0u_u2CDe2KGNruk" // Token/Kunci rahasia untuk mengakses Firebase

const char* ssid = "TECNO CAMON 40 Pro 5G";       // Nama hotspot/Wi-Fi yang akan dikoneksikan
const char* pass = "12345678";                   // Password Wi-Fi tersebut
unsigned long myChannelNumber = 3411486;          // ID Channel milikmu di ThingSpeak
const char * myWriteAPIKey = "5Y19LZEH7ZYW9GSB"; // Kunci API untuk izin menulis/mengirim data ke ThingSpeak

WiFiClient client;                    // Membuat objek client Wi-Fi untuk ThingSpeak
Adafruit_INA219 ina219;               // Membuat objek bernama "ina219" dari library INA219
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Membuat objek layar OLED

// Membuat objek-objek konfigurasi yang dibutuhkan oleh library Firebase
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

const int pinLdrKiri = 34;   // Sensor LDR bagian kiri dihubungkan ke pin analog GPIO34
const int pinLdrKanan = 35;  // Sensor LDR bagian kanan dihubungkan ke pin analog GPIO35
const int pinServo = 33;     // Kabel data motor servo dihubungkan ke pin GPIO33
const int voltagePin = 32;   // Pin analog GPIO32 digunakan jika ingin membaca tegangan lewat metode pembagi tegangan alternatif
float vCalibration = 0.022;  // Angka pengali untuk kalibrasi pembacaan pin analog tegangan

Servo myservo;        // Membuat objek motor servo bernama "myservo"
int posisiServo = 90; // Mengatur posisi awal servo di tengah-tengah (90 derajat)
int toleransi = 100;  // Batas minimum selisih nilai LDR agar servo bergerak (mencegah servo bergetar terus-menerus)
int batasMalam = 2000; // Indikator kegelapan. Jika pembacaan LDR di atas 2000, alat menganggap hari sudah malam
int posisiAwal = 0;   // Target sudut servo (0 derajat/Timur) saat hari berganti malam (pulang ke posisi awal)

unsigned long lastTimeThingSpeak = 0;         // Variabel pencatat waktu terakhir kali data dikirim ke ThingSpeak
const unsigned long postingInterval = 15000; // Aturan jeda kirim data ke ThingSpeak (minimal 15 detik sekali)

BlynkTimer timer; // Membuat objek timer internal milik Blynk untuk eksekusi fungsi berkala tanpa merusak delay loop

void kirimDataCloud() {
  // 1. BACA DATA SENSOR VOLTAGE
  int sensorValue = analogRead(voltagePin); 
  float voltage = (sensorValue * 3.3) / 4095.0; 
  float actualVoltage = voltage / vCalibration;  

  // 2. BACA DATA SENSOR INA219
  float shuntvoltage = ina219.getShuntVoltage_mV(); 
  float busvoltage = ina219.getBusVoltage_V();     
  float current_mA = ina219.getCurrent_mA();       
  float power_mW = ina219.getPower_mW();          
  float loadvoltage = busvoltage + (shuntvoltage / 1000); 

  // 3. KIRIM KE BLYNK
  Blynk.virtualWrite(V1, actualVoltage); 
  Blynk.virtualWrite(V2, loadvoltage);
  Blynk.virtualWrite(V3, current_mA);
  Blynk.virtualWrite(V4, power_mW);
  Blynk.virtualWrite(V5, posisiServo);
  Serial.println(">> Data terkirim ke Blynk!");

  // 4. KIRIM KE FIREBASE (Menggunakan FirebaseJson agar rapi membentuk tabel)
  if (Firebase.ready()) { 
    FirebaseJson json;
    
    // Satukan semua data ke dalam satu paket objek
    json.set("Voltsen", actualVoltage);
    json.set("Tegangan", loadvoltage);
    json.set("Arus", current_mA);
    json.set("Daya", power_mW);
    json.set("Sudut_Servo", posisiServo);

    // Perbaikan: Langsung gunakan Firebase.push tanpa .RTDB dan gunakan push() untuk objek JSON
    if (Firebase.push(firebaseData, "/Data_Monitoring", json)) {
      Serial.println(">> Data terkirim ke Firebase (Format Tabel)!");
    } else {
      Serial.print(">> Gagal kirim ke Firebase: ");
      Serial.println(firebaseData.errorReason());
    }
  } else {
    Serial.println(">> Firebase Belum Siap/Error!");
  }
}

void setup() {
  Serial.begin(115200); // Membuka jalur komunikasi serial ke komputer dengan kecepatan 115200 baud
  Wire.begin(21, 22);   // Mengaktifkan pin I2C khusus ESP32 (SDA di GPIO21, SCL di GPIO22)
  
  // Memulai layar OLED, jika gagal maka program akan berhenti total di sini
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  
  // Memulai sensor INA219, jika kabel terputus atau rusak maka program akan berhenti total di sini
  if (!ina219.begin()) {
    Serial.println("Gagal menemukan chip INA219. Periksa kabel!");
    while (1) { delay(10); }
  }
  
  // Mengaktifkan mode Wi-Fi sebagai Station (menangkap sinyal) dan mulai menghubungkan
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  
  // Menampilkan tulisan "Connecting WiFi..." di layar OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("Connecting WiFi...");
  display.display();
  Serial.print("Menghubungkan ke Wi-Fi");
  
  // Loop pengunci: Program tidak akan lanjut sebelum status Wi-Fi benar-benar terhubung
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
  
  ThingSpeak.begin(client); // Mengaktifkan koneksi library ThingSpeak

  // Menampilkan pesan sukses terhubung di layar OLED selama 1,5 detik
  display.clearDisplay();
  display.setCursor(15, 15);
  display.println("TRACKER READY");
  display.setCursor(15, 35);
  display.println("WiFi Connected!");
  display.display();
  delay(1500);

  // Mengisi data konfigurasi rahasia Firebase yang sudah didefinisikan di awal
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth); // Memulai sambungan ke Firebase
  Firebase.reconnectWiFi(true);   // Menginstruksikan Firebase untuk otomatis terkoneksi lagi jika Wi-Fi sempat terputus

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass); // Memulai sambungan ke Server Blynk secara Cloud
  
  // Menampilkan status sistem semua OK di layar OLED selama 1,5 detik
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 20);
  display.println("ALL SYSTEMS OK");
  display.setCursor(15, 40);
  display.println("Firebase + Blynk");
  display.display();
  delay(1500);

  // Mengalokasikan resource hardware timer internal ESP32 untuk kontrol modul PWM Servo agar tidak bentrok
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  myservo.setPeriodHertz(50); // Frekuensi standar motor servo pada umumnya adalah 50Hz

  // Mengatur agar fungsi kirimDataCloud() dipanggil otomatis setiap 3000ms (3 detik) sekali
  timer.setInterval(3000L, kirimDataCloud);
  
  myservo.attach(pinServo, 500, 2400); // Menghubungkan pin fisik servo ke objek software beserta batasan pulsa mikrodetiknya
  myservo.write(posisiServo);          // Menggerakkan servo ke posisi awal (90)
  delay(500);                          // Jeda penstabilan motor servo
}

void loop() {
  Blynk.run(); // Perintah wajib agar koneksi data Blynk tidak terputus dan selalu merespon perintah cloud
  timer.run(); // Perintah wajib untuk menjalankan jadwal BlynkTimer (fungsi kirimDataCloud tiap 3 detik)

  // 1. BACA DATA SENSOR LDR
  int nilaiKiri = analogRead(pinLdrKiri);   // Membaca tingkat cahaya LDR kiri (nilai 0 - 4095)
  int nilaiKanan = analogRead(pinLdrKanan); // Membaca tingkat cahaya LDR kanan (nilai 0 - 4095)
  int selisih = nilaiKiri - nilaiKanan;     // Menghitung selisih cahaya antara kiri dan kanan

  // 2. LOGIKA KONTROL SERVO (SIANG VS MALAM)
  if (nilaiKiri > batasMalam && nilaiKanan > batasMalam) {
    // KONDISI MALAM HARI (Kedua LDR gelap/di atas nilai batasMalam)
    if (posisiServo != posisiAwal) {
      if (posisiServo < posisiAwal) posisiServo += 1;      // Jika posisi kurang dari target pulang, naikkan sudutnya
      else if (posisiServo > posisiAwal) posisiServo -= 1; // Jika posisi lebih dari target pulang, turunkan sudutnya
      myservo.write(posisiServo); // Jalankan pergerakan servo menuju titik awal secara bertahap
      delay(15); // Efek jeda halus (soft movement) saat pulang agar tidak merusak mekanik alat
    }
  }
  else {
    // KONDISI SIANG HARI (Ada cahaya matahari terdeteksi)
    if (abs(selisih) > toleransi) { // Hanya bergerak jika selisih perbedaan cahaya lebih besar dari nilai toleransi
      if (nilaiKiri < nilaiKanan) { // Jika LDR kiri lebih terang (nilai ADC kecil artinya cahaya lebih terang)
        if (posisiServo < 180) posisiServo += 1; // Geser servo ke arah kanan (maksimal 180 derajat)
      }
      else if (nilaiKanan < nilaiKiri) { // Jika LDR kanan lebih terang
        if (posisiServo > 0) posisiServo -= 1; // Geser servo ke arah kiri (minimal 0 derajat)
      }
      myservo.write(posisiServo); // Terapkan perubahan posisi sudut ke motor servo fisik
    }
  }

  // 3. BACA DATA SENSOR DAYA & TEGANGAN
  int sensorValue = analogRead(voltagePin); // Membaca nilai mentah pin analog pembagi tegangan tambahan
  float voltage = (sensorValue * 3.3) / 4095.0; // Mengonversi nilai mentah menjadi nilai tegangan nyata (Volt) mikroprosesor
  float actualVoltage = voltage / vCalibration;  // Mendapatkan nilai tegangan asli sebelum diturunkan pembagi tegangan
  
  // Mengambil data terkini dari INA219 untuk kebutuhan tampilan OLED lokal
  float loadvoltage = ina219.getBusVoltage_V() + (ina219.getShuntVoltage_mV() / 1000);
  float current_mA = ina219.getCurrent_mA();
  float power_mW = ina219.getPower_mW();

  // 4. UPDATE TAMPILAN MONITOR PADA LAYAR OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 0);
  display.println("--- POWER MONITOR ---"); // Judul menu
  display.setCursor(0, 16);
  display.print("V-Analog : "); display.print(actualVoltage, 2); display.println(" V"); // Menampilkan V-Analog 2 angka desimal
  display.setCursor(0, 28);
  display.print("V-INA219 : "); display.print(loadvoltage, 2); display.println(" V");  // Menampilkan Tegangan INA219
  display.setCursor(0, 40);
  display.print("Arus     : "); display.print(current_mA, 1); display.println(" mA"); // Menampilkan Arus Listrik (mA)
  display.setCursor(0, 52);
  display.print("Daya     : "); display.print(power_mW, 1); display.println(" mW");  // Menampilkan Daya Listrik (mW)
  display.display(); // Terapkan seluruh tulisan di atas ke layar fisik OLED

  // 5. PENGIRIMAN DATA IOT KE THINGSPEAK (Setiap 15 Detik Secara Berkala)
  if (millis() - lastTimeThingSpeak > postingInterval) { // Logika non-blocking millis untuk mengecek apakah sudah lewat 15 detik
    // Memasukkan masing-masing data variabel ke dalam slot Field 1 sampai Field 5 di server ThingSpeak
    ThingSpeak.setField(1, actualVoltage);
    ThingSpeak.setField(2, loadvoltage);  
    ThingSpeak.setField(3, current_mA);    
    ThingSpeak.setField(4, power_mW);    
    ThingSpeak.setField(5, posisiServo);  

    // Mengirimkan paket data terpadu ke ThingSpeak menggunakan ID Proyek dan API Key milikmu
    int responseCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (responseCode == 200) {
      Serial.println(">> ThingSpeak update sukses!"); // Kode HTTP 200 berarti sukses terkirim
    } else {
      Serial.println(">> Gagal update ThingSpeak. HTTP Error: " + String(responseCode)); // Menampilkan kode error jika gagal
    }   
    lastTimeThingSpeak = millis(); // Memperbarui catatan waktu terakhir upload data ke ThingSpeak
  }

  // 6. DEBUGGING KE SERIAL MONITOR (Untuk dipantau via Laptop/PC)
  Serial.print("LDR_K: "); Serial.print(nilaiKiri);
  Serial.print(" | LDR_N: "); Serial.print(nilaiKanan);
  Serial.print(" | Servo: "); Serial.print(posisiServo);
  Serial.print(" | V_Anlg: "); Serial.print(actualVoltage, 2);
  Serial.print("V | Arus: "); Serial.print(current_mA, 1);
  Serial.println("mA");
  
  delay(30); // Jeda kecil 30 milidetik agar pembacaan loop tidak terlalu cepat/stres dan gerakan servo stabil
}
