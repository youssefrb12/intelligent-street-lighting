#include <RBDdimmer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ══════════════════════════════════════
//  CONFIGURATION
// ══════════════════════════════════════
const char* WIFI_SSID     = "Galaxy A26 5G 14CF";
const char* WIFI_PASSWORD = "youssef2003";
const char* MQTT_SERVER   = "10.26.67.67";
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT   = "ESP8266_Lampe_Triple";

// ══════════════════════════════════════
//  PINS
// ══════════════════════════════════════
#define ZC_PIN      5   
#define CH1_PIN    12   // D6 — Dimmer lampe 1x
#define CH2_PIN    13   // D7 — Dimmer lampe 2
#define CH3_PIN    15   // D8 — Dimmer lampe 3
#define TCRT1_PIN    4   
#define TCRT2_PIN   14   
#define TCRT3_PIN    0  
#define LDR_PIN    A0

// ══════════════════════════════════════
//  PARAMÈTRES
// ══════════════════════════════════════
#define LDR_SEUIL        100   // En dessous = JOUR, au dessus = NUIT
#define PWM_VEILLE       30   // % en mode veille nuit
#define PWM_ACTIF       100   // % lors d'une détection
#define DUREE_MAINTIEN  500   // ms à 100% après la dernière détection

// ══════════════════════════════════════
//  OBJETS
// ══════════════════════════════════════
dimmerLamp lampe1(CH1_PIN, ZC_PIN);
dimmerLamp lampe2(CH2_PIN, ZC_PIN);
dimmerLamp lampe3(CH3_PIN, ZC_PIN);
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ══════════════════════════════════════
//  VARIABLES GLOBALES
// ══════════════════════════════════════
bool modeManuel = false;
bool estNuit    = false;

// Niveaux courants et cibles
int niveauActuel1 = 0, niveauCible1 = 0;
int niveauActuel2 = 0, niveauCible2 = 0;
int niveauActuel3 = 0, niveauCible3 = 0;

// Mode manuel ON/OFF par lampe
bool manuelOn1 = false;
bool manuelOn2 = false;
bool manuelOn3 = false;

// Timers maintien (temps jusqu'auquel rester à 100%)
unsigned long timer1 = 0;
unsigned long timer2 = 0;
unsigned long timer3 = 0;

// MQTT cache (publication uniquement si changement)
String dernierLampe1 = "", dernierLampe2 = "", dernierLampe3 = "";
String dernierSensor1 = "", dernierSensor2 = "", dernierSensor3 = "";
String dernierLDR = "", dernierMode = "";
unsigned long dernierPublish = 0;
unsigned long dernierUpdate  = 0;

// ══════════════════════════════════════
//  WIFI
// ══════════════════════════════════════
void connecterWifi() {
  Serial.print("[WiFi] Connexion...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK — " + WiFi.localIP().toString());
}

// ══════════════════════════════════════
//  MQTT CALLBACK
// ══════════════════════════════════════
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  String t = String(topic);

  Serial.println("[MQTT] " + t + " = " + msg);

  if (t == "rue/cmd/mode") {
    modeManuel = (msg == "MANUEL");
  }
  else if (t == "rue/cmd/lampe/1") {
    manuelOn1 = (msg == "ON");
    if (modeManuel) {
      String val = manuelOn1 ? "100" : "0";
      mqtt.publish("rue/lampe/1", val.c_str());
      dernierLampe1 = val;
    }
  }
  else if (t == "rue/cmd/lampe/2") {
    manuelOn2 = (msg == "ON");
    if (modeManuel) {
      String val = manuelOn2 ? "100" : "0";
      mqtt.publish("rue/lampe/2", val.c_str());
      dernierLampe2 = val;
    }
  }
  else if (t == "rue/cmd/lampe/3") {
    manuelOn3 = (msg == "ON");
    if (modeManuel) {
      String val = manuelOn3 ? "100" : "0";
      mqtt.publish("rue/lampe/3", val.c_str());
      dernierLampe3 = val;
    }
  }
}

// ══════════════════════════════════════
//  MQTT CONNEXION
// ══════════════════════════════════════
void connecterMQTT() {
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connexion...");
    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println(" OK");
      mqtt.subscribe("rue/cmd/mode");
      mqtt.subscribe("rue/cmd/lampe/1");
      mqtt.subscribe("rue/cmd/lampe/2");
      mqtt.subscribe("rue/cmd/lampe/3");
    } else {
      Serial.print(" echec rc=");
      Serial.println(mqtt.state());
      delay(3000);
    }
  }
}

