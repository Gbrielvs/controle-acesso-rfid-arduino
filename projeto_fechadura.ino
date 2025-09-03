#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>

// ===== LCD I2C =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== RC522 (UNO) =====
#define RST_PIN 9
#define SS_PIN  10
MFRC522 rfid(SS_PIN, RST_PIN);

// ===== RELÉ (porta) no D8 =====
#define DOOR_RELAY_PIN 8
const bool RELAY_ACTIVE_LOW = true;       // mude p/ false se seu módulo for ativo-HIGH
const unsigned long DOOR_PULSE_MS = 3000; // <<< 3 segundos

// ===== BUZZER no D5 =====
#define BUZZER_PIN 5

// ===== Cartões autorizados =====
struct User { byte len; byte b[10]; const char* name; };
const User AUTH[] = { {4, {0x63,0x2B,0x31,0xE2}, "Gabriel"} };
const size_t AUTH_COUNT = sizeof(AUTH)/sizeof(AUTH[0]);

// ---------- Utils ----------
inline void relayOn()  { digitalWrite(DOOR_RELAY_PIN, RELAY_ACTIVE_LOW ? LOW  : HIGH); }
inline void relayOff() { digitalWrite(DOOR_RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW ); }

String uidToString(const MFRC522::Uid &u){
  String s=""; for(byte i=0;i<u.size;i++){ if(u.uidByte[i]<0x10)s+='0';
    s+=String(u.uidByte[i],HEX); if(i<u.size-1)s+=':'; } s.toUpperCase(); return s;
}
const char* findUserName(const MFRC522::Uid &u){
  for(size_t i=0;i<AUTH_COUNT;i++){
    if (AUTH[i].len != u.size) continue;
    bool ok = true;
    for(byte j=0;j<u.size;j++){ if(AUTH[i].b[j] != u.uidByte[j]){ ok=false; break; } }
    if (ok) return AUTH[i].name;
  }
  return nullptr;
}
void showCentered(const String &a,const String &b=""){
  lcd.clear(); const int w=16;
  lcd.setCursor(max(0,(w-(int)a.length())/2),0); lcd.print(a);
  lcd.setCursor(max(0,(w-(int)b.length())/2),1); lcd.print(b);
}

// ---------- Beeps não bloqueantes ----------
inline void beepGranted(){ tone(BUZZER_PIN, 1600, 150); } // curto e agudo
inline void beepDenied() { tone(BUZZER_PIN,  450, 200); } // curto e grave

// ---------- Estados (não bloqueantes) ----------
bool doorActive = false;
unsigned long doorUntil = 0;

unsigned long msgUntil = 0;     // quando voltar ao prompt no LCD
String lastUID = "";
unsigned long lastMs = 0;
const unsigned long COOLDOWN_MS = 300;   // leitura rápida

void setup(){
  Serial.begin(9600);

  pinMode(DOOR_RELAY_PIN, OUTPUT); relayOff();
  pinMode(BUZZER_PIN, OUTPUT);     digitalWrite(BUZZER_PIN, LOW);

  lcd.init(); lcd.backlight();
  showCentered("Iniciando...", "LCD OK");

  // clique de teste do relé
  relayOn(); delay(200); relayOff(); delay(120);

  SPI.begin();
  #if defined(__AVR__)
    SPI.setClockDivider(SPI_CLOCK_DIV8); // ~2MHz ajuda com ruído
  #endif
  rfid.PCD_Init(); delay(40);

  showCentered("Aproxime o", "cartao/tag");
}

void loop(){
  const unsigned long now = millis();

  // 1) Gerencia tempo da porta aberta
  if (doorActive && now >= doorUntil) {
    relayOff();
    doorActive = false;
  }

  // 2) Volta mensagem padrão
  if (msgUntil && now >= msgUntil) {
    showCentered("Aproxime o", "cartao/tag");
    msgUntil = 0;
  }

  // 3) Leitura rápida do RC522
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  String uid = uidToString(rfid.uid);
  if (uid == lastUID && (now - lastMs) < COOLDOWN_MS) {
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return;
  }
  lastUID = uid; lastMs = now;

  if(const char* name = findUserName(rfid.uid)){
    // Acesso liberado: beep + mensagem + porta por 3s (sem delay)
    Serial.print(F("LIBERADO: ")); Serial.println(name);
    showCentered("Bem-vindo,", String(name));
    msgUntil = now + 1200;
    beepGranted();
    relayOn();
    doorActive = true;
    doorUntil = now + DOOR_PULSE_MS;  // 3s
  } else {
    Serial.println(F("NEGADO"));
    showCentered("ACESSO", "NEGADO");
    msgUntil = now + 1000;
    beepDenied();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