// ══════════════════════════════════════
//  PUBLICATION MQTT
// ══════════════════════════════════════
void publierEtats() {
  // Niveaux lampes
  String l1 = String(niveauActuel1);
  if (l1 != dernierLampe1) { mqtt.publish("rue/lampe/1", l1.c_str()); dernierLampe1 = l1; }

  String l2 = String(niveauActuel2);
  if (l2 != dernierLampe2) { mqtt.publish("rue/lampe/2", l2.c_str()); dernierLampe2 = l2; }

  String l3 = String(niveauActuel3);
  if (l3 != dernierLampe3) { mqtt.publish("rue/lampe/3", l3.c_str()); dernierLampe3 = l3; }

  // État capteurs (1 = actif, 0 = inactif)
  String s1 = (niveauCible1 == PWM_ACTIF && estNuit) ? "1" : "0";
  String s2 = (niveauCible2 == PWM_ACTIF && estNuit) ? "1" : "0";
  String s3 = (niveauCible3 == PWM_ACTIF && estNuit) ? "1" : "0";
  if (s1 != dernierSensor1) { mqtt.publish("rue/pir/1", s1.c_str()); dernierSensor1 = s1; }
  if (s2 != dernierSensor2) { mqtt.publish("rue/pir/2", s2.c_str()); dernierSensor2 = s2; }
  if (s3 != dernierSensor3) { mqtt.publish("rue/pir/3", s3.c_str()); dernierSensor3 = s3; }

  // LDR
  String ldrStr = estNuit ? "NUIT" : "JOUR";
  if (ldrStr != dernierLDR) { mqtt.publish("rue/ldr", ldrStr.c_str()); dernierLDR = ldrStr; }

  // Mode
  String modeStr = modeManuel ? "MANUEL" : "AUTO";
  if (modeStr != dernierMode) { mqtt.publish("rue/mode", modeStr.c_str()); dernierMode = modeStr; }
}


void calculerNiveaux() {

  
  if (modeManuel) {
    niveauCible1 = manuelOn1 ? PWM_ACTIF : 0;
    niveauCible2 = manuelOn2 ? PWM_ACTIF : 0;
    niveauCible3 = manuelOn3 ? PWM_ACTIF : 0;
    return;
  }

  
  estNuit = (analogRead(LDR_PIN) > LDR_SEUIL);

  if (!estNuit) {
    
    niveauCible1 = niveauCible2 = niveauCible3 = 0;
    return;
  }


  if (digitalRead(TCRT1_PIN) == LOW) {
    timer1 = millis() + DUREE_MAINTIEN;
    niveauCible1 = PWM_ACTIF;
  } else if (millis() > timer1) {
    niveauCible1 = PWM_VEILLE;
  }
  if (digitalRead(TCRT2_PIN) == LOW) {
    timer2 = millis() + DUREE_MAINTIEN;
    niveauCible2 = PWM_ACTIF;
  } else if (millis() > timer2) {
    niveauCible2 = PWM_VEILLE;
  }
  if (digitalRead(TCRT3_PIN) == LOW) {
    timer3 = millis() + DUREE_MAINTIEN;
    niveauCible3 = PWM_ACTIF;
  } else if (millis() > timer3) {
    niveauCible3 = PWM_VEILLE;
  }

  
}

// ══════════════════════════════════════
//  TRANSITION DOUCE
// ══════════════════════════════════════
void appliquerTransitions() {
  auto transition = [](int& actuel, int cible, dimmerLamp& lamp) {
    if (actuel != cible) {
      actuel = (actuel < cible) ?
        min(actuel + 5, cible) :
        max(actuel - 5, cible);
      lamp.setPower(actuel);
    }
  };

  transition(niveauActuel1, niveauCible1, lampe1);
  transition(niveauActuel2, niveauCible2, lampe2);
  transition(niveauActuel3, niveauCible3, lampe3);
}

// ══════════════════════════════════════
//  SETUP
// ══════════════════════════════════════
void setup() {
  Serial.begin(9600);
  delay(2000);
  Serial.println("\n=== Eclairage Independant 3 Lampes ===");

  // Capteurs
  pinMode(TCRT1_PIN, INPUT); // TCRT5000 : LOW actif → pullup interne
  pinMode(TCRT2_PIN, INPUT);
  pinMode(TCRT3_PIN, INPUT);

  // Dimmers
  lampe1.begin(NORMAL_MODE, ON); lampe1.setPower(0);
  lampe2.begin(NORMAL_MODE, ON); lampe2.setPower(0);
  lampe3.begin(NORMAL_MODE, ON); lampe3.setPower(0);
  Serial.println("[DIMMER] OK");

  connecterWifi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

// ══════════════════════════════════════
//  LOOP
// ══════════════════════════════════════
void loop() {
  if (!mqtt.connected()) connecterMQTT();
  mqtt.loop();

  if (millis() - dernierUpdate > 50) {
    dernierUpdate = millis();

    calculerNiveaux();

    Serial.print("[LDR]="); Serial.print(estNuit ? "NUIT" : "JOUR");
    Serial.print(" [TCRT]="); Serial.print(digitalRead(TCRT1_PIN) == LOW ? "DETECT" : "---");
    Serial.print(" [P2]=");   Serial.print(digitalRead(TCRT2_PIN) ? "DETECT" : "---");
    Serial.print(" [P3]=");   Serial.print(digitalRead(TCRT3_PIN) ? "DETECT" : "---");
    Serial.print(" L1="); Serial.print(niveauActuel1);
    Serial.print("% L2="); Serial.print(niveauActuel2);
    Serial.print("% L3="); Serial.print(niveauActuel3);
    Serial.println("%");
    
  }

  appliquerTransitions();

  if (millis() - dernierPublish > 500) {
    dernierPublish = millis();
    publierEtats();
  }

  delay(10);
}
