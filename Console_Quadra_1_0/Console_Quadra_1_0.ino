// CONSOLE QUADRA nasce dal progetto ORAQUADRA 2 di (Davide Gatti Survival Hacking) un idea e creazione di Marco Prunca
//
// 17/01/2026 V1.0 Versione iniziale.
// 19/01/2026 V1.1 Adattamento per ESP32-WROOM-32D (pin modificati per compatibilità)
// dispositivo da selezionare ESP32 Dev Module + Bluepad32 inserendo il link nella sezione File/Preferenze  https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
// parametri da modificare nella sezione Strumenti/Partition scheme HUGE APP
// Collegare il dispositivo CONSOLE QUADRA con il WIFI e poi andare all'indirizzo 192.168.4.1 con un browser per impostare la connessione WIFI di casa vostra.
// Una volta connesso, andare all'inidirizzo IP mostrato mediante scorrimento in matrice per utilizzare il dispositivo.

//*********************** LIBRERIE ************
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FastLED.h>
#include <EEPROM.h>
#include <ezTime.h>
#include <math.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_HTU21DF.h>
#include <LittleFS.h>
//*********************************************

// Contatore transizioni radio per debug e auto-restart
static uint8_t radioTransitionCount = 0;
static const uint8_t MAX_RADIO_TRANSITIONS = 10; // Dopo 10 transizioni, restart preventivo

// Commenta questa riga per disabilitare Bluepad32 (gamepad Bluetooth)
#define ENABLE_BLUEPAD32
#ifdef ENABLE_BLUEPAD32
#include <Bluepad32.h>
#endif
//*********************************************

inline CRGB ColorHSV_NeoPixel(uint16_t hue, uint8_t sat = 255, uint8_t val = 255) {
  uint8_t fastled_hue = hue >> 8; // Converte da 0-65535 a 0-255
  return CHSV(fastled_hue, sat, val);
}

inline CRGB gamma32(CRGB color) {
  
  color.r = dim8_video(color.r);
  color.g = dim8_video(color.g);
  color.b = dim8_video(color.b);
  return color;
}

// ============================================
// CONFIGURAZIONE HARDWARE - PIN (ESP32-WROOM-32D)
// ============================================
// PIN ORIGINALI (ESP32-S2/S3):
// LED_PIN=5, BUTTON_MODE=6, BUTTON_SEC=7, BUZZER_PIN=4, I2C_SDA=8, I2C_SCL=9
//
// PIN MODIFICATI PER ESP32-WROOM-32D:
// GPIO 6-11 non disponibili (flash interna), usare pin alternativi
// ============================================
#define BUTTON_LOGIC_INVERTED 1
#define LED_PIN      16    // Matrice LED WS2812B (Data In)
#define BUTTON_MODE  32    // Pulsante Mode (con pull-up interno)
#define BUTTON_SEC   33    // Pulsante Secondario (con pull-up interno)
#define BUZZER_PIN   17    // Buzzer passivo
#define I2C_SDA      21    // HTU21D SDA (default I2C ESP32)
#define I2C_SCL      22    // HTU21D SCL (default I2C ESP32)
#define NUM_LEDS    256
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS_DAY   40 //64
#define BRIGHTNESS_NIGHT 20 //32
#define MATRIX_WIDTH  16
#define MATRIX_HEIGHT 16

// ============================================
// VARIABILI GLOBALI
// ============================================
CRGB leds[NUM_LEDS];
WebServer server(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);
Timezone myTZ;
Adafruit_HTU21DF htu = Adafruit_HTU21DF();

// Stato WiFi
bool wifiConnected = false;
bool apMode = false;

// Flag per quick restart da Bluetooth (sopravvive a ESP.restart())
RTC_NOINIT_ATTR uint32_t quickRestartFlag;
#define QUICK_RESTART_MAGIC 0xB72F1F1

// Sensore HTU21 - Temperatura e Umidità
float currentTemperature = 0.0;
float currentHumidity = 0.0;
unsigned long lastSensorRead = 0;
bool sensorAvailable = false;

// Configurazione WiFi e impostazioni salvate in EEPROM
struct WiFiConfig {
  char ssid[32];
  char password[64];
  uint8_t brightness;
  float weatherLatitude;    // Latitudine per Open-Meteo API
  float weatherLongitude;   // Longitudine per Open-Meteo API
  char weatherCity[32];

  // Impostazioni orologio
  uint8_t clockColorMode;
  uint8_t secondsLedColorMode;
  uint8_t clockDisplayType;
  bool clockWeatherAutoSwitch;
  uint16_t clockDisplayInterval;
  uint16_t weatherDisplayInterval;
  char clockTimezone[32];

  // Spegnimento programmato
  bool nightShutdownEnabled;
  uint8_t nightShutdownStartHour;
  uint8_t nightShutdownStartMinute;
  uint8_t nightShutdownEndHour;
  uint8_t nightShutdownEndMinute;
  bool dayShutdownEnabled;
  uint8_t dayShutdownStartHour;
  uint8_t dayShutdownStartMinute;
  uint8_t dayShutdownEndHour;
  uint8_t dayShutdownEndMinute;

  // Controllo LED matrice
  bool matrixLedEnabled;

  // Impostazioni testo scorrevole
  uint8_t scrollTextColor;
  uint8_t scrollTextSize;
  uint16_t scrollSpeed;

  // Impostazioni visualizzazione data
  bool dateDisplayEnabled;
  uint16_t dateDisplayInterval;
  uint8_t dateColorMode;
  uint8_t dateDisplaySize;
  uint8_t displaySequence; // 0=Orologio→Meteo→Data, 1=Orologio→Data→Meteo, 2=Data→Orologio→Meteo

  // Impostazioni sveglia
  bool alarmEnabled;        // Sveglia abilitata/disabilitata
  uint8_t alarmHour;        // Ora sveglia (0-23)
  uint8_t alarmMinute;      // Minuto sveglia (0-59)
  uint8_t alarmDays;        // Bitmap giorni settimana: bit0=Lun...bit6=Dom
  uint8_t alarmRingtone;    // Indice suoneria (0-11)
  uint8_t alarmDuration;    // Durata suoneria in secondi (30-180)

  // Impostazioni sensore locale (temperatura/umidità ambiente)
  bool localSensorDisplayEnabled;     // Abilita visualizzazione sensore locale
  uint16_t localSensorDisplayInterval; // Tempo visualizzazione in secondi (default 15)

  // Impostazioni Cronotermostato
  bool thermostatEnabled;             // Termostato abilitato/disabilitato
  char shellyIP[16];                  // IP dello Shelly (es: "192.168.1.100")
  float thermostatHysteresis;         // Isteresi in gradi (default 0.5°C)
  float thermostatManualTemp;         // Temperatura override manuale
  bool thermostatManualOverride;      // Override manuale attivo
  uint8_t thermostatDefaultTemp;      // Temperatura default quando non programmato (default 19°C)

  // Controller Bluetooth associati (memorizza se sono stati paired)
  bool btController1Paired;     // Controller 1 già associato
  bool btController2Paired;     // Controller 2 già associato

  // Flag di validità configurazione (per verificare se EEPROM è inizializzata)
  uint16_t magicNumber; // 0xCAFE = configurazione valida

  // Taratura manuale sensore HTU21D (aggiunto DOPO magicNumber per compatibilità EEPROM)
  float sensorTempOffset;  // Offset temperatura in °C (da -5.0 a +5.0, default 0.0)

  // Giorni settimana per spegnimento diurno (bitmap: bit0=Lun...bit6=Dom, 0x7F=tutti)
  uint8_t dayShutdownDays;
};

// Stati del sistema
enum SystemState {
  STATE_BOOT,
  STATE_WIFI_SETUP,
  STATE_GAME_MENU,
  STATE_GAME_TRIS,
  STATE_GAME_TEXT_SCROLL,
  STATE_GAME_CLOCK,
  STATE_GAME_WEATHER,
  STATE_GAME_SPACE_INVADERS,
  STATE_GAME_PONG,
  STATE_GAME_SNAKE,
  STATE_GAME_BREAKOUT,
  STATE_GAME_SCOREBOARD,
  STATE_GAME_TETRIS,
  STATE_GAME_PACMAN,
  STATE_GAME_SIMON,
  STATE_GAME_ZUMA,
  STATE_STOPWATCH,
  STATE_TIMER,
  STATE_CALENDAR_EVENT,
  STATE_THERMOSTAT
};

SystemState currentState = STATE_BOOT;
SystemState previousState = STATE_BOOT;

// Variabili per Tris
char trisBoard[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
bool trisPlayerTurn = true;
bool trisVsAI = true;
bool trisGameActive = false;
char trisWinner = ' ';
int trisScoreX = 0;  // Punteggio giocatore X
int trisScoreO = 0;  // Punteggio giocatore O / AI
int trisScoreDraw = 0;  // Pareggi

// Variabili per testo scorrevole
String scrollText = "CONSOLE QUADRA";
int scrollPosition = MATRIX_WIDTH;
int scrollSpeed = 50;
int scrollTextColor = 1; // 0=rosso, 1=verde, 2=blu, 3=giallo, 4=ciano, 5=magenta, 6=bianco, 7=arancione, 8=rainbow
int scrollTextSize = 0; // 0=piccolo (3x5), 1=medio (5x7), 2=grande (10x14 - 2x del medio)
unsigned long lastScrollUpdate = 0;
unsigned long rainbowOffset = 0; // Offset per animazione rainbow

// Variabili per Space Invaders
bool siVsAI = true;
int siPlayerX = 7;
int siPlayer2X = 7;
int siBulletX = -1, siBulletY = -1;
int siBullet2X = -1, siBullet2Y = -1;
bool siAliens[8][4]; // 8 colonne x 4 righe (riga 0: 5 alieni da 2 LED, righe 1-3: 8 alieni da 1 LED)
int siAlienX = 0;
int siAlienDir = 1;
int siAlienY = 1;
int siDirectionChanges = 0; // Conta i cambi di direzione per scendere ogni ciclo completo
int siScoreP1 = 0;
int siScoreP2 = 0;
int siLives = 5; // Numero di vite (5 totali)
int siLevel = 1; // Livello corrente (da 1 a 5)
bool siGameActive = false;
bool siGameOver = false;
unsigned long siLastUpdate = 0;
int siUpdateSpeed = 1800; // Velocità movimento orizzontale livello 1
int siBaseSpeed = 1800; // Velocità base livello 1 (più lento)
unsigned long siLastBulletUpdate = 0;
int siBulletSpeed = 30; // Velocità proiettili (visibile e fluido)
unsigned long siLastShot = 0; // Timestamp ultimo sparo del giocatore
int siShootCooldown = 500; // Cooldown sparo in millisecondi (500ms tra spari)

// Proiettili alieni (massimo 3 proiettili contemporaneamente)
#define MAX_ALIEN_BULLETS 3
int siAlienBulletX[MAX_ALIEN_BULLETS];
int siAlienBulletY[MAX_ALIEN_BULLETS];
unsigned long siLastAlienShot = 0; // Timestamp ultimo sparo alieno
int siAlienShootInterval = 3000; // Intervallo tra spari alieni (3 secondi - più equilibrato)

// Scudi Space Invaders (4 scudi, rettangoli 3x2)
byte siShields[4][2][3]; // 4 scudi, altezza 2, larghezza 3 (6 LED totali)
int siShieldX[4] = {0, 4, 9, 13}; // Posizioni X degli scudi (simmetrici sulla matrice 16x16)
int siShieldY = 12; // Posizione Y degli scudi

// Astronave UFO bonus (passa in alto)
int siUfoX = -1; // Posizione X UFO (-1 = inattivo)
int siUfoDir = 1; // Direzione UFO (1 = destra, -1 = sinistra)
unsigned long siLastUfoSpawn = 0; // Timestamp ultimo spawn UFO
int siUfoSpawnInterval = 15000; // Intervallo spawn UFO (15 secondi)
unsigned long siLastUfoUpdate = 0; // Timestamp ultimo update UFO
int siUfoSpeed = 200; // Velocità movimento UFO (ms)

// Schermata cambio livello (non bloccante)
bool siShowingLevel = false; // True se sta mostrando la schermata LEVEL X
unsigned long siLevelScreenStart = 0; // Timestamp inizio schermata livello
int siLevelScreenDuration = 2000; // Durata schermata livello (2 secondi)

// Suono marcia alieni (4 note che cambiano ad ogni movimento)
int siAlienMarchStep = 0; // Indice della nota corrente (0-3)
const int siAlienMarchNotes[4] = {196, 147, 131, 110}; // G3, D3, C3, A2 - note marcia arcade

// Variabili per Pong
bool pongVsAI = true;
int pongAIDifficulty = 1; // 0=Facile, 1=Medio, 2=Difficile, 3=Impossibile
int pongPaddle1Y = 6;
int pongPaddle2Y = 6;
int pongBallX = 8;
int pongBallY = 8;
int pongBallDirX = 1;
int pongBallDirY = 1;
int pongScoreP1 = 0;
int pongScoreP2 = 0;
int pongLives = 5;
bool pongGameActive = false;
bool pongGameOver = false;
bool pongBallOnPaddle = true;  // Pallina attaccata alla racchetta del giocatore 1
unsigned long pongLastUpdate = 0;
int pongSpeed = 150;

// Variabili per Snake
int snakeX[64];
int snakeY[64];
int snakeLength = 3;
int snakeDirX = 1;
int snakeDirY = 0;
int snakeFoodX = 10;
int snakeFoodY = 10;
int snakeScore = 0;
int snakeLives = 5;
bool snakeGameActive = false;
bool snakeGameOver = false;
unsigned long snakeLastUpdate = 0;
int snakeSpeed = 200;

// Variabili per Breakout
int breakoutPaddleX = 6;
int breakoutBallX = 8;
int breakoutBallY = 13;
int breakoutBallDirX = 1;
int breakoutBallDirY = -1;
// Fisica migliorata con posizioni e velocità frazionarie
float breakoutBallPosX = 8.0;
float breakoutBallPosY = 13.0;
float breakoutBallVelX = 1.0;
float breakoutBallVelY = -1.0;
bool breakoutBricks[16][6]; // 16 colonne x 6 righe
int breakoutScore = 0;
int breakoutLives = 3;
int breakoutLevel = 1;
bool breakoutGameActive = false;
unsigned long breakoutLastUpdate = 0;
int breakoutSpeed = 120;  // Rallentato da 100 a 120ms
bool breakoutLevelComplete = false;
unsigned long breakoutLevelCompleteTime = 0;
bool breakoutGameOver = false;
unsigned long breakoutGameOverTime = 0;
bool breakoutBallOnPaddle = false;  // Pallina ferma sulla racchetta
int breakoutPrevBallX = -1;
int breakoutPrevBallY = -1;
int breakoutPrevPaddleX = -1;
int breakoutBricksCount = 0;  // Numero mattoni rimasti
bool breakoutFullRedraw = true;  // Flag per forzare ridisegno completo

// Variabili per Segnapunti
int scorePlayer1 = 0;
int scorePlayer2 = 0;
int scorePlayer3 = 0;
int scorePlayer4 = 0;
String player1Name = "P1";
String player2Name = "P2";
String player3Name = "P3";
String player4Name = "P4";

// Variabili per Cronometro
unsigned long stopwatchStartTime = 0;
unsigned long stopwatchElapsedTime = 0;
bool stopwatchRunning = false;
uint8_t stopwatchColorMode = 0; // 0=ciano, 1=rosso, 2=verde, 3=blu, 4=giallo, 5=magenta, 6=bianco, 7=arancione, 8=rainbow

// Variabili per Timer
unsigned long timerStartTime = 0;
unsigned long timerDuration = 0;
bool timerRunning = false;
bool timerFinished = false;
uint8_t timerColorMode = 2; // 0=ciano, 1=rosso, 2=verde, 3=blu, 4=giallo, 5=magenta, 6=bianco, 7=arancione, 8=rainbow

// Variabili per Sveglia (Alarm)
bool alarmEnabled = false;
uint8_t alarmHour = 7;
uint8_t alarmMinute = 0;
uint8_t alarmDays = 0b01111100;  // Lun-Ven di default (bit 0-4 attivi)
uint8_t alarmRingtone = 0;  // 0=Mario di default
uint8_t alarmDuration = 30;  // 30 secondi di default
bool alarmRinging = false;  // Flag per indicare che la sveglia sta suonando
unsigned long alarmRingingStartTime = 0;  // Timestamp inizio suoneria
bool alarmTriggeredToday = false;  // Flag per evitare ripetizioni multiple nello stesso minuto
bool alarmDisplayStarted = false;  // Flag per sincronizzare suoneria con lampeggio rosso
bool alarmStopRequested = false;  // Flag per fermare sveglia solo dopo fine melodia corrente
uint8_t lastCheckedDay = 255;  // Per resettare alarmTriggeredToday a mezzanotte

// ============================================
// VARIABILI CRONOTERMOSTATO
// ============================================
#define THERMOSTAT_CHECK_INTERVAL 30000   // Controlla ogni 30 secondi
#define SHELLY_MIN_INTERVAL 5000          // Min 5 sec tra comandi Shelly
#define THERMOSTAT_FILE "/thermostat.json"

bool thermostatEnabled = false;
String shellyIP = "";
float thermostatHysteresis = 0.5;
float thermostatTargetTemp = 19.0;
float thermostatManualTemp = 20.0;
bool thermostatHeatingOn = false;
bool thermostatManualOverride = false;
uint8_t thermostatDefaultTemp = 19;
unsigned long lastThermostatCheck = 0;
unsigned long lastShellyCommand = 0;
bool shellyConnected = false;           // Stato connessione Shelly
unsigned long lastShellyStatusCheck = 0;

// Struttura per fascia oraria termostato
struct ThermostatSlot {
  char name[16];        // Nome fascia (es: "Mattina")
  float temperature;    // Temperatura target
};

// Struttura per programmazione giornaliera
struct ThermostatDaySchedule {
  uint8_t slot[24];     // Per ogni ora (0-23), indice dello slot (0-3) o 255=off
};

// Programmazione settimanale (0=Lun, 6=Dom)
ThermostatSlot thermostatSlots[4];              // 4 fasce orarie
ThermostatDaySchedule thermostatSchedule[7];   // 7 giorni
bool thermostatScheduleLoaded = false;

// ============================================

// Struttura per rappresentare una nota musicale
struct Note {
  int frequency;  // Frequenza in Hz (0 = pausa/silenzio)
  int duration;   // Durata in millisecondi
};

// Enum per le suonerie della sveglia
enum AlarmRingtone {
  RINGTONE_MARIO = 0,
  RINGTONE_ZELDA = 1,
  RINGTONE_TETRIS = 2,
  RINGTONE_NOKIA = 3,
  RINGTONE_POKEMON = 4,
  RINGTONE_STARWARS = 5,
  RINGTONE_HARRYPOTTER = 6,
  RINGTONE_CLASSIC = 7,
  RINGTONE_BEEP = 8,
  RINGTONE_STARTREK = 9,
  RINGTONE_BACKTOTHEFUTURE = 10,
  RINGTONE_INDIANAJONES = 11
};

// Variabili per riproduzione melodia non bloccante
const Note* currentMelody = NULL;
int currentMelodyLength = 0;
int currentNoteIndex = 0;
unsigned long noteStartTime = 0;
bool isPlayingMelody = false;
unsigned long lastMelodyStartTime = 0;
unsigned long lastMelodyEndTime = 0;
bool currentNoteActive = false;  // true quando la nota corrente ha frequenza > 0 (per sincronizzare lampeggio)

// ============================================
// CALENDARIO EVENTI
// ============================================
#define MAX_CALENDAR_EVENTS 30
#define CALENDAR_FILE "/calendar.json"

struct CalendarEvent {
  bool active;              // Evento attivo
  uint8_t day;              // Giorno (1-31)
  uint8_t month;            // Mese (1-12)
  uint16_t year;            // Anno (2024-2099)
  uint8_t hour;             // Ora principale (0-23)
  uint8_t minute;           // Minuto principale (0-59)
  uint8_t hour2;            // Secondo orario - ora (255 = disabilitato)
  uint8_t minute2;          // Secondo orario - minuto
  uint8_t hour3;            // Terzo orario - ora (255 = disabilitato)
  uint8_t minute3;          // Terzo orario - minuto
  char text[48];            // Testo da visualizzare (max 47 caratteri)
  bool repeatYearly;        // Ripeti ogni anno
  uint8_t textSize;         // Dimensione testo: 0=piccolo, 1=medio, 2=grande
  uint8_t textColor;        // Colore: 0=rosso,1=verde,2=blu,3=giallo,4=ciano,5=magenta,6=bianco,7=arancione,8=rainbow
  bool notificationShown;   // Notifica già mostrata per orario 1
  bool notificationShown2;  // Notifica già mostrata per orario 2
  bool notificationShown3;  // Notifica già mostrata per orario 3
};

CalendarEvent calendarEvents[MAX_CALENDAR_EVENTS];
int calendarEventCount = 0;
bool calendarEventActive = false;          // Un evento è attivo ora
int currentCalendarEventIndex = -1;        // Indice evento attivo
unsigned long calendarEventStartTime = 0;  // Quando è iniziato l'evento
unsigned long calendarEventDuration = 30000; // Durata visualizzazione (30 secondi)
SystemState stateBeforeCalendarEvent = STATE_GAME_CLOCK; // Stato precedente

// ============================================

// Variabili per Tetris (ottimizzate per memoria)
byte tetrisGrid[14][16]; // Griglia 14x16 OTTIMIZZATA (ampliata da 12 a 14 per evitare pezzi tagliati ai bordi)
byte tetrisPieceType = 0; // Tipo pezzo corrente (0-6: I,O,T,S,Z,L,J)
byte tetrisPieceRotation = 0; // Rotazione corrente (0-3)
int tetrisPieceX = 5; // Posizione X pezzo corrente (centrato in 14) - FIX: int invece di byte per gestire posizioni negative
int tetrisPieceY = 0; // Posizione Y pezzo corrente - FIX: int invece di byte per gestire posizioni negative
byte tetrisNextPieceType = 0; // Prossimo pezzo
int tetrisScore = 0;
byte tetrisLevel = 1;
byte tetrisLines = 0;
byte tetrisLives = 5;
bool tetrisGameActive = false;
bool tetrisGameOver = false;
unsigned long tetrisLastUpdate = 0;
int tetrisSpeed = 500; // Velocità caduta (diminuisce con il livello)

// Definizione tetramini (4x4 per ogni tipo, 4 rotazioni)
// Formato: ogni tetramino è una matrice 4x4 binaria appiattita
const uint16_t tetrominoes[7][4] = {
  // I - Pezzo lineare
  {0x0F00, 0x2222, 0x00F0, 0x4444},
  // O - Quadrato
  {0x6600, 0x6600, 0x6600, 0x6600},
  // T - Pezzo a T
  {0x0E40, 0x4C40, 0x4E00, 0x4640},
  // S - Pezzo S
  {0x06C0, 0x4620, 0x06C0, 0x4620},
  // Z - Pezzo Z
  {0x0C60, 0x2640, 0x0C60, 0x2640},
  // L - Pezzo L
  {0x0E20, 0x44C0, 0x8E00, 0x6440},
  // J - Pezzo J
  {0x0E80, 0xC440, 0x2E00, 0x4460}
};

// Variabili per Pac-Man
#define PACMAN_MAZE_WIDTH 16
#define PACMAN_MAZE_HEIGHT 16
#define PACMAN_NUM_GHOSTS 4
#define PACMAN_POWERUP_DURATION 10000 // 10 secondi di power-up

// Ghost modes (come nel Pac-Man arcade originale)
enum GhostMode {
  GHOST_MODE_SCATTER,  // Fantasmi vanno ai loro angoli casa
  GHOST_MODE_CHASE     // Fantasmi inseguono Pac-Man
};

GhostMode currentGhostMode = GHOST_MODE_SCATTER;
unsigned long lastModeSwitch = 0;
int modePhase = 0; // Fase corrente del pattern scatter/chase

// Maze: 0=vuoto, 1=muro, 2=dot, 3=power pellet
byte pacmanMaze[PACMAN_MAZE_HEIGHT][PACMAN_MAZE_WIDTH];
byte pacmanMazeOriginal[PACMAN_MAZE_HEIGHT][PACMAN_MAZE_WIDTH]; // Copia per reset

// Pac-Man
int pacmanX = 8;
int pacmanY = 12;
int pacmanDirX = 0;
int pacmanDirY = 0;
int pacmanNextDirX = 0;
int pacmanNextDirY = 0;
int pacmanMouthOpen = 0; // Animazione bocca (0-2)

// Ghosts
struct Ghost {
  int x;
  int y;
  int dirX;
  int dirY;
  int color; // 0=rosso, 1=rosa, 2=ciano, 3=arancione
  bool frightened; // Modalità spaventato (blu)
  bool eaten; // Fantasma mangiato, torna alla base
  unsigned long frightenedStartTime;
};

Ghost pacmanGhosts[PACMAN_NUM_GHOSTS];

// Game state
int pacmanScore = 0;
int pacmanLives = 5;
int pacmanLevel = 1;
int pacmanDotsRemaining = 0;
int pacmanTotalDots = 0;
bool pacmanGameActive = false;
bool pacmanGameOver = false;
bool pacmanLevelComplete = false;
bool pacmanPowerUpActive = false;
unsigned long pacmanPowerUpStartTime = 0;
unsigned long pacmanLastUpdate = 0;
unsigned long pacmanLastGhostUpdate = 0;
unsigned long pacmanLastMouthUpdate = 0;
int pacmanSpeed = 200; // ms per movimento Pac-Man (bilanciato)
int pacmanGhostSpeed = 400; // ms per movimento fantasmi normali (rallentati)
int pacmanGhostFrightenedSpeed = 500; // ms per movimento fantasmi spaventati (più lenti)
int pacmanGhostBaseX = 8; // Posizione base fantasmi
int pacmanGhostBaseY = 6;
bool pacmanGameOverSoundPlayed = false; // Flag per evitare suono ripetuto
bool pacmanGameOverDisplayed = false; // Flag per tracciare visualizzazione game over

// Variabili per Simon Says
#define SIMON_MAX_SEQUENCE 50
#define SIMON_RED 0
#define SIMON_GREEN 1
#define SIMON_BLUE 2
#define SIMON_YELLOW 3

enum SimonState {
  SIMON_IDLE,           // In attesa di iniziare
  SIMON_SHOWING,        // Mostra la sequenza
  SIMON_WAITING_INPUT,  // Aspetta input giocatore
  SIMON_CORRECT,        // Input corretto - animazione
  SIMON_WRONG,          // Input sbagliato - game over
  SIMON_LEVEL_UP        // Completato livello - animazione
};

byte simonSequence[SIMON_MAX_SEQUENCE];  // Sequenza di colori (0-3)
byte simonLevel = 0;                     // Livello corrente (lunghezza sequenza)
byte simonCurrentStep = 0;               // Step corrente durante la riproduzione/input
byte simonPlayerInput = 0;               // Numero di input corretti del giocatore
int simonScore = 0;                      // Punteggio totale
int simonBestScore = 0;                  // Miglior punteggio
SimonState simonState = SIMON_IDLE;
bool simonGameActive = false;
unsigned long simonLastUpdate = 0;
int simonShowDelay = 600;                // Tempo di visualizzazione colore (diminuisce con livello)
int simonCurrentColor = -1;              // Colore attualmente illuminato (-1 = nessuno)
unsigned long simonColorStartTime = 0;   // Timestamp inizio illuminazione colore
bool simonWaitingForRelease = false;     // Flag per aspettare rilascio pulsante

// Tonalità per ogni colore (Hz) - ispirate al Simon originale
const int simonTones[4] = {
  415,  // ROSSO - G#4
  310,  // VERDE - D#4
  252,  // BLU - C4
  209   // GIALLO - G#3
};

// ============================================
// VARIABILI ZUMA
// ============================================
#define ZUMA_MAX_BALLS 80
#define ZUMA_PATH_LENGTH 196
#define ZUMA_CANNON_X 7
#define ZUMA_CANNON_Y 7
#define ZUMA_NUM_COLORS 5
#define ZUMA_SHOOT_SPEED 8
#define ZUMA_BALL_SPAWN_INTERVAL 800
#define ZUMA_MIN_MATCH 3

struct ZumaBall {
  float position;
  uint8_t color;
  bool active;
  bool exploding;
  uint8_t explodeFrame;
};

struct ZumaProjectile {
  float x, y;
  float dx, dy;
  uint8_t color;
  bool active;
};

ZumaBall zumaChain[ZUMA_MAX_BALLS];
uint8_t zumaChainLength = 0;
ZumaProjectile zumaProjectile = {0, 0, 0, 0, 0, false};
uint8_t zumaShooterColor = 0;
uint8_t zumaNextColor = 1;
int16_t zumaAimAngle = 0;
uint16_t zumaScore = 0;
uint8_t zumaLevel = 1;
uint8_t zumaLives = 5;
bool zumaGameActive = false;
bool zumaGameOver = false;
unsigned long zumaLastUpdate = 0;
unsigned long zumaLastSpawn = 0;
int zumaSpeed = 400;
bool zumaChainMoving = true;
unsigned long zumaLastAimUpdate = 0;
int zumaComboCount = 0;
unsigned long zumaComboTime = 0;
uint8_t zumaPathData[ZUMA_PATH_LENGTH][2];

// Variabili per orologio
int clockColorMode = 1; // 0=rosso, 1=verde, 2=blu, 3=giallo, 4=ciano, 5=magenta, 6=bianco, 7=arancione, 8=rainbow
int secondsLedColorMode = 0; // 0=rosso, 1=verde, 2=blu, 3=giallo, 4=ciano, 5=magenta, 6=bianco, 7=arancione
int clockDisplayType = 0; // 0=classico, 1=compatto, 2=grande, 3=binario, 4=analogico, 5=verticale, 6=scorrevole, 7=compatto+giorno
bool useNTP = true;
String clockTimezone = "CET-1CEST,M3.5.0,M10.5.0/3"; // POSIX timezone per Italia/Roma con DST

// Variabili per alternanza automatica Orologio/Meteo/Data/Sensore Locale
bool clockWeatherAutoSwitch = false; // Abilita alternanza automatica
int clockDisplayInterval = 20; // Tempo visualizzazione orologio in secondi (default 20)
int weatherDisplayInterval = 5; // Tempo visualizzazione meteo in secondi (default 5)
bool dateDisplayEnabled = false; // Abilita visualizzazione data nell'alternanza
int dateDisplayInterval = 5; // Tempo visualizzazione data in secondi (default 5)
int dateColorMode = 1; // 0=rosso, 1=verde, 2=blu, 3=giallo, 4=ciano, 5=magenta, 6=bianco, 7=arancione, 8=rainbow
int dateDisplaySize = 0; // 0=piccolo (3x5), 1=grande (5x7)
bool localSensorDisplayEnabled = false; // Abilita visualizzazione sensore locale (temp/umidità) - COLORI FISSI: bianco per temp, ciano per umidità
int localSensorDisplayInterval = 5; // Tempo visualizzazione sensore locale in secondi (default 5)
float sensorTempOffset = 0.0; // Taratura manuale temperatura HTU21D (da -5.0 a +5.0)
int displaySequence = 0; // 0=Orologio→Meteo→Data, 1=Orologio→Data→Meteo, 2=Data→Orologio→Meteo
unsigned long lastClockWeatherSwitch = 0; // Ultimo momento di switch
int currentDisplayMode = 0; // 0=orologio, 1=meteo, 2=data, 3=sensore locale
int sequenceIndex = 0; // Indice nella sequenza corrente di alternanza
bool showingClock = true; // true = orologio, false = meteo (mantenuto per compatibilità)
bool forceRedraw = false; // Flag per forzare ridisegno dopo switch

// HUD Overlay per giochi con gamepad
bool hudOverlayActive = false;
unsigned long hudOverlayStartTime = 0;
int hudOverlayDuration = 5000; // 5 secondi
int hudScore = 0;
int hudLives = 0;
int hudLevel = 0;
bool hudShowLevel = true; // alcuni giochi non hanno livelli

// Variabili per spegnimento programmato
bool nightShutdownEnabled = false;
int nightShutdownStartHour = 23;
int nightShutdownStartMinute = 0;
int nightShutdownEndHour = 7;
int nightShutdownEndMinute = 0;

bool dayShutdownEnabled = false;
int dayShutdownStartHour = 9;
int dayShutdownStartMinute = 0;
int dayShutdownEndHour = 17;
int dayShutdownEndMinute = 0;
uint8_t dayShutdownDays = 0x7F; // Bitmap giorni: bit0=Lun...bit6=Dom, 0x7F=tutti

// Variabile per controllo manuale LED matrice
bool matrixLedEnabled = true; // true = LED accesi, false = LED spenti manualmente

// Variabili per audio/buzzer
bool soundEnabled = true; // true = suoni abilitati, false = muto

// Variabili per stazione meteo
struct WeatherData {
  String city;
  String description;
  int temperature;
  int humidity;
  int pressure;
  int windSpeed;
  String icon;
  bool isValid;
};

struct ForecastDay {
  String date;
  String description;
  int tempMin;
  int tempMax;
  int humidity;
  String icon;
};

WeatherData currentWeather;
ForecastDay forecast[5];
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 600000; // 10 minuti in millisecondi
bool weatherDataAvailable = false;


// ============================================
// BLUETOOTH GAMEPAD (Bluepad32)
// ============================================
#ifdef ENABLE_BLUEPAD32
ControllerPtr myControllers[2];  // Max 2 controller
#endif

// Struttura per tracking stato pulsanti gamepad (per debouncing)
struct GamepadState {
  bool dpadUp;
  bool dpadDown;
  bool dpadLeft;
  bool dpadRight;
  bool buttonA;
  bool buttonB;
  bool buttonX;
  bool buttonY;
  bool buttonStart;
  bool buttonSelect;
  unsigned long lastDpadUp;
  unsigned long lastDpadDown;
  unsigned long lastDpadLeft;
  unsigned long lastDpadRight;
  unsigned long lastButtonA;
  unsigned long lastButtonB;
  unsigned long lastButtonX;
  unsigned long lastButtonY;
  unsigned long lastButtonStart;
  unsigned long lastButtonSelect;
};

GamepadState gamepadStates[2] = {
  {false, false, false, false, false, false, false, false, false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {false, false, false, false, false, false, false, false, false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

// Costanti per gamepad
const int GAMEPAD_DEBOUNCE_MS = 150;      // Debounce per pulsanti
const int GAMEPAD_REPEAT_INITIAL_MS = 400; // Ritardo iniziale prima di auto-repeat
const int GAMEPAD_REPEAT_MS = 100;         // Intervallo auto-repeat
const int ANALOG_DEADZONE = 300;           // Deadzone per stick analogici

// Costanti D-Pad Bluepad32
const uint8_t DPAD_UP_MASK    = 0x01;
const uint8_t DPAD_DOWN_MASK  = 0x02;
const uint8_t DPAD_RIGHT_MASK = 0x04;
const uint8_t DPAD_LEFT_MASK  = 0x08;

// Costanti Misc Buttons - usiamo quelle definite in Bluepad32:
// MISC_BUTTON_SYSTEM, MISC_BUTTON_BACK, MISC_BUTTON_HOME, MISC_BUTTON_START
// Alias per compatibilità con il codice esistente:
#ifdef ENABLE_BLUEPAD32
#define MISC_BUTTON_MENU MISC_BUTTON_START
#endif

// Variabili per navigazione menu con gamepad
int menuCursorX = 0;  // 0 = colonna sinistra, 1 = colonna destra
int menuCursorY = 0;  // 0 = riga superiore, 1 = riga inferiore
unsigned long menuCursorBlink = 0;
bool menuCursorVisible = true;

// Variabili per Tris con gamepad (controllo cursore)
int trisCursorPos = 0;  // Posizione cursore (0-8 per griglia 3x3)
bool trisCursorEnabled = false;  // Abilita cursore solo quando si usa gamepad

// Flag per connessione controller
bool gamepadConnected[2] = {false, false};

// Flag per modalità Bluetooth (quando attivo, WiFi è disabilitato)
bool bluetoothMode = false;

// Variabili per pairing mode (pagina web /btpairing)
bool btPairingModeActive = false;
unsigned long btPairingModeStart = 0;
const unsigned long BT_PAIRING_MODE_TIMEOUT = 180000; // 3 minuti timeout
int8_t btPairingTargetSlot = -1; // -1 = qualsiasi slot, 0 = solo Controller 1, 1 = solo Controller 2

// Flag per controller Bluetooth già associati (salvati in EEPROM)
bool btController1Paired = false;
bool btController2Paired = false;

// Cache per scan WiFi (per velocizzare pagina AP)
int cachedNetworksCount = -1;  // -1 = scan non ancora fatto
bool wifiScanInProgress = false;

// Stato gioco salvato prima di entrare in modalità Bluetooth
SystemState savedGameState = STATE_GAME_MENU;
bool btWaitingForController = false;  // In attesa di connessione controller
bool btWaitingForStart = false;       // Controller connesso, in attesa di START
unsigned long btPairingStartTime = 0; // Timestamp inizio pairing
const unsigned long BT_PAIRING_TIMEOUT = 60000; // Timeout pairing: 60 secondi (1 minuto)

// Variabili generali
unsigned long lastInputTime = 0;
int currentBrightness = BRIGHTNESS_DAY;
bool pendingBrightnessUpdate = false;  // Flag per aggiornamento luminosità dal web
bool nightMode = false;
unsigned long ipScrollStartTime = 0;
bool ipScrollActive = false;

// ============================================
// PROTOTIPI DI FUNZIONE
// ============================================
// WiFi
bool connectToSavedWiFi(bool showBootText = true);
void startWiFiManager();
void handleWiFiSetup();
void handleWiFiConnect();
void startWebServer();

// EEPROM
void saveConfig();
void loadConfig();
void setDefaultConfig();

// Web Server
void handleRoot();
void handleControl();
void handleTris();
void handleTrisMove();
void handleTrisScore();
void handleTrisResetScore();
void handleTextScroll();
void handleUpdateText();
void handleClock();
void handleSetClock();
void handleBrightness();
void handleReboot();
void handleReset();
void handleWeather();
void handleWeatherConfig();
void handleSaveWeatherConfig();
void handleWeatherUpdate();
void handleWeatherTest();
void handleSpaceInvaders();
void handleSIControl();
void handlePong();
void handlePongControl();
void handleSnake();
void handleSnakeControl();
void handleBreakout();
void handleBreakoutControl();
void handleScoreboard();
void handleScoreboardControl();
void handleTetris();
void handleTetrisControl();
void handleStopwatch();
void handleStopwatchControl();
void handleTimer();
void handleTimerControl();

// Calendario Eventi
void handleCalendar();
void handleCalendarSave();
void handleCalendarUpdate();
void handleCalendarDelete();
void handleCalendarList();
void saveCalendarEvents();
void loadCalendarEvents();
void checkCalendarEvents();
void triggerCalendarEvent(int index);
CRGB getCalendarEventColor(int colorIndex, int charIndex);
void drawCalendarEvent();

// Sensore HTU21 (Temperatura/Umidità)
void readSensorData();
void handleSensorData();

// Stopwatch & Timer
void drawStopwatch();
void drawTimer();
void updateStopwatch();
void updateTimer();

// Alarm (Sveglia)
void checkAlarm();
void handleAlarmRinging();
void playAlarmRingtone(uint8_t ringtoneIndex);
void playMelody(const Note* melody, int length);
void startMelody(const Note* melody, int length);
void updateMelody();
void stopMelody();

// Cronotermostato
void handleThermostat();
void handleThermostatConfig();
void handleThermostatSchedule();
void handleSetThermostat();
void handleThermostatStatus();
void checkThermostat();
bool setShellyState(bool on);
bool getShellyState();
float getThermostatTargetTemp();
void loadThermostatSchedule();
void saveThermostatSchedule();
void initThermostatDefaults();
void drawThermostat();
void playPacmanBeginningMelody();
void getMelodyByIndex(uint8_t index, const Note** melody, int* length);
void handleAlarm();
void handleSetAlarm();
void handleStopAlarm();
void handleTestAlarm();

// Tris Game
void resetTrisGame();
bool checkTrisWinner(char player);
bool isTrisBoardFull();
void makeAIMove();
void drawTrisOnMatrix();

// Space Invaders
void resetSpaceInvaders(bool fullReset = true);
void resetSpaceInvadersPlayer(); // Reset solo giocatore (per perdita vita)
void updateSpaceInvaders();
void drawSpaceInvaders();
void drawGameOver();
void drawLevelComplete();
void siShoot(int player);
void siMovePlayer(int player, int dir);
void siInitShields();
CRGB siGetAlienColor(int row);

// Pong
void resetPong(bool resetPaddles = true);
void updatePong();
void drawPong();
void drawPongGameOver();
void pongMovePaddle(int player, int dir);

// Snake
void resetSnake();
void updateSnake();
void drawSnake();
void snakeChangeDir(int dx, int dy);
void snakePlaceFood();

// Breakout
void resetBreakout();
void initBreakoutLevel(int level);
void updateBreakout();
void drawBreakout();
void forceBreakoutRedraw();
void breakoutMovePaddle(int dir);
void breakoutNextLevel();

// Scoreboard
void drawScoreboard();
void updateScoreboardFromWeb(int player, int points);

// Tetris
void resetTetris();
void updateTetris();
void drawTetris();
bool tetrisCheckCollision(int type, int rotation, int x, int y);
void tetrisLockPiece();
int tetrisClearLines();
void tetrisMovePiece(int dx, int dy);
void tetrisRotatePiece();
void tetrisSpawnNewPiece();
bool tetrisGetPieceBlock(int type, int rotation, int bx, int by);
CRGB tetrisGetPieceColor(int type);

// Pac-Man
void resetPacman(bool fullReset = true);
void initPacmanMaze();
void updatePacman();
bool updatePacmanGhosts();
void drawPacman();
void drawPacmanGameOver();
void pacmanChangeDir(int dx, int dy);
bool pacmanCanMove(int x, int y, int dx, int dy);
void pacmanEatDot(int x, int y);
void pacmanCollisionCheck();
void pacmanResetPositions();
void pacmanNextLevel();
CRGB pacmanGetGhostColor(int ghostIndex);
void handlePacman();
void handlePacmanControl();
void handlePacmanStatus();

// Simon Says
void resetSimon();
void updateSimon();
void drawSimon();
void simonAddToSequence();
void simonShowSequence();
void simonCheckInput(byte color);
void simonDrawQuadrant(byte quadrant, bool highlight);
CRGB simonGetColor(byte color, bool highlight);
void simonPlayTone(byte color);
void handleSimon();
void handleSimonControl();

// ZUMA
void initZumaPath();
void resetZuma();
void updateZuma();
void drawZuma();
void zumaShoot();
void zumaRotateAim(int direction);
void zumaSwapColors();
void zumaAdvanceChain();
void zumaSpawnBall();
void zumaInsertBall(int insertPos);
int zumaCheckMatches(int fromPos);
void zumaRemoveBalls(int startPos, int count);
void zumaCollapseChain();
void zumaGameOverCheck();
CRGB zumaGetBallColor(uint8_t colorIndex);
void zumaDrawCannon();
void zumaDrawChain();
void zumaDrawProjectile();
void handleZuma();
void handleZumaControl();
#ifdef ENABLE_BLUEPAD32
void handleGamepadZuma(int idx, ControllerPtr ctl);
#endif

// Weather (Open-Meteo API)
bool updateWeatherData();
bool updateForecastData();
bool getCoordinatesFromCity(String city, float &lat, float &lon);
String mapWMOCodeToIcon(int wmoCode, bool isNight);
String getWMODescription(int wmoCode);
void drawWeatherOnMatrix();
void drawWeatherIcon(String icon, int x, int y, CRGB color, int animFrame);
void drawSmallDigit(int digit, int x, int y, CRGB color);

// Display
void displayWiFiSetupMode();
void displayIP();
void displayStaticIP();
void displayBootText();
void scrollTextOnMatrix();
void drawClockOnMatrix();
void drawDateOnMatrix();
void drawLocalSensorOnMatrix();
void drawBigDigit(int digit, int x, int y, CRGB color);
void drawBigDigitOldStyle(int digit, int x, int y, CRGB color);
void drawClockCompact(int h, int m, int s, CRGB digitColor);
void drawClockCompactDay(int h, int m, int s, CRGB digitColor);
void drawClockBinary(int h, int m, int s, CRGB digitColor);
void drawClockAnalog(int h, int m, int s, CRGB digitColor);
void drawClockLarge(int h, int m, int s, CRGB digitColor);
void drawClockVertical(int h, int m, int s, CRGB digitColor);
void drawClockScrolling(int h, int m, int s, CRGB digitColor);
CRGB getClockColor();
CRGB getSecondsLedColor();
CRGB getDateColor();
void drawCharacter(char ch, int x, int y, CRGB color);
void setPixel(int x, int y, CRGB color);
void fillMatrix(CRGB color);
void clearMatrix();
void drawMenu();
void bootSequence();

// Utility
void debugWiFiStatus();
void checkForHardReset();
void checkButtons();
void changeState(SystemState newState);
bool isInShutdownPeriod();

// Audio/Buzzer
void playTone(int frequency, int duration);
void playBeep();
void playSuccess();
void playError();
void playGameOver();
void playLevelUp();
void playEat();
void playShoot();

// Bluetooth Gamepad (Bluepad32)
#ifdef ENABLE_BLUEPAD32
void onConnectedController(ControllerPtr ctl);
void onDisconnectedController(ControllerPtr ctl);
void setupGamepads();
void processGamepadInput();
void processGamepadForCurrentState(int controllerIndex, ControllerPtr ctl);
void handleGamepadMenu(int idx, ControllerPtr ctl);
void handleGamepadClock(int idx, ControllerPtr ctl);
void handleGamepadPong(int idx, ControllerPtr ctl);
void handleGamepadSnake(int idx, ControllerPtr ctl);
void handleGamepadTetris(int idx, ControllerPtr ctl);
void handleGamepadPacman(int idx, ControllerPtr ctl);
void handleGamepadSpaceInvaders(int idx, ControllerPtr ctl);
void handleGamepadBreakout(int idx, ControllerPtr ctl);
void handleGamepadZuma(int idx, ControllerPtr ctl);
#endif

// Bluetooth/WiFi mode switching
void enableBluetoothMode();
void enableBluetoothModeWithGame(SystemState gameState);
void disableBluetoothMode();
void quickRestartToWiFi();
void handleEnableBluetooth();
void handleBluetoothPairing();
void handleBluetoothPairingStart();
void handleBluetoothPairingStop();
void handleBluetoothPairingStatus();
void handleBluetoothForget();
void showBluetoothPairingScreen();
void showBluetoothWaitingStart();

void drawMenuWithCursor();
void drawTrisWithCursor();

// ============================================
// EEPROM - GESTIONE CONFIGURAZIONE
// ============================================
void setDefaultConfig() {
  WiFiConfig config;
  memset(&config, 0, sizeof(config));

  // Valori di default
  config.brightness = BRIGHTNESS_DAY;
  config.clockColorMode = 1; // verde
  config.secondsLedColorMode = 0; // rosso
  config.clockDisplayType = 0; // classico
  config.clockWeatherAutoSwitch = false;
  config.clockDisplayInterval = 20;
  config.weatherDisplayInterval = 5;
  strncpy(config.clockTimezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config.clockTimezone) - 1);

  config.nightShutdownEnabled = false;
  config.nightShutdownStartHour = 23;
  config.nightShutdownStartMinute = 0;
  config.nightShutdownEndHour = 7;
  config.nightShutdownEndMinute = 0;

  config.dayShutdownEnabled = false;
  config.dayShutdownStartHour = 9;
  config.dayShutdownStartMinute = 0;
  config.dayShutdownEndHour = 17;
  config.dayShutdownEndMinute = 0;
  config.dayShutdownDays = 0x7F; // Tutti i giorni attivi

  config.matrixLedEnabled = true;

  // Impostazioni testo scorrevole
  config.scrollTextColor = 1; // verde
  config.scrollTextSize = 0; // piccolo
  config.scrollSpeed = 50; // normale

  // Impostazioni visualizzazione data
  config.dateDisplayEnabled = false; // disabilitata di default
  config.dateDisplayInterval = 5; // 5 secondi
  config.dateColorMode = 1; // verde
  config.dateDisplaySize = 0; // piccolo
  config.displaySequence = 0; // Orologio→Meteo→Data

  // Impostazioni sveglia
  config.alarmEnabled = false; // disabilitata di default
  config.alarmHour = 7; // 7:00 AM
  config.alarmMinute = 0;
  config.alarmDays = 0b01111100; // Lun-Ven (bit 0-4 attivi)
  config.alarmRingtone = 0; // Mario di default
  config.alarmDuration = 30; // 30 secondi

  // Impostazioni sensore locale (temperatura/umidità ambiente)
  config.localSensorDisplayEnabled = false; // disabilitata di default
  config.localSensorDisplayInterval = 5; // 5 secondi

  // Impostazioni cronotermostato
  config.thermostatEnabled = false; // disabilitato di default
  memset(config.shellyIP, 0, sizeof(config.shellyIP));
  config.thermostatHysteresis = 0.5; // 0.5°C di isteresi
  config.thermostatManualTemp = 20.0; // 20°C temperatura manuale
  config.thermostatManualOverride = false;
  config.thermostatDefaultTemp = 19; // 19°C temperatura default

  // Controller Bluetooth associati
  config.btController1Paired = false;
  config.btController2Paired = false;

  config.magicNumber = 0xCAFE; // Indica che la configurazione è valida

  // Taratura sensore HTU21D
  config.sensorTempOffset = 0.0; // Nessun offset di default

  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Default configuration saved to EEPROM");
}

void saveConfig() {
  WiFiConfig config;
  EEPROM.get(0, config);

  // Salva le impostazioni correnti
  config.brightness = currentBrightness;
  config.clockColorMode = clockColorMode;
  config.secondsLedColorMode = secondsLedColorMode;
  config.clockDisplayType = clockDisplayType;
  config.clockWeatherAutoSwitch = clockWeatherAutoSwitch;
  config.clockDisplayInterval = clockDisplayInterval;
  config.weatherDisplayInterval = weatherDisplayInterval;
  strncpy(config.clockTimezone, clockTimezone.c_str(), sizeof(config.clockTimezone) - 1);

  config.nightShutdownEnabled = nightShutdownEnabled;
  config.nightShutdownStartHour = nightShutdownStartHour;
  config.nightShutdownStartMinute = nightShutdownStartMinute;
  config.nightShutdownEndHour = nightShutdownEndHour;
  config.nightShutdownEndMinute = nightShutdownEndMinute;

  config.dayShutdownEnabled = dayShutdownEnabled;
  config.dayShutdownStartHour = dayShutdownStartHour;
  config.dayShutdownStartMinute = dayShutdownStartMinute;
  config.dayShutdownEndHour = dayShutdownEndHour;
  config.dayShutdownEndMinute = dayShutdownEndMinute;
  config.dayShutdownDays = dayShutdownDays;

  config.matrixLedEnabled = matrixLedEnabled;

  // Salva impostazioni testo scorrevole
  config.scrollTextColor = scrollTextColor;
  config.scrollTextSize = scrollTextSize;
  config.scrollSpeed = scrollSpeed;

  // Salva impostazioni visualizzazione data
  config.dateDisplayEnabled = dateDisplayEnabled;
  config.dateDisplayInterval = dateDisplayInterval;
  config.dateColorMode = dateColorMode;
  config.dateDisplaySize = dateDisplaySize;
  config.displaySequence = displaySequence;

  // Salva impostazioni sveglia
  config.alarmEnabled = alarmEnabled;
  config.alarmHour = alarmHour;
  config.alarmMinute = alarmMinute;
  config.alarmDays = alarmDays;
  config.alarmRingtone = alarmRingtone;
  config.alarmDuration = alarmDuration;

  // Salva impostazioni sensore locale
  config.localSensorDisplayEnabled = localSensorDisplayEnabled;
  config.localSensorDisplayInterval = localSensorDisplayInterval;

  // Salva impostazioni cronotermostato
  config.thermostatEnabled = thermostatEnabled;
  strncpy(config.shellyIP, shellyIP.c_str(), sizeof(config.shellyIP) - 1);
  config.shellyIP[sizeof(config.shellyIP) - 1] = '\0';
  config.thermostatHysteresis = thermostatHysteresis;
  config.thermostatManualTemp = thermostatManualTemp;
  config.thermostatManualOverride = thermostatManualOverride;
  config.thermostatDefaultTemp = thermostatDefaultTemp;

  // Salva stato controller Bluetooth associati
  config.btController1Paired = btController1Paired;
  config.btController2Paired = btController2Paired;

  config.magicNumber = 0xCAFE;

  // Salva taratura sensore HTU21D
  config.sensorTempOffset = sensorTempOffset;

  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Configuration saved to EEPROM");
}

void loadConfig() {
  WiFiConfig config;
  EEPROM.get(0, config);

  // Verifica se la configurazione è valida
  if (config.magicNumber != 0xCAFE) {
    Serial.println("EEPROM not initialized, setting defaults...");
    setDefaultConfig();
    EEPROM.get(0, config);
  }

  // Carica le impostazioni
  if (config.brightness >= 1 && config.brightness <= 128) {
    currentBrightness = config.brightness;
  }

  clockColorMode = config.clockColorMode;
  secondsLedColorMode = config.secondsLedColorMode;
  clockDisplayType = config.clockDisplayType;
  clockWeatherAutoSwitch = config.clockWeatherAutoSwitch;
  clockDisplayInterval = config.clockDisplayInterval;
  weatherDisplayInterval = config.weatherDisplayInterval;

  if (strlen(config.clockTimezone) > 0) {
    clockTimezone = String(config.clockTimezone);
  }

  nightShutdownEnabled = config.nightShutdownEnabled;
  nightShutdownStartHour = config.nightShutdownStartHour;
  nightShutdownStartMinute = config.nightShutdownStartMinute;
  nightShutdownEndHour = config.nightShutdownEndHour;
  nightShutdownEndMinute = config.nightShutdownEndMinute;

  dayShutdownEnabled = config.dayShutdownEnabled;
  dayShutdownStartHour = config.dayShutdownStartHour;
  dayShutdownStartMinute = config.dayShutdownStartMinute;
  dayShutdownEndHour = config.dayShutdownEndHour;
  dayShutdownEndMinute = config.dayShutdownEndMinute;
  dayShutdownDays = config.dayShutdownDays;
  // Compatibilità EEPROM vecchia: se valore non inizializzato, default tutti i giorni
  if (dayShutdownDays == 0x00 || dayShutdownDays > 0x7F) dayShutdownDays = 0x7F;

  matrixLedEnabled = config.matrixLedEnabled;

  // Carica impostazioni testo scorrevole
  scrollTextColor = config.scrollTextColor;
  scrollTextSize = config.scrollTextSize;
  scrollSpeed = config.scrollSpeed;

  // Validazione testo scorrevole
  if (scrollTextColor > 8) scrollTextColor = 1;  // Default verde
  if (scrollTextSize > 2) scrollTextSize = 0;     // Default piccolo
  if (scrollSpeed < 10 || scrollSpeed > 200) scrollSpeed = 50;  // Default normale

  // Carica impostazioni visualizzazione data
  dateDisplayEnabled = config.dateDisplayEnabled;
  dateDisplayInterval = config.dateDisplayInterval;
  dateColorMode = config.dateColorMode;
  dateDisplaySize = config.dateDisplaySize;
  displaySequence = config.displaySequence;

  // Carica impostazioni sveglia
  alarmEnabled = config.alarmEnabled;
  alarmHour = config.alarmHour;
  alarmMinute = config.alarmMinute;
  alarmDays = config.alarmDays;
  alarmRingtone = config.alarmRingtone;
  alarmDuration = config.alarmDuration;

  // Validazione campi sveglia
  if (alarmHour > 23) alarmHour = 7;
  if (alarmMinute > 59) alarmMinute = 0;
  if (alarmRingtone > 10) alarmRingtone = 0;
  if (alarmDuration < 5 || alarmDuration > 180) alarmDuration = 30;

  // Carica impostazioni sensore locale
  localSensorDisplayEnabled = config.localSensorDisplayEnabled;
  localSensorDisplayInterval = config.localSensorDisplayInterval;

  // Validazione campi sensore locale
  if (localSensorDisplayInterval < 5 || localSensorDisplayInterval > 300) localSensorDisplayInterval = 15;

  // Carica taratura sensore HTU21D
  sensorTempOffset = config.sensorTempOffset;
  // Validazione: se valore è NaN o fuori range (es. EEPROM mai scritta), default 0.0
  if (isnan(sensorTempOffset) || sensorTempOffset < -5.0 || sensorTempOffset > 5.0) sensorTempOffset = 0.0;

  // Carica impostazioni cronotermostato
  thermostatEnabled = config.thermostatEnabled;
  if (strlen(config.shellyIP) > 0) {
    shellyIP = String(config.shellyIP);
  }
  thermostatHysteresis = config.thermostatHysteresis;
  thermostatManualTemp = config.thermostatManualTemp;
  thermostatManualOverride = config.thermostatManualOverride;
  thermostatDefaultTemp = config.thermostatDefaultTemp;

  // Validazione campi termostato
  if (thermostatHysteresis < 0.1 || thermostatHysteresis > 2.0) thermostatHysteresis = 0.5;
  if (thermostatManualTemp < 5.0 || thermostatManualTemp > 30.0) thermostatManualTemp = 20.0;
  if (thermostatDefaultTemp < 5 || thermostatDefaultTemp > 30) thermostatDefaultTemp = 19;

  // Carica stato controller Bluetooth associati
  btController1Paired = config.btController1Paired;
  btController2Paired = config.btController2Paired;

  Serial.println("Configuration loaded from EEPROM");
  Serial.print("Clock Color: "); Serial.println(clockColorMode);
  Serial.print("Display Type: "); Serial.println(clockDisplayType);
  Serial.print("Timezone: "); Serial.println(clockTimezone);
  Serial.print("Matrix LED: "); Serial.println(matrixLedEnabled ? "ON" : "OFF");
  Serial.print("Scroll Color: "); Serial.println(scrollTextColor);
  Serial.print("Scroll Size: "); Serial.println(scrollTextSize);
  Serial.print("Date Display: "); Serial.println(dateDisplayEnabled ? "ENABLED" : "DISABLED");
  Serial.print("Date Interval: "); Serial.print(dateDisplayInterval); Serial.println("s");
  Serial.print("Thermostat: "); Serial.println(thermostatEnabled ? "ENABLED" : "DISABLED");
  Serial.print("Shelly IP: "); Serial.println(shellyIP);
}

// ============================================
// SETUP - INIZIALIZZAZIONE
// ============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nCONSOLE QUADRA - Starting...");

  // Inizializza EEPROM
  EEPROM.begin(sizeof(WiFiConfig));

  // Carica tutte le impostazioni salvate
  loadConfig();

  // Inizializza LittleFS per il calendario
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed! Formatting...");
  } else {
    Serial.println("LittleFS initialized successfully");
    loadCalendarEvents();
    loadThermostatSchedule();
  }

  // Inizializza pulsanti
  pinMode(BUTTON_MODE, INPUT_PULLUP);
  pinMode(BUTTON_SEC, INPUT_PULLUP);

  // Inizializza buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Inizializza sensore HTU21 (Temperatura/Umidità)
  Wire.begin(I2C_SDA, I2C_SCL);
  if (htu.begin()) {
    sensorAvailable = true;
    Serial.println("HTU21D sensor initialized successfully!");
  } else {
    sensorAvailable = false;
    Serial.println("HTU21D sensor not found!");
  }

  // Controlla reset hardware
  delay(100);
  checkForHardReset();

  // Inizializza LED Matrix
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setDither(0);  // Disabilita dithering temporale per evitare flickering
  FastLED.setCorrection(TypicalLEDStrip);  // Correzione colore per strip LED
  FastLED.setBrightness(currentBrightness);
  FastLED.clear();
  FastLED.show();

  // Fase di boot (salta se quick restart da Bluetooth)
  bool isQuickRestart = (quickRestartFlag == QUICK_RESTART_MAGIC);
  if (isQuickRestart) {
    quickRestartFlag = 0;  // Reset flag
    Serial.println("Quick restart from Bluetooth - skipping boot sequence");
  } else {
    bootSequence();
  }

  // Inizializza Bluepad32 UNA SOLA VOLTA all'avvio
  // (necessario farlo prima del WiFi per evitare conflitti)
  #ifdef ENABLE_BLUEPAD32
  Serial.println("Initializing Bluepad32...");
  BP32.setup(&onConnectedController, &onDisconnectedController);
  // Disabilita nuove connessioni Bluetooth di default (si abilita solo quando serve)
  BP32.enableNewBluetoothConnections(false);
  bluetoothMode = false;
  btPairingModeActive = false;
  Serial.println("Bluepad32 initialized (new connections disabled by default)");
  #endif
  delay(100);

  // Prova a connettersi al WiFi salvato (senza mostrare CONSOLE QUADRA se quick restart)
  Serial.println("Attempting to connect to saved WiFi...");
  if (!connectToSavedWiFi(!isQuickRestart)) {
    Serial.println("Failed to connect, starting WiFi Manager...");
    startWiFiManager();
  } else {
    wifiConnected = true;
    Serial.println("WiFi connected successfully!");

    // Configura NTP con ezTime usando POSIX timezone
    setDebug(INFO);
    Serial.println("Waiting for NTP sync...");
    Serial.print("Timezone POSIX: ");
    Serial.println(clockTimezone);

    // Usa setPosix invece di setLocation per maggiore precisione
    myTZ.setPosix(clockTimezone);

    // Attendi sincronizzazione NTP
    waitForSync(10); // Timeout 10 secondi

    if (timeStatus() == timeSet) {
      Serial.println("NTP synchronized successfully!");
      Serial.print("Ora locale: ");
      Serial.println(myTZ.dateTime("d/m/Y H:i:s"));
      Serial.print("UTC: ");
      Serial.println(UTC.dateTime("d/m/Y H:i:s"));
      Serial.print("Offset UTC: ");
      Serial.print(myTZ.getOffset());
      Serial.println(" minuti");
    } else {
      Serial.println("NTP sync failed, using default time");
    }

    startWebServer();

    // Aggiornamento automatico meteo al riavvio (se configurato con Open-Meteo)
    WiFiConfig config;
    EEPROM.get(0, config);
    // Verifica che le coordinate siano configurate (non 0,0)
    if ((config.weatherLatitude != 0.0 || config.weatherLongitude != 0.0) && strlen(config.weatherCity) > 0) {
      Serial.println("Auto-updating weather data on startup (Open-Meteo)...");
      bool weatherOk = updateWeatherData();
      if (weatherOk) {
        updateForecastData(); // Aggiorna anche le previsioni
        weatherDataAvailable = true;
        lastWeatherUpdate = millis();

        // Abilita automaticamente l'alternanza orologio/meteo/data
        clockWeatherAutoSwitch = true;
        lastClockWeatherSwitch = millis();
        saveConfig(); // Salva la configurazione con l'alternanza abilitata

        Serial.println("Weather data auto-updated successfully on startup");
        Serial.println("Auto-switch clock/weather/date enabled");
      } else {
        Serial.println("Weather data auto-update failed on startup");
      }
    }
  }

  debugWiFiStatus();

  // Bluetooth Gamepad (Bluepad32) NON viene inizializzato automaticamente.
  // Verrà attivato dall'utente tramite il pulsante "Bluetooth" nell'interfaccia web dei giochi.
  // Questo evita conflitti tra WiFi e Bluetooth che condividono la stessa radio.
  #ifdef ENABLE_BLUEPAD32
  Serial.println("Bluepad32: Ready (activate via web interface in games)");
  #endif

  // Se connesso al WiFi, mostra l'IP scorrevole (salta se quick restart)
  if (wifiConnected) {
    if (!isQuickRestart) {
      displayIP();
      ipScrollActive = true;
      ipScrollStartTime = millis();
    } else {
      // Quick restart: vai direttamente all'orologio
      changeState(STATE_GAME_CLOCK);
    }
  } else if (apMode) {
    // Già in STATE_WIFI_SETUP, impostato da startWiFiManager()
  } else {
    changeState(STATE_GAME_MENU);
  }
}

void bootSequence() {
  // Schermata rossa
  fillMatrix(CRGB(255, 0, 0));
  delay(2000);

  // Schermata ciano
  fillMatrix(CRGB(0, 255, 255));
  delay(2000);

  clearMatrix();
}

// ============================================
// FUNZIONI DI UTILITY PER LED MATRIX
// ============================================
void setPixel(int x, int y, CRGB color) {
  if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
    // Inverti x per correggere lo specchiamento
    x = MATRIX_WIDTH - 1 - x;

    int index;
    if (y % 2 == 0) {
      index = y * MATRIX_WIDTH + x;
    } else {
      index = y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);
    }

    if (index >= 0 && index < NUM_LEDS) {
      leds[index] = color;
    }
  }
}

void fillMatrix(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);  // Usa funzione FastLED ottimizzata
  FastLED.show();
}

void fillMatrixNoShow(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);  // Usa funzione FastLED ottimizzata
  // NON chiama FastLED.show() - da chiamare esternamente per ottimizzazione
}

void clearMatrix() {
  FastLED.clear();  // Usa funzione FastLED nativa
  FastLED.show();
}

void clearMatrixNoShow() {
  FastLED.clear();  // Usa funzione FastLED nativa - non chiama show()
}

// === HUD OVERLAY FUNCTIONS ===

void showGameHUD(int score, int lives, int level, bool hasLevel) {
  hudScore = score;
  hudLives = lives;
  hudLevel = level;
  hudShowLevel = hasLevel;
  hudOverlayActive = true;
  hudOverlayStartTime = millis();
}

void drawGameHUD() {
  clearMatrixNoShow();

  CRGB scoreColor = CRGB(0, 255, 0);  // Verde
  CRGB livesColor = CRGB(255, 0, 0);  // Rosso
  CRGB levelColor = CRGB(0, 200, 255); // Ciano

  // Score centrato (riga 1) - 3 cifre a x=2, 6, 10 (totale 11 pixel, centrato)
  int s = hudScore;
  if (s > 999) s = 999;
  drawSmallDigit3x5(s / 100, 2, 1, scoreColor);
  drawSmallDigit3x5((s / 10) % 10, 6, 1, scoreColor);
  drawSmallDigit3x5(s % 10, 10, 1, scoreColor);

  // Level e Vite (riga 9)
  if (hudShowLevel) {
    // L: pattern 3x5 a x=0
    setPixel(0, 9, levelColor);
    setPixel(0, 10, levelColor);
    setPixel(0, 11, levelColor);
    setPixel(0, 12, levelColor);
    setPixel(0, 13, levelColor);
    setPixel(1, 13, levelColor);
    setPixel(2, 13, levelColor);
    drawSmallDigit3x5(hudLevel % 10, 4, 9, levelColor);

    // V: pattern 3x5 a x=9 + digit a x=13
    setPixel(9, 9, livesColor);
    setPixel(9, 10, livesColor);
    setPixel(10, 11, livesColor);
    setPixel(10, 12, livesColor);
    setPixel(11, 13, livesColor);
    drawSmallDigit3x5(hudLives % 10, 13, 9, livesColor);
  } else {
    // Solo vite, centrate
    // V: pattern 3x5 a x=4 + digit a x=8
    setPixel(4, 9, livesColor);
    setPixel(4, 10, livesColor);
    setPixel(5, 11, livesColor);
    setPixel(5, 12, livesColor);
    setPixel(6, 13, livesColor);
    drawSmallDigit3x5(hudLives % 10, 8, 9, livesColor);
  }

  FastLED.show();
}

void updateGameHUD() {
  if (hudOverlayActive) {
    if (millis() - hudOverlayStartTime >= hudOverlayDuration) {
      hudOverlayActive = false;
      // Forza ridisegno quando HUD termina
      forceRedraw = true;
      if (currentState == STATE_GAME_BREAKOUT) {
        forceBreakoutRedraw();
      }
    } else {
      drawGameHUD();
    }
  }
}

// === END HUD OVERLAY ===

void debugWiFiStatus() {
  Serial.println("=== WiFi Debug ===");
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected to: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Not connected");
  }

  WiFiConfig config;
  EEPROM.get(0, config);
  Serial.print("Saved SSID: ");
  Serial.println(config.ssid);
  Serial.println("==================");
}

void checkForHardReset() {
  if (digitalRead(BUTTON_MODE) == LOW && digitalRead(BUTTON_SEC) == LOW) {
    Serial.println("Resetting WiFi configuration...");
    WiFiConfig config;
    memset(&config, 0, sizeof(config));
    EEPROM.put(0, config);
    EEPROM.commit();
    delay(1000);
    Serial.println("Restarting...");
    ESP.restart();
  }
}

// ============================================
// WIFI MANAGER
// ============================================
bool connectToSavedWiFi(bool showBootText) {
  WiFiConfig config;
  EEPROM.get(0, config);

  // Verifica se la configurazione è valida
  if (config.magicNumber != 0xCAFE) {
    Serial.println("EEPROM configuration not valid (magic number mismatch)");
    return false;
  }

  if (strlen(config.ssid) == 0) {
    Serial.println("No WiFi configuration found in EEPROM");
    return false;
  }

  Serial.print("Connecting to: ");
  Serial.println(config.ssid);
  Serial.print("Password length: ");
  Serial.println(strlen(config.password));

  // Mostra "CONSOLE QUADRA" durante la connessione (solo se richiesto)
  if (showBootText) {
    displayBootText();
  }

  // === ROBUST RADIO CLEANUP (importante dopo transizione BT) ===
  // Assicura che le nuove connessioni Bluetooth siano disabilitate
  #ifdef ENABLE_BLUEPAD32
  BP32.enableNewBluetoothConnections(false);
  delay(100);
  yield();
  #endif

  // Reset WiFi
  WiFi.disconnect(true);
  delay(200);
  yield();

  // Riattiva WiFi in modalità station
  WiFi.mode(WIFI_STA);
  delay(200);
  yield();

  // Inizia connessione
  WiFi.begin(config.ssid, config.password);

  int attempts = 0;
  int maxAttempts = 50; // Aumentato a 25 secondi (50 * 500ms)

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    yield(); // Alimenta watchdog durante attesa connessione
    Serial.print(".");

    // Stampa lo stato dettagliato ogni 5 tentativi
    if (attempts % 5 == 0 && attempts > 0) {
      Serial.print(" (Status: ");
      Serial.print(WiFi.status());
      Serial.print(") ");
    }

    // Verifica errori specifici per uscita anticipata
    if (WiFi.status() == WL_NO_SSID_AVAIL && attempts > 15) {
      Serial.println("\nSSID not found, stopping early");
      break;
    }
    if (WiFi.status() == WL_CONNECT_FAILED && attempts > 15) {
      Serial.println("\nConnection failed (wrong password?), stopping early");
      break;
    }

    attempts++;
  }

  // Pulisci solo se abbiamo mostrato il boot text
  if (showBootText) {
    clearMatrix();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    wifiConnected = true;
    return true;
  } else {
    Serial.print("\nFailed to connect to WiFi. Status: ");
    Serial.println(WiFi.status());

    // Messaggi di errore specifici
    switch (WiFi.status()) {
      case WL_NO_SSID_AVAIL:
        Serial.println("Error: SSID not available (network not found)");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("Error: Connection failed (check password)");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("Error: Connection lost");
        break;
      case WL_DISCONNECTED:
        Serial.println("Error: Disconnected");
        break;
      default:
        Serial.println("Error: Unknown WiFi error");
    }

    // Cleanup in caso di errore
    WiFi.disconnect(true);
    delay(200);
    yield();
    wifiConnected = false;
    return false;
  }
}

// Handler per Captive Portal - redirige tutte le richieste alla pagina di setup
void handleCaptivePortal() {
  Serial.print("Captive portal redirect: ");
  Serial.println(server.uri());

  // Redirect 302 alla pagina di setup
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");
}

void startWiFiManager() {
  apMode = true;

  // Assicura che le nuove connessioni Bluetooth siano disabilitate per WiFi AP mode
  #ifdef ENABLE_BLUEPAD32
  BP32.enableNewBluetoothConnections(false);
  #endif
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  String apSSID = "CONSOLE QUADRA";

  if (!WiFi.softAP(apSSID.c_str())) {
    Serial.println("Failed to start AP mode!");
    return;
  }

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(53, "*", apIP);

  server.on("/", handleWiFiSetup);
  server.on("/connect", handleWiFiConnect);
  server.on("/resetwifi", handleReset);
  server.on("/wifiscan", []() {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);  // Avvia scan asincrono
    server.send(200, "text/plain", "Scan started");
  });

  // Endpoint Captive Portal per vari sistemi operativi
  server.on("/generate_204", handleCaptivePortal);        // Android
  server.on("/gen_204", handleCaptivePortal);             // Android
  server.on("/hotspot-detect.html", handleCaptivePortal); // iOS/macOS
  server.on("/library/test/success.html", handleCaptivePortal); // iOS
  server.on("/ncsi.txt", handleCaptivePortal);            // Windows
  server.on("/connecttest.txt", handleCaptivePortal);     // Windows
  server.on("/fwlink", handleCaptivePortal);              // Windows

  // Handler per tutte le altre richieste sconosciute
  server.onNotFound(handleCaptivePortal);

  server.begin();

  Serial.println("WiFi Manager started");
  Serial.print("Connect to: ");
  Serial.println(apSSID);
  Serial.println("Go to: 192.168.4.1");

  // Avvia scansione WiFi asincrona in background (non bloccante)
  WiFi.scanNetworks(true);  // true = async
  wifiScanInProgress = true;
  cachedNetworksCount = -1;
  Serial.println("Started async WiFi scan in background...");

  changeState(STATE_WIFI_SETUP);
  displayWiFiSetupMode();
}

void handleWiFiSetup() {
  // Usa risultati scan asincrono (avviato in startWiFiManager)
  int networksFound = WiFi.scanComplete();

  // Se scan in corso, mostra pagina di attesa veloce
  if (networksFound == WIFI_SCAN_RUNNING) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='1'>"; // Auto-refresh ogni 1 secondo
    html += "<title>CONSOLE QUADRA - Scanning...</title>";
    html += "<style>body{font-family:Arial;text-align:center;background:#000;color:#fff;padding:50px;}";
    html += ".spinner{border:4px solid #333;border-top:4px solid #4CAF50;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:20px auto;}";
    html += "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body>";
    html += "<h1>CONSOLE QUADRA</h1>";
    html += "<div class='spinner'></div>";
    html += "<p>Ricerca reti WiFi in corso...</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    return;
  }

  // Se scan fallito o non iniziato, avvia nuovo scan asincrono e mostra attesa
  if (networksFound == WIFI_SCAN_FAILED || networksFound < 0) {
    WiFi.scanNetworks(true);  // Avvia nuovo scan asincrono
    // Mostra pagina di attesa come per scan in corso
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='1'>";
    html += "<title>CONSOLE QUADRA - Scanning...</title>";
    html += "<style>body{font-family:Arial;text-align:center;background:#000;color:#fff;padding:50px;}";
    html += ".spinner{border:4px solid #333;border-top:4px solid #4CAF50;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:20px auto;}";
    html += "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body>";
    html += "<h1>CONSOLE QUADRA</h1>";
    html += "<div class='spinner'></div>";
    html += "<p>Ricerca reti WiFi in corso...</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    return;
  }

  Serial.print("Networks found: ");
  Serial.println(networksFound);

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>CONSOLE QUADRA - WiFi Setup</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;text-align:center;margin:20px;background:#000;color:#fff;}";
  html += ".container{background:#1a1a1a;padding:30px;border-radius:10px;max-width:500px;margin:0 auto;box-shadow:0 4px 20px rgba(255,255,255,0.1);border:1px solid #333;}";
  html += ".network-list{margin:20px 0;max-height:300px;overflow-y:auto;border:1px solid #333;border-radius:5px;background:#0a0a0a;}";
  html += ".network-item{padding:15px;border-bottom:1px solid #222;cursor:pointer;transition:background 0.3s;display:flex;justify-content:space-between;align-items:center;}";
  html += ".network-item:hover{background:#2a2a2a;}";
  html += ".network-item.selected{background:#1e4620;border-left:4px solid #4CAF50;}";
  html += ".network-name{font-size:16px;font-weight:bold;flex-grow:1;text-align:left;}";
  html += ".network-signal{font-size:12px;color:#888;margin-left:10px;}";
  html += ".network-security{font-size:12px;color:#ff9800;margin-left:10px;}";
  html += "input{padding:12px;margin:10px 0;width:100%;box-sizing:border-box;border:1px solid #444;border-radius:5px;font-size:16px;background:#222;color:#fff;}";
  html += "button{padding:14px 30px;background:#4CAF50;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;margin:10px 5px;transition:background 0.3s;}";
  html += "button:hover{background:#45a049;}";
  html += "button.secondary{background:#666;}";
  html += "button.secondary:hover{background:#555;}";
  html += "h1{background:linear-gradient(90deg,#ff0000,#ff7f00,#ffff00,#00ff00,#0000ff,#4b0082,#9400d3);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;margin-bottom:10px;}";
  html += "h2{color:#fff;margin:10px 0;}";
  html += "p{color:#aaa;}";
  html += ".signal-icon{display:inline-block;width:20px;height:20px;margin-right:8px;}";
  html += ".manual-entry{margin-top:20px;padding-top:20px;border-top:1px solid #333;}";
  html += ".show-password{display:flex;align-items:center;justify-content:flex-start;margin:5px 0 10px 0;font-size:14px;color:#aaa;cursor:pointer;}";
  html += ".show-password input{width:auto;margin:0 8px 0 0;cursor:pointer;}";
  html += "</style>";
  html += "<script>";
  html += "let selectedSSID='';";
  html += "function selectNetwork(ssid,secured){";
  html += "  selectedSSID=ssid;";
  html += "  document.getElementById('ssid').value=ssid;";
  html += "  document.querySelectorAll('.network-item').forEach(el=>el.classList.remove('selected'));";
  html += "  event.target.closest('.network-item').classList.add('selected');";
  html += "  if(!secured){document.getElementById('password').value='';}";
  html += "  document.getElementById('password').focus();";
  html += "}";
  html += "function rescan(){fetch('/wifiscan').then(()=>setTimeout(()=>location.reload(),2000));}";
  html += "function togglePassword(){var p=document.getElementById('password');p.type=p.type==='password'?'text':'password';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>CONSOLE QUADRA</h1>";
  html += "<h2>WiFi Setup</h2>";

  // Lista reti disponibili
  if (networksFound > 0) {
    html += "<p style='color:#4CAF50;'>Trovate " + String(networksFound) + " reti disponibili</p>";
    html += "<div class='network-list'>";
    for (int i = 0; i < networksFound && i < 15; i++) {  // Limite a 15 reti per non sovraccaricare
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      bool isSecured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

      // Icona segnale basata su RSSI
      String signalIcon = "📶";
      if (rssi > -50) signalIcon = "📶"; // Eccellente
      else if (rssi > -60) signalIcon = "📡"; // Buono
      else if (rssi > -70) signalIcon = "📉"; // Medio
      else signalIcon = "⚠️"; // Debole

      html += "<div class='network-item' onclick='selectNetwork(\"" + ssid + "\"," + String(isSecured) + ")'>";
      html += "<span class='network-name'>" + signalIcon + " " + ssid + "</span>";
      html += "<span class='network-signal'>" + String(rssi) + " dBm</span>";
      if (isSecured) {
        html += "<span class='network-security'>🔒</span>";
      }
      html += "</div>";
    }
    html += "</div>";
  } else {
    html += "<p style='color:#ff9800;'>Nessuna rete trovata. Riprova la scansione.</p>";
  }

  html += "<form action='/connect' method='post'>";
  html += "<div class='manual-entry'>";
  html += "<p style='font-size:14px;'>Seleziona una rete sopra o inserisci manualmente:</p>";
  html += "<input type='text' id='ssid' name='ssid' placeholder='SSID (Nome Rete WiFi)' required>";
  html += "<input type='password' id='password' name='password' placeholder='Password WiFi (lasciare vuoto se aperta)'>";
  html += "<label class='show-password'><input type='checkbox' onclick='togglePassword()'>Mostra password</label>";
  html += "<button type='submit'>Connetti</button>";
  html += "<button type='button' class='secondary' onclick='rescan()'>Ricarica Reti</button>";
  html += "</div>";
  html += "</form>";
  html += "<p style='margin-top:20px;color:#aaa;font-size:12px;'>Dopo la connessione, il dispositivo si riavvierà.</p>";

  // Mostra pulsante Reset WiFi solo se ci sono credenziali salvate
  WiFiConfig checkConfig;
  EEPROM.get(0, checkConfig);
  if (checkConfig.magicNumber == 0xCAFE && strlen(checkConfig.ssid) > 0) {
    html += "<div style='margin-top:20px;padding-top:20px;border-top:1px solid #333;'>";
    html += "<p style='color:#ff9800;font-size:13px;'>Hai già una rete configurata: <strong>" + String(checkConfig.ssid) + "</strong></p>";
    html += "<button type='button' style='background:#d32f2f;' onclick='if(confirm(\"Sei sicuro di voler cancellare la configurazione WiFi?\"))window.location=\"/resetwifi\"'>🗑️ Reset WiFi</button>";
    html += "</div>";
  }
  html += "</div></body></html>";

  server.send(200, "text/html", html);

  // Pulizia scansione
  WiFi.scanDelete();
}

void handleWiFiConnect() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    // Leggi la configurazione esistente per preservare le altre impostazioni
    WiFiConfig config;
    EEPROM.get(0, config);

    // Se la configurazione non è valida, inizializza con i default
    if (config.magicNumber != 0xCAFE) {
      memset(&config, 0, sizeof(config));
      config.brightness = 50;
      config.clockColorMode = 1;
      config.secondsLedColorMode = 0;
      config.clockDisplayType = 0;
      config.matrixLedEnabled = true;
      strcpy(config.clockTimezone, "CET-1CEST,M3.5.0,M10.5.0/3");
    }

    String ssid = server.arg("ssid");
    String password = server.arg("password");

    // Aggiorna solo SSID e password
    memset(config.ssid, 0, sizeof(config.ssid));
    memset(config.password, 0, sizeof(config.password));
    ssid.toCharArray(config.ssid, sizeof(config.ssid));
    password.toCharArray(config.password, sizeof(config.password));

    // Imposta il magic number per indicare configurazione valida
    config.magicNumber = 0xCAFE;

    Serial.print("Saving SSID: ");
    Serial.println(config.ssid);
    Serial.print("Password length: ");
    Serial.println(strlen(config.password));

    EEPROM.put(0, config);
    if (EEPROM.commit()) {
      Serial.println("WiFi configuration saved to EEPROM");
    } else {
      Serial.println("Error saving to EEPROM");
    }

    String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;text-align:center;padding:50px;background:#4CAF50;color:white;}";
  html += ".message{background:white;color:#333;padding:30px;border-radius:10px;display:inline-block;}";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='5;url=/'></head><body>";
  html += "<div class='message'>";
  html += "<h1>Configurazione Salvata!</h1>";
  html += "<p>Connessione a: " + ssid + "</p>";
  html += "<p>Riavvio in corso...</p>";
  html += "<p>Tra 5 secondi sarai reindirizzato.</p>";
  html += "</div></body></html>";

    server.send(200, "text/html", html);

    delay(3000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "Parametri mancanti");
  }
}

// ============================================
// WEB SERVER & INTERFACCIA WEB
// ============================================
void startWebServer() {
  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/tris", handleTris);
  server.on("/trisMove", handleTrisMove);
  server.on("/trisScore", handleTrisScore);
  server.on("/trisResetScore", handleTrisResetScore);
  server.on("/text", handleTextScroll);
  server.on("/updateText", handleUpdateText);
  server.on("/clock", handleClock);
  server.on("/setClock", handleSetClock);
  server.on("/weather", handleWeather);
  server.on("/weatherconfig", handleWeatherConfig);
  server.on("/saveweatherconfig", handleSaveWeatherConfig);
  server.on("/weatherupdate", handleWeatherUpdate);
  server.on("/weathertest", handleWeatherTest);
  server.on("/sensordata", handleSensorData);
  server.on("/brightness", handleBrightness);
  server.on("/reboot", handleReboot);
  server.on("/reset", handleReset);
  server.on("/spaceinvaders", handleSpaceInvaders);
  server.on("/sicontrol", handleSIControl);
  server.on("/pong", handlePong);
  server.on("/pongcontrol", handlePongControl);
  server.on("/snake", handleSnake);
  server.on("/snakecontrol", handleSnakeControl);
  server.on("/breakout", handleBreakout);
  server.on("/breakoutcontrol", handleBreakoutControl);
  server.on("/scoreboard", handleScoreboard);
  server.on("/scoreboardcontrol", handleScoreboardControl);
  server.on("/tetris", handleTetris);
  server.on("/tetriscontrol", handleTetrisControl);
  server.on("/pacman", handlePacman);
  server.on("/pacman_control", handlePacmanControl);
  server.on("/pacman_status", handlePacmanStatus);
  server.on("/simon", handleSimon);
  server.on("/simoncontrol", handleSimonControl);
  server.on("/zuma", handleZuma);
  server.on("/zumacontrol", handleZumaControl);
  server.on("/stopwatch", handleStopwatch);
  server.on("/stopwatchcontrol", handleStopwatchControl);
  server.on("/timer", handleTimer);
  server.on("/timercontrol", handleTimerControl);
  server.on("/alarm", handleAlarm);
  server.on("/setalarm", handleSetAlarm);
  server.on("/stopalarm", handleStopAlarm);
  server.on("/testalarm", handleTestAlarm);

  // Calendario Eventi
  server.on("/calendar", handleCalendar);
  server.on("/calendarsave", handleCalendarSave);
  server.on("/calendarupdate", handleCalendarUpdate);
  server.on("/calendardelete", handleCalendarDelete);
  server.on("/calendarlist", handleCalendarList);

  // Cronotermostato
  server.on("/thermostat", handleThermostat);
  server.on("/thermostatconfig", handleThermostatConfig);
  server.on("/thermostatschedule", handleThermostatSchedule);
  server.on("/setthermostat", handleSetThermostat);
  server.on("/thermostatstatus", handleThermostatStatus);

  // Bluetooth mode (per giochi con controller)
  server.on("/enableBluetooth", handleEnableBluetooth);
  server.on("/btpairing", handleBluetoothPairing);
  server.on("/btpairingstart", handleBluetoothPairingStart);
  server.on("/btpairingstop", handleBluetoothPairingStop);
  server.on("/btpairingstatus", handleBluetoothPairingStatus);
  server.on("/btforget", handleBluetoothForget);

  server.begin();
  Serial.print("Web server started at: http://");
  Serial.println(WiFi.localIP());
}

void handleRoot() {
  // Quando si accede al menu principale, torna all'orologio sulla matrice
  changeState(STATE_GAME_CLOCK);
  forceRedraw = true;

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>CONSOLE QUADRA</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box;}";
  html += "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#0a0a0a 0%,#1a1a2e 50%,#16213e 100%);min-height:100vh;color:#fff;padding:20px;}";
  html += ".container{max-width:900px;margin:0 auto;}";
  html += "@keyframes rainbow{0%{filter:hue-rotate(0deg);}100%{filter:hue-rotate(360deg);}}";
  html += "@keyframes float{0%,100%{transform:translateY(0px);}50%{transform:translateY(-10px);}}";
  html += "@keyframes glow{0%,100%{box-shadow:0 0 20px rgba(33,150,243,0.3);}50%{box-shadow:0 0 40px rgba(33,150,243,0.6);}}";
  html += ".header{text-align:center;margin-bottom:40px;animation:float 3s ease-in-out infinite;}";
  html += ".logo{font-size:3em;font-weight:900;background:linear-gradient(90deg,#ff0844,#ffb199,#00ff88,#00bfff,#8a2be2);background-size:200% 200%;-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;animation:rainbow 3s linear infinite;text-shadow:0 0 30px rgba(255,255,255,0.3);margin-bottom:10px;}";
  html += ".subtitle{font-size:1.1em;color:#aaa;letter-spacing:2px;}";
  html += ".status-card{background:rgba(26,26,46,0.8);backdrop-filter:blur(10px);padding:20px;border-radius:15px;margin-bottom:30px;border:2px solid rgba(33,150,243,0.3);animation:glow 2s ease-in-out infinite;}";
  html += ".status-grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;text-align:left;}";
  html += ".status-item{display:flex;align-items:center;gap:10px;}";
  html += ".status-icon{font-size:1.5em;}";
  html += ".status-label{color:#888;font-size:0.9em;}";
  html += ".status-value{color:#fff;font-weight:bold;font-size:1.1em;}";
  html += ".menu-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:15px;margin-bottom:30px;}";
  html += ".menu-card{background:linear-gradient(135deg,rgba(26,26,46,0.9),rgba(22,33,62,0.9));padding:20px;border-radius:15px;text-decoration:none;color:#fff;transition:all 0.3s ease;border:2px solid transparent;position:relative;overflow:hidden;}";
  html += ".menu-card:before{content:'';position:absolute;top:0;left:-100%;width:100%;height:100%;background:linear-gradient(90deg,transparent,rgba(255,255,255,0.1),transparent);transition:0.5s;}";
  html += ".menu-card:hover:before{left:100%;}";
  html += ".menu-card:hover{transform:translateY(-5px);border-color:rgba(33,150,243,0.5);box-shadow:0 10px 30px rgba(33,150,243,0.3);}";
  html += ".menu-card.games{border-color:rgba(76,175,80,0.3);}";
  html += ".menu-card.games:hover{border-color:rgba(76,175,80,0.5);box-shadow:0 10px 30px rgba(76,175,80,0.3);}";
  html += ".menu-card.tools{border-color:rgba(255,152,0,0.3);}";
  html += ".menu-card.tools:hover{border-color:rgba(255,152,0,0.5);box-shadow:0 10px 30px rgba(255,152,0,0.3);}";
  html += ".menu-card.danger{border-color:rgba(244,67,54,0.3);}";
  html += ".menu-card.danger:hover{border-color:rgba(244,67,54,0.5);box-shadow:0 10px 30px rgba(244,67,54,0.3);}";
  html += ".menu-icon{font-size:2.5em;margin-bottom:10px;display:block;}";
  html += ".menu-title{font-size:1.2em;font-weight:bold;margin-bottom:5px;}";
  html += ".menu-desc{font-size:0.85em;color:#aaa;}";
  html += ".footer{text-align:center;margin-top:40px;padding:20px;color:#666;font-size:0.9em;}";
  html += ".badge{display:inline-block;padding:5px 10px;background:rgba(33,150,243,0.2);border-radius:5px;font-size:0.85em;margin-top:10px;color:#2196F3;}";
  html += ".pacman-icon{position:relative;display:inline-block;width:60px;height:60px;background:#FFD700;border-radius:50%;margin:0 auto;}";
  html += ".pacman-icon:before{content:'';position:absolute;width:0;height:0;border-right:25px solid rgba(26,26,46,0.9);border-top:15px solid transparent;border-bottom:15px solid transparent;top:50%;right:0;transform:translateY(-50%);}";
  html += ".pacman-icon:after{content:'';position:absolute;width:6px;height:6px;background:#000;border-radius:50%;top:18px;left:20px;}";
  html += ".simon-icon{position:relative;display:inline-block;width:60px;height:60px;border-radius:50%;margin:0 auto;";
  html += "background:conic-gradient(from 270deg,#e74c3c 0deg 90deg,#2ecc71 90deg 180deg,#f1c40f 180deg 270deg,#3498db 270deg 360deg);";
  html += "box-shadow:inset 0 0 0 3px rgba(26,26,46,0.9);}";
  html += ".simon-icon:before{content:'';position:absolute;width:10px;height:10px;background:rgba(26,26,46,0.9);border-radius:50%;top:50%;left:50%;transform:translate(-50%,-50%);z-index:2;}";
  html += ".simon-icon:after{content:'';position:absolute;width:100%;height:100%;top:0;left:0;";
  html += "background:linear-gradient(rgba(26,26,46,0.9),rgba(26,26,46,0.9))center/3px 100% no-repeat,linear-gradient(rgba(26,26,46,0.9),rgba(26,26,46,0.9))center/100% 3px no-repeat;}";
  html += "@media(max-width:768px){.logo{font-size:2em;}.menu-grid{grid-template-columns:1fr;}}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "<div class='logo'>CONSOLE QUADRA</div>";
  html += "<div class='subtitle'>🎮 RETRO GAMING MATRIX 16x16 LED 🎮</div>";
  html += "</div>";

  if (wifiConnected) {
    html += "<div class='status-card'>";
    html += "<div class='status-grid'>";
    html += "<div class='status-item'><span class='status-icon'>📡</span><div><div class='status-label'>WiFi</div><div class='status-value'>" + String(WiFi.SSID()) + "</div></div></div>";
    html += "<div class='status-item'><span class='status-icon'>🌐</span><div><div class='status-label'>IP Address</div><div class='status-value'>" + WiFi.localIP().toString() + "</div></div></div>";
    html += "<div class='status-item'><span class='status-icon'>💪</span><div><div class='status-label'>Signal</div><div class='status-value'>" + String(WiFi.RSSI()) + " dBm</div></div></div>";
    html += "<div class='status-item'><span class='status-icon'>⚡</span><div><div class='status-label'>Status</div><div class='status-value' style='color:#4CAF50;'>Online</div></div></div>";
    html += "</div></div>";
  }

  // Sensore HTU21 - Temperatura/Umidità
  html += "<div class='status-card'>";
  html += "<div class='status-grid'>";
  html += "<div class='status-item'><span class='status-icon'>🌡️</span><div><div class='status-label'>Temperatura</div><div class='status-value' id='temperature'>";
  if (sensorAvailable) {
    html += String(currentTemperature, 1) + " °C";
  } else {
    html += "N/A";
  }
  html += "</div></div></div>";
  html += "<div class='status-item'><span class='status-icon'>💧</span><div><div class='status-label'>Umidità</div><div class='status-value' id='humidity'>";
  if (sensorAvailable) {
    html += String(currentHumidity, 1) + " %";
  } else {
    html += "N/A";
  }
  html += "</div></div></div>";
  html += "</div></div>";

  html += "<div class='menu-grid'>";
  html += "<a href='/tris' class='menu-card games'><span class='menu-icon'>❌⭕</span><div class='menu-title'>TRIS</div><div class='menu-desc'>Sfida l'AI in questo classico gioco</div><span class='badge'>2 Players</span></a>";
  html += "<a href='/spaceinvaders' class='menu-card games'><span class='menu-icon'>👾</span><div class='menu-title'>Space Invaders</div><div class='menu-desc'>Difendi la Terra dagli invasori</div><span class='badge'>Arcade</span></a>";
  html += "<a href='/pong' class='menu-card games'><span class='menu-icon'>🏓</span><div class='menu-title'>Pong</div><div class='menu-desc'>Il primo videogioco della storia</div><span class='badge'>Classic</span></a>";
  html += "<a href='/snake' class='menu-card games'><span class='menu-icon'>🐍</span><div class='menu-title'>Snake</div><div class='menu-desc'>Mangia, cresci e non colpire i bordi</div><span class='badge'>Retro</span></a>";
  html += "<a href='/breakout' class='menu-card games'><span class='menu-icon'>🧱</span><div class='menu-title'>Breakout</div><div class='menu-desc'>Distruggi tutti i mattoni</div><span class='badge'>Action</span></a>";
  html += "<a href='/tetris' class='menu-card games'><span class='menu-icon'>🧩</span><div class='menu-title'>Tetris</div><div class='menu-desc'>Il puzzle più iconico di sempre</div><span class='badge'>Puzzle</span></a>";
  html += "<a href='/pacman' class='menu-card games'><span class='menu-icon'><div class='pacman-icon'></div></span><div class='menu-title'>Pac-Man</div><div class='menu-desc'>Mangia i puntini e scappa dai fantasmi</div><span class='badge'>Arcade</span></a>";
  html += "<a href='/simon' class='menu-card games'><span class='menu-icon'><div class='simon-icon'></div></span><div class='menu-title'>Simon Says</div><div class='menu-desc'>Memorizza e ripeti la sequenza di colori</div><span class='badge'>Memory</span></a>";
  html += "<a href='/zuma' class='menu-card games'><span class='menu-icon'>🐸</span><div class='menu-title'>ZUMA</div><div class='menu-desc'>La Rana Azteca - Spara e abbina i colori!</div><span class='badge'>Aztec</span></a>";
  html += "<a href='/scoreboard' class='menu-card tools'><span class='menu-icon'>🏆</span><div class='menu-title'>Segnapunti</div><div class='menu-desc'>Tracker punteggio per 4 giocatori</div><span class='badge'>Sport</span></a>";
  html += "<a href='/stopwatch' class='menu-card tools'><span class='menu-icon'>⏱️</span><div class='menu-title'>Cronometro</div><div class='menu-desc'>Misura il tempo con precisione</div><span class='badge'>Timer</span></a>";
  html += "<a href='/timer' class='menu-card tools'><span class='menu-icon'>⏲️</span><div class='menu-title'>Timer</div><div class='menu-desc'>Conto alla rovescia personalizzato</div><span class='badge'>Countdown</span></a>";
  html += "<a href='/alarm' class='menu-card tools'><span class='menu-icon'>⏰</span><div class='menu-title'>Sveglia</div><div class='menu-desc'>12 suonerie personalizzate e ripetizioni</div><span class='badge'>Alarm</span></a>";
  html += "<a href='/calendar' class='menu-card tools'><span class='menu-icon'>📅</span><div class='menu-title'>Calendario</div><div class='menu-desc'>Crea eventi con notifiche sulla matrice</div><span class='badge'>Events</span></a>";
  html += "<a href='/thermostat' class='menu-card tools'><span class='menu-icon'>🔥</span><div class='menu-title'>Cronotermostato</div><div class='menu-desc'>Controlla la caldaia con Shelly</div><span class='badge'>Smart Home</span></a>";
  html += "<a href='/clock' class='menu-card tools'><span class='menu-icon'>🕐</span><div class='menu-title'>Orologio Digitale</div><div class='menu-desc'>Display orologio con NTP sync</div><span class='badge'>Smart Home</span></a>";
  html += "<a href='/weather' class='menu-card tools'><span class='menu-icon'>🌤️</span><div class='menu-title'>Stazione Meteo</div><div class='menu-desc'>Meteo in tempo reale con Open-Meteo</div><span class='badge'>IoT</span></a>";
  html += "<a href='/text' class='menu-card tools'><span class='menu-icon'>📜</span><div class='menu-title'>Testo Scorrevole</div><div class='menu-desc'>Messaggi personalizzati animati</div><span class='badge'>Custom</span></a>";
  html += "<a href='/brightness' class='menu-card tools'><span class='menu-icon'>💡</span><div class='menu-title'>Luminosità</div><div class='menu-desc'>Controlla l'intensità LED</div><span class='badge'>Settings</span></a>";
  html += "<a href='/btpairing' class='menu-card tools' style='border-color:rgba(33,150,243,0.5);'><span class='menu-icon'>🎮</span><div class='menu-title'>Bluetooth Pairing</div><div class='menu-desc'>Associa controller Bluetooth</div><span class='badge'>Gamepad</span></a>";
  html += "<a href='#' onclick='rebootDevice()' class='menu-card danger'><span class='menu-icon'>🔌</span><div class='menu-title'>Riavvia</div><div class='menu-desc'>Reboot del dispositivo</div><span class='badge'>System</span></a>";
  html += "<a href='/reset' class='menu-card danger'><span class='menu-icon'>🔄</span><div class='menu-title'>Reset WiFi</div><div class='menu-desc'>Cancella configurazione WiFi</div><span class='badge'>Advanced</span></a>";
  html += "</div>";

  html += "<div class='footer'>";
  html += "<p>💡 <strong>Controlli fisici:</strong> Tieni premuti entrambi i pulsanti posteriori per 5 secondi per resettare il WIFI</p>";
  html += "<p style='margin-top:10px;'>⚙️ PULSANTE MODO: CAMBIA GLI OROLOGI | PULSANTE SEC: CAMBIA LUMINOSITA'</p>";
  html += "<p style='margin-top:20px;opacity:0.5;'>Console Quadra v1.0 - ESP32 Powered</p>";
  html += "<p style='text-align:center;opacity:0.6;margin-top:5px;font-size:0.9em;'>by Marco Prunca and Davide Gatti SH</p>";
  html += "</div>";

  html += "<script>";
  html += "function rebootDevice(){";
  html += "fetch('/reboot').then(()=>{";
  html += "setTimeout(()=>{location.reload();},10000);";
  html += "});";
  html += "}";
  html += "function updateSensorData(){";
  html += "fetch('/sensordata').then(r=>r.json()).then(d=>{";
  html += "if(d.available){";
  html += "document.getElementById('temperature').innerHTML=d.temperature.toFixed(1)+' °C';";
  html += "document.getElementById('humidity').innerHTML=d.humidity.toFixed(1)+' %';";
  html += "}else{";
  html += "document.getElementById('temperature').innerHTML='N/A';";
  html += "document.getElementById('humidity').innerHTML='N/A';";
  html += "}";
  html += "}).catch(e=>console.error('Error fetching sensor data:',e));";
  html += "}";
  html += "setInterval(updateSensorData,5000);";
  html += "</script>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleControl() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Game Pad Virtuale</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:white;}";
  html += ".container{display:flex;flex-wrap:wrap;justify-content:center;padding:20px;}";
  html += ".d-pad{display:grid;grid-template-columns:repeat(3,100px);gap:10px;margin:20px;}";
  html += ".d-pad button{height:100px;font-size:32px;border:none;border-radius:10px;background:#444;color:white;cursor:pointer;}";
  html += ".d-pad button:hover{background:#555;}";
  html += ".d-pad button:active{background:#666;}";
  html += ".action-buttons{display:flex;flex-direction:column;margin:20px;}";
  html += ".action-buttons button{padding:30px;margin:10px;font-size:24px;border:none;border-radius:10px;color:white;cursor:pointer;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "</style>";
  html += "<script>";
  html += "function sendCommand(cmd){";
  html += "fetch('/control?cmd='+cmd, {method:'GET'});";
  html += "console.log('Comando inviato: '+cmd);";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<h1>Game Pad Virtuale</h1>";
  html += "<p>Usa i comandi per controllare il dispositivo</p>";
  html += "<div class='container'>";
  html += "<div class='d-pad'>";
  html += "<div></div><button onclick='sendCommand(\"up\")'>⬆️</button><div></div>";
  html += "<button onclick='sendCommand(\"left\")'>⬅️</button>";
  html += "<button onclick='sendCommand(\"center\")'>⏺️</button>";
  html += "<button onclick='sendCommand(\"right\")'>➡️</button>";
  html += "<div></div><button onclick='sendCommand(\"down\")'>⬇️</button><div></div>";
  html += "</div>";
  html += "<div class='action-buttons'>";
  html += "<button onclick='sendCommand(\"A\")' style='background:#FF5722;'>A</button>";
  html += "<button onclick='sendCommand(\"B\")' style='background:#2196F3;'>B</button>";
  html += "<button onclick='sendCommand(\"start\")' style='background:#4CAF50;'>START</button>";
  html += "<button onclick='sendCommand(\"select\")' style='background:#9C27B0;'>SELECT</button>";
  html += "</div>";
  html += "</div>";
  html += "<p style='margin-top:30px;color:#aaa;'>I comandi verranno implementati in futuro per controllare giochi.</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ============================================
// GIOCO TRIS (TIC-TAC-TOE)
// ============================================
void handleTris() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Tris</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;}";
  html += ".container{max-width:500px;margin:0 auto;padding:20px;}";
  html += ".mode-select{margin:20px;}";
  html += ".mode-select button{padding:15px 30px;margin:10px;font-size:18px;border:none;border-radius:5px;cursor:pointer;background:#333;color:#fff;}";
  html += ".mode-select button.active{background:#4CAF50;color:white;}";
  html += ".tris-board{display:grid;grid-template-columns:repeat(3,100px);gap:5px;margin:20px auto;justify-content:center;}";
  html += ".tris-cell{width:100px;height:100px;font-size:48px;background:#222;border:2px solid #444;cursor:pointer;color:#fff;}";
  html += ".tris-cell:hover{background:#333;}";
  html += ".tris-cell.X{color:#FF5722;}";
  html += ".tris-cell.O{color:#FFD700;}";
  html += ".status{margin:20px;font-size:24px;font-weight:bold;min-height:40px;color:#fff;}";
  html += ".reset-btn{padding:15px 30px;background:#f44336;color:white;border:none;border-radius:5px;font-size:18px;cursor:pointer;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".score-board{display:flex;justify-content:space-around;margin:20px auto;max-width:400px;background:#1a1a1a;padding:15px;border-radius:10px;border:2px solid #333;}";
  html += ".score-item{text-align:center;flex:1;}";
  html += ".score-label{font-size:14px;color:#aaa;margin-bottom:5px;}";
  html += ".score-value{font-size:28px;font-weight:bold;}";
  html += ".score-x{color:#FF5722;}";
  html += ".score-o{color:#FFD700;}";
  html += ".score-draw{color:#FFC107;}";
  html += "</style>";
  html += "<script>";
  html += "let currentMode = 'ai';";
  html += "function makeMove(cell){";
  html += "if(!document.querySelector('.tris-cell[data-cell=\"'+cell+'\"]').disabled){";
  html += "fetch('/trisMove?cell='+cell).then(r=>r.text()).then(data=>{";
  html += "document.getElementById('status').innerText=data;";
  html += "updateBoard();";
  html += "});";
  html += "}";
  html += "}";
  html += "function updateBoard(){";
  html += "fetch('/trisMove').then(r=>r.text()).then(data=>{";
  html += "let cells=document.querySelectorAll('.tris-cell');";
  html += "for(let i=0;i<9;i++){";
  html += "cells[i].innerText=data[i];";
  html += "cells[i].className='tris-cell ' + (data[i]=='X'?'X':data[i]=='O'?'O':'');";
  html += "cells[i].disabled=data[i]!=' ';";
  html += "}";
  html += "updateScore();";
  html += "});";
  html += "}";
  html += "function updateScore(){";
  html += "fetch('/trisScore').then(r=>r.json()).then(score=>{";
  html += "document.getElementById('score-x').innerText=score.x;";
  html += "document.getElementById('score-o').innerText=score.o;";
  html += "document.getElementById('score-draw').innerText=score.draw;";
  html += "});";
  html += "}";
  html += "function setMode(mode){";
  html += "currentMode=mode;";
  html += "document.querySelectorAll('.mode-select button').forEach(btn=>{";
  html += "btn.classList.remove('active');";
  html += "});";
  html += "document.getElementById('mode-'+mode).classList.add('active');";
  html += "resetGame();";
  html += "}";
  html += "function resetGame(){";
  html += "fetch('/trisMove?reset=1&mode='+currentMode).then(()=>{";
  html += "updateBoard();";
  html += "document.getElementById('status').innerText='Nuova partita iniziata!';";
  html += "});";
  html += "}";
  html += "function resetScore(){";
  html += "fetch('/trisResetScore').then(()=>{";
  html += "updateScore();";
  html += "document.getElementById('status').innerText='Punteggi resettati!';";
  html += "});";
  html += "}";
  html += "document.addEventListener('DOMContentLoaded', function(){";
  html += "updateBoard();";
  html += "currentMode='ai';";
  html += "document.getElementById('mode-ai').classList.add('active');";
  html += "document.querySelectorAll('.tris-cell').forEach(cell=>{";
  html += "cell.addEventListener('touchstart',function(e){e.preventDefault();if(!this.disabled)makeMove(parseInt(this.dataset.cell));},false);";
  html += "});";
  html += "});";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>TRIS (Tic-Tac-Toe)</h1>";
  html += "<div class='score-board'>";
  html += "<div class='score-item'>";
  html += "<div class='score-label'>Vittorie X</div>";
  html += "<div class='score-value score-x' id='score-x'>0</div>";
  html += "</div>";
  html += "<div class='score-item'>";
  html += "<div class='score-label'>Pareggi</div>";
  html += "<div class='score-value score-draw' id='score-draw'>0</div>";
  html += "</div>";
  html += "<div class='score-item'>";
  html += "<div class='score-label'>Vittorie O</div>";
  html += "<div class='score-value score-o' id='score-o'>0</div>";
  html += "</div>";
  html += "</div>";
  html += "<div class='mode-select'>";
  html += "<button id='mode-ai' onclick='setMode(\"ai\")'>🤖 Gioca contro IA</button>";
  html += "<button id='mode-player' onclick='setMode(\"player\")'>👥 Due Giocatori</button>";
  html += "</div>";
  html += "<div class='tris-board'>";
  for (int i = 0; i < 9; i++) {
    html += "<button class='tris-cell' data-cell='" + String(i) + "' onclick='makeMove(" + String(i) + ")'></button>";
  }
  html += "</div>";
  html += "<div class='status' id='status'>Seleziona una modalità</div>";
  html += "<button class='reset-btn' onclick='resetGame()'>🔄 Nuova Partita</button>";
  html += "<button class='reset-btn' onclick='resetScore()' style='background:#FF9800;margin-left:10px;'>🗑️ Reset Punteggio</button>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleTrisMove() {
  if (server.hasArg("reset")) {
    if (server.hasArg("mode")) {
      trisVsAI = (server.arg("mode") == "ai");
    }
    resetTrisGame();
    changeState(STATE_GAME_TRIS);
    server.send(200, "text/plain", "Nuova partita iniziata!");
    return;
  }

  if (server.hasArg("cell") && trisGameActive) {
    int cell = server.arg("cell").toInt();

    if (cell >= 0 && cell < 9 && trisBoard[cell] == ' ') {
      // In modalità 2 giocatori, alterna tra X e O
      char currentPlayer = trisVsAI ? 'X' : (trisPlayerTurn ? 'X' : 'O');
      trisBoard[cell] = currentPlayer;
      playBeep(); // Suono mossa

      // Controlla se il giocatore corrente ha vinto
      if (checkTrisWinner(currentPlayer)) {
        trisGameActive = false;
        playSuccess(); // Suono vittoria
        if (currentPlayer == 'X') {
          trisScoreX++;
          server.send(200, "text/plain", trisVsAI ? "🎉 Hai vinto!" : "🎉 Vince X!");
        } else {
          trisScoreO++;
          server.send(200, "text/plain", "🎉 Vince O!");
        }
        drawTrisOnMatrix();
        return;
      }

      if (isTrisBoardFull()) {
        trisGameActive = false;
        trisScoreDraw++;
        playBeep(); // Suono pareggio
        drawTrisOnMatrix();
        server.send(200, "text/plain", "🤝 Pareggio!");
        return;
      }

      if (trisVsAI) {
        makeAIMove();

        if (checkTrisWinner('O')) {
          trisGameActive = false;
          trisScoreO++;
          playError(); // Suono sconfitta contro IA
          drawTrisOnMatrix();
          server.send(200, "text/plain", "🤖 IA vince!");
          return;
        }

        if (isTrisBoardFull()) {
          trisGameActive = false;
          trisScoreDraw++;
          drawTrisOnMatrix();
          server.send(200, "text/plain", "🤝 Pareggio!");
          return;
        }

        drawTrisOnMatrix();
        server.send(200, "text/plain", "✅ Mossa effettuata! Tocca a te!");
      } else {
        trisPlayerTurn = !trisPlayerTurn;
        drawTrisOnMatrix();
        server.send(200, "text/plain", trisPlayerTurn ? "Turno: ❌ (X)" : "Turno: ⭕ (O)");
      }
      return;
    }
  }

  String boardStr = "";
  for (int i = 0; i < 9; i++) {
    boardStr += String(trisBoard[i]);
  }
  server.send(200, "text/plain", boardStr);
}

void handleTrisScore() {
  String json = "{\"x\":" + String(trisScoreX) + ",\"o\":" + String(trisScoreO) + ",\"draw\":" + String(trisScoreDraw) + "}";
  server.send(200, "application/json", json);
}

void handleTrisResetScore() {
  // Resetta tutti i punteggi a zero
  trisScoreX = 0;
  trisScoreO = 0;
  trisScoreDraw = 0;
  server.send(200, "text/plain", "Punteggi resettati!");
}

void resetTrisGame() {
  for (int i = 0; i < 9; i++) {
    trisBoard[i] = ' ';
  }
  trisGameActive = true;
  trisPlayerTurn = true;
  trisWinner = ' ';
  drawTrisOnMatrix();
}

bool checkTrisWinner(char player) {
  for (int i = 0; i < 3; i++) {
    if (trisBoard[i*3] == player && trisBoard[i*3+1] == player && trisBoard[i*3+2] == player) {
      return true;
    }
  }

  for (int i = 0; i < 3; i++) {
    if (trisBoard[i] == player && trisBoard[i+3] == player && trisBoard[i+6] == player) {
      return true;
    }
  }

  if (trisBoard[0] == player && trisBoard[4] == player && trisBoard[8] == player) {
    return true;
  }
  if (trisBoard[2] == player && trisBoard[4] == player && trisBoard[6] == player) {
    return true;
  }

  return false;
}

bool isTrisBoardFull() {
  for (int i = 0; i < 9; i++) {
    if (trisBoard[i] == ' ') {
      return false;
    }
  }
  return true;
}

// Algoritmo Minimax per AI imbattibile
int evaluateBoard() {
  // +10 se l'AI vince, -10 se il giocatore vince, 0 altrimenti
  if (checkTrisWinner('O')) return 10;
  if (checkTrisWinner('X')) return -10;
  return 0;
}

int minimax(bool isMaximizing, int depth, int alpha, int beta) {
  int score = evaluateBoard();

  // Se qualcuno ha vinto, ritorna il punteggio
  if (score == 10) return score - depth;
  if (score == -10) return score + depth;
  if (isTrisBoardFull()) return 0;

  if (isMaximizing) {
    int bestScore = -1000;
    for (int i = 0; i < 9; i++) {
      if (trisBoard[i] == ' ') {
        trisBoard[i] = 'O';
        int currentScore = minimax(false, depth + 1, alpha, beta);
        trisBoard[i] = ' ';
        bestScore = max(bestScore, currentScore);
        alpha = max(alpha, bestScore);
        if (beta <= alpha) break; // Alpha-beta pruning
      }
    }
    return bestScore;
  } else {
    int bestScore = 1000;
    for (int i = 0; i < 9; i++) {
      if (trisBoard[i] == ' ') {
        trisBoard[i] = 'X';
        int currentScore = minimax(true, depth + 1, alpha, beta);
        trisBoard[i] = ' ';
        bestScore = min(bestScore, currentScore);
        beta = min(beta, bestScore);
        if (beta <= alpha) break; // Alpha-beta pruning
      }
    }
    return bestScore;
  }
}

void makeAIMove() {
  int bestScore = -1000;
  int bestMove = -1;

  // Prima mossa: se il board è vuoto, gioca in un angolo casuale per varietà
  int emptyCount = 0;
  for (int i = 0; i < 9; i++) {
    if (trisBoard[i] == ' ') emptyCount++;
  }

  if (emptyCount == 9) {
    // Prima mossa: scegli un angolo casuale o il centro
    int firstMoves[] = {0, 2, 4, 6, 8};
    bestMove = firstMoves[random(0, 5)];
  } else {
    // Usa minimax per trovare la mossa migliore
    for (int i = 0; i < 9; i++) {
      if (trisBoard[i] == ' ') {
        trisBoard[i] = 'O';
        int moveScore = minimax(false, 0, -1000, 1000);
        trisBoard[i] = ' ';

        if (moveScore > bestScore) {
          bestScore = moveScore;
          bestMove = i;
        }
      }
    }
  }

  if (bestMove != -1) {
    trisBoard[bestMove] = 'O';
  }
}

void drawTrisOnMatrix() {
  clearMatrixNoShow();

  CRGB gridColor = CRGB(100, 100, 100); // Aumentata luminosità (era 40)
  CRGB xColor = CRGB(255, 50, 50);
  CRGB oColor = CRGB(255, 255, 0); // Giallo

  for (int x = 3; x < 13; x++) {
    setPixel(x, 5, gridColor);
    setPixel(x, 10, gridColor);
  }
  for (int y = 3; y < 13; y++) {
    setPixel(5, y, gridColor);
    setPixel(10, y, gridColor);
  }

  for (int i = 0; i < 9; i++) {
    int row = i / 3;
    int col = i % 3;

    int centerX = col * 5 + 2;
    int centerY = row * 5 + 2;

    if (trisBoard[i] == 'X') {
      // X grande e ben definita (3x3)
      // Diagonale principale
      setPixel(centerX - 1, centerY - 1, xColor);
      setPixel(centerX, centerY, xColor);
      setPixel(centerX + 1, centerY + 1, xColor);
      // Diagonale secondaria
      setPixel(centerX + 1, centerY - 1, xColor);
      setPixel(centerX, centerY, xColor);
      setPixel(centerX - 1, centerY + 1, xColor);
    } else if (trisBoard[i] == 'O') {
      // O grande e ben definita (quadrato 3x3 senza centro)
      setPixel(centerX - 1, centerY - 1, oColor);
      setPixel(centerX, centerY - 1, oColor);
      setPixel(centerX + 1, centerY - 1, oColor);
      setPixel(centerX - 1, centerY, oColor);
      setPixel(centerX + 1, centerY, oColor);
      setPixel(centerX - 1, centerY + 1, oColor);
      setPixel(centerX, centerY + 1, oColor);
      setPixel(centerX + 1, centerY + 1, oColor);
    }
  }

  FastLED.show();
}

// ============================================
// SPACE INVADERS
// ============================================
void siInitShields() {
  // Forma degli scudi: rettangolo 3x2 (6 LED)
  // 0 = distrutto, valori 1-3 = integrità (3 = pieno)
  byte shieldPattern[2][3] = {
    {3, 3, 3},
    {3, 3, 3}
  };

  for (int s = 0; s < 4; s++) {
    for (int y = 0; y < 2; y++) {
      for (int x = 0; x < 3; x++) {
        siShields[s][y][x] = shieldPattern[y][x];
      }
    }
  }

  // Scudi laterali: togli i 2 LED verticali esterni per simmetria
  // Scudo 0 (sinistra): togli colonna sinistra
  siShields[0][0][0] = 0;
  siShields[0][1][0] = 0;
  // Scudo 3 (destra): togli colonna destra
  siShields[3][0][2] = 0;
  siShields[3][1][2] = 0;
}

CRGB siGetAlienColor(int row) {
  // 3 colori diversi per le righe di alieni (come arcade)
  if (row == 0) return CRGB(255, 0, 0);      // Rosso - fila superiore
  else if (row == 1) return CRGB(255, 0, 255); // Magenta - seconda fila
  else return CRGB(0, 255, 255);              // Ciano - file inferiori
}

// Reset solo giocatore quando perde una vita (mantiene alieni e scudi)
void resetSpaceInvadersPlayer() {
  // Resetta solo posizione giocatore e proiettili
  siPlayerX = 7;
  siPlayer2X = 7;
  siBulletX = siBulletY = -1;
  siBullet2X = siBullet2Y = -1;

  // Rimuovi tutti i proiettili alieni
  for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
    siAlienBulletX[i] = -1;
    siAlienBulletY[i] = -1;
  }

  siGameActive = true;
  siGameOver = false;
  siLastUpdate = millis();
  siLastBulletUpdate = millis();
  siLastAlienShot = millis();

  // NON resettare alieni, scudi, posizioni alieni - rimangono come erano!
  drawSpaceInvaders();
}

void resetSpaceInvaders(bool fullReset) {
  siPlayerX = 7;
  siPlayer2X = 7;
  siBulletX = siBulletY = -1;
  siBullet2X = siBullet2Y = -1;
  siAlienX = 0;
  siAlienDir = 1;
  siAlienY = 1;
  siDirectionChanges = 0;
  // Solo se è reset completo disabilita il gioco (aspetta START)
  // Se è cambio livello (fullReset=false), mantieni attivo
  if (fullReset) {
    siGameActive = false;  // Non avviare automaticamente - aspetta START
  }
  siGameOver = false;
  siLastUpdate = millis();
  siLastBulletUpdate = millis();
  siLastAlienShot = millis();

  // Inizializza proiettili alieni (tutti inattivi)
  for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
    siAlienBulletX[i] = -1;
    siAlienBulletY[i] = -1;
  }

  // Reset UFO
  siUfoX = -1; // Inattivo
  siLastUfoSpawn = millis();
  siLastUfoUpdate = millis();

  // Reset schermata livello solo se è reset completo
  if (fullReset) {
    siShowingLevel = false;
    siLives = 5;
    siScoreP1 = 0;
    siScoreP2 = 0;
    siLevel = 1; // Ricomincia dal livello 1
  }
  // Se è cambio livello (fullReset=false), NON toccare siShowingLevel

  // Ripristina sempre gli scudi ad ogni nuovo livello (sia reset completo che cambio livello)
  siInitShields();

  // Calcola velocità in base al livello (ogni livello è più veloce)
  // Livello 1: 1100ms, Livello 2: 950ms, Livello 3: 800ms, Livello 4: 650ms, Livello 5: 500ms
  siUpdateSpeed = siBaseSpeed - ((siLevel - 1) * 150);
  if (siUpdateSpeed < 300) siUpdateSpeed = 300; // Velocità minima

  // Inizializza alieni
  // Riga 0: 5 alieni da 2 LED
  // Righe 1-3: 8 alieni da 1 LED
  for (int y = 0; y < 4; y++) {
    int maxAliens = (y == 0) ? 5 : 8;
    for (int x = 0; x < 8; x++) {
      siAliens[x][y] = (x < maxAliens);
    }
  }

  drawSpaceInvaders();
}

void siShoot(int player) {
  if (!siGameActive) return;

  unsigned long currentTime = millis();

  // Controllo cooldown per player 1
  if (player == 1 && siBulletY == -1) {
    if (currentTime - siLastShot >= siShootCooldown) {
      siBulletX = siPlayerX;
      siBulletY = 14;
      siLastShot = currentTime;
      playShoot(); // Suono sparo
    }
  } else if (player == 2 && !siVsAI && siBullet2Y == -1) {
    siBullet2X = siPlayer2X;
    siBullet2Y = 1;
  }
}

void siMovePlayer(int player, int dir) {
  // Permetti movimento anche durante schermata LEVEL (siShowingLevel)
  if (!siGameActive && !siShowingLevel) return;

  if (player == 1) {
    siPlayerX += dir;
    if (siPlayerX < 0) siPlayerX = 0;
    if (siPlayerX > 15) siPlayerX = 15;
    // Non ridisegnare se sta mostrando la schermata LEVEL
    if (!siShowingLevel) {
      drawSpaceInvaders(); // Aggiorna immediatamente per movimento fluido
    }
  } else if (player == 2 && !siVsAI) {
    siPlayer2X += dir;
    if (siPlayer2X < 0) siPlayer2X = 0;
    if (siPlayer2X > 15) siPlayer2X = 15;
    // Non ridisegnare se sta mostrando la schermata LEVEL
    if (!siShowingLevel) {
      drawSpaceInvaders(); // Aggiorna immediatamente per movimento fluido
    }
  }
}

void updateSpaceInvaders() {
  static bool gameOverDrawn = false;

  // Se è GAME OVER, mostra il messaggio SOLO UNA VOLTA e mantienilo visibile
  if (siGameOver) {
    if (!gameOverDrawn) {
      drawGameOver();
      gameOverDrawn = true;
    }
    return;
  } else {
    gameOverDrawn = false; // Reset quando il gioco è attivo
  }

  unsigned long currentTime = millis();

  // Se sta mostrando la schermata LEVEL X, controlla se è finito il tempo
  if (siShowingLevel) {
    if (currentTime - siLevelScreenStart >= siLevelScreenDuration) {
      // Finito il tempo, avvia il nuovo livello
      siShowingLevel = false;
      siGameActive = true;
    }
    // Continua a mostrare la schermata (già disegnata)
    return;
  }

  if (!siGameActive) return;

  // Muovi proiettili più velocemente (separato dal movimento alieni)
  if (currentTime - siLastBulletUpdate >= siBulletSpeed) {
    siLastBulletUpdate = currentTime;

    // Muovi proiettile player 1 (verso l'alto)
    if (siBulletY != -1) {
      siBulletY--;
      if (siBulletY < 0) {
        siBulletY = -1; // Scompare quando esce dalla matrice
      }
    }

    // Muovi proiettile player 2 (verso il basso)
    if (siBullet2Y != -1 && !siVsAI) {
      siBullet2Y++;
      if (siBullet2Y > 15) {
        siBullet2Y = -1; // Scompare quando esce dalla matrice
      }
    }

    // Muovi proiettili alieni (verso il basso)
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
      if (siAlienBulletY[i] != -1) {
        siAlienBulletY[i]++;
        if (siAlienBulletY[i] > 15) {
          siAlienBulletY[i] = -1; // Scompare quando esce dalla matrice
        }
      }
    }

    // Controlla collisioni proiettili con scudi (ogni frame di movimento proiettili)
    for (int s = 0; s < 4; s++) {
      for (int sy = 0; sy < 2; sy++) {
        for (int sx = 0; sx < 3; sx++) {
          if (siShields[s][sy][sx] > 0) {
            int shieldPixelX = siShieldX[s] + sx;
            int shieldPixelY = siShieldY + sy;

            // Proiettile player 1 colpisce scudo
            if (siBulletX == shieldPixelX && siBulletY == shieldPixelY) {
              siShields[s][sy][sx]--; // Deteriora lo scudo
              if (siShields[s][sy][sx] == 0) {
                // Scudo completamente distrutto
              }
              siBulletY = -1;
              drawSpaceInvaders(); // Aggiorna immediatamente
            }

            // Proiettile player 2 colpisce scudo
            if (!siVsAI && siBullet2X == shieldPixelX && siBullet2Y == shieldPixelY) {
              siShields[s][sy][sx]--; // Deteriora lo scudo
              if (siShields[s][sy][sx] == 0) {
                // Scudo completamente distrutto
              }
              siBullet2Y = -1;
              drawSpaceInvaders(); // Aggiorna immediatamente
            }

            // Proiettili alieni colpiscono scudi
            for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
              if (siAlienBulletX[i] == shieldPixelX && siAlienBulletY[i] == shieldPixelY) {
                siShields[s][sy][sx]--; // Deteriora lo scudo
                siAlienBulletY[i] = -1; // Disattiva proiettile
                drawSpaceInvaders();
              }
            }
          }
        }
      }
    }

    // Controlla collisioni proiettili con alieni (ogni frame di movimento proiettili)
    bool hitDetected = false;
    for (int y = 0; y < 4; y++) {
      int maxAliens = (y == 0) ? 5 : 8;
      for (int x = 0; x < maxAliens; x++) {
        if (siAliens[x][y]) {
          int alienX, alienY, alienWidth;

          alienY = siAlienY + y * 2; // Spaziatura verticale 1 LED tra righe

          if (y == 0) {
            // Riga 0: alieni da 2 LED, spaziatura orizzontale 1 LED
            alienX = siAlienX + x * 3;
            alienWidth = 2;
          } else {
            // Righe 1-3: alieni da 1 LED, spaziatura orizzontale 1 LED
            alienX = siAlienX + x * 2;
            alienWidth = 1;
          }

          // Controllo collisione proiettile player 1
          if (siBulletX >= alienX && siBulletX < alienX + alienWidth &&
              siBulletY == alienY) {
            siAliens[x][y] = false;
            siBulletY = -1;
            siScoreP1 += 5;
            hitDetected = true;
            playSIAlienExplosion(); // Suono esplosione alieno arcade
          }

          // Controllo collisione proiettile player 2
          if (!siVsAI && siBullet2X >= alienX && siBullet2X < alienX + alienWidth &&
              siBullet2Y == alienY) {
            siAliens[x][y] = false;
            siBullet2Y = -1;
            siScoreP2 += 5;
            hitDetected = true;
            playSIAlienExplosion(); // Suono esplosione alieno arcade
          }
        }
      }
    }

    // Controlla collisioni proiettili alieni con navicella player 1
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
      if (siAlienBulletY[i] != -1) {
        // Navicella player 1 è in y=14-15, x=siPlayerX-1 a siPlayerX+1
        if (siAlienBulletY[i] >= 14 &&
            siAlienBulletX[i] >= siPlayerX - 1 &&
            siAlienBulletX[i] <= siPlayerX + 1) {
          // Colpito! Perdi una vita
          siLives--;
          showGameHUD(siScoreP1, siLives, siLevel, true);
          tone(BUZZER_PIN, 100, 1000); // Suono grave 1 secondo quando colpito
          siAlienBulletY[i] = -1; // Disattiva proiettile
          playSIPlayerDeath(); // Suono morte giocatore arcade

          if (siLives <= 0) {
            // Game Over
            siGameActive = false;
            siGameOver = true;
            hudOverlayActive = false;  // Disattiva HUD per mostrare GAME OVER
            playSIGameOver(); // Suono game over arcade
            drawGameOver();
            // RIMOSSO delay(3000) - non bloccare il gioco
            return;
          } else {
            // Perdi una vita - mantieni alieni e scudi!
            // Suono già riprodotto sopra
            // RIMOSSO delay(1000) - non bloccare il gioco
            resetSpaceInvadersPlayer(); // Reset solo giocatore
            return;
          }
        }
      }
    }

    // Se c'è stata una collisione o un movimento di proiettile, ridisegna
    bool alienBulletsActive = false;
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
      if (siAlienBulletY[i] != -1) {
        alienBulletsActive = true;
        break;
      }
    }
    // Controlla collisione proiettile con UFO bonus
    if (siUfoX != -1 && siBulletY == 0) { // UFO è sempre in Y=0
      if (siBulletX >= siUfoX && siBulletX < siUfoX + 3) {
        // UFO colpito! +25 punti
        siScoreP1 += 25;
        siUfoX = -1; // Disattiva UFO
        siBulletY = -1; // Consuma proiettile
        playSIUFOHit(); // Suono bonus arcade
        hitDetected = true;
      }
    }

    if (hitDetected || siBulletY != -1 || siBullet2Y != -1 || alienBulletsActive || siUfoX != -1) {
      drawSpaceInvaders();
    }
  }

  // Logica UFO bonus - movimento e spawn
  if (siUfoX != -1) {
    // UFO attivo - muovi
    if (currentTime - siLastUfoUpdate >= siUfoSpeed) {
      siLastUfoUpdate = currentTime;
      siUfoX += siUfoDir;

      // Disattiva UFO se esce dalla matrice
      if (siUfoX < -3 || siUfoX > 16) {
        siUfoX = -1;
      } else {
        drawSpaceInvaders();
      }
    }
  } else {
    // UFO inattivo - controlla se spawn
    if (currentTime - siLastUfoSpawn >= siUfoSpawnInterval) {
      // Spawn casuale (50% chance)
      if (random(0, 2) == 0) {
        siLastUfoSpawn = currentTime;
        // Spawn da sinistra o destra casualmente
        if (random(0, 2) == 0) {
          siUfoX = 0;
          siUfoDir = 1; // Muovi a destra
        } else {
          siUfoX = 13; // Posizione iniziale per muoversi a sinistra (16 - 3 = 13)
          siUfoDir = -1; // Muovi a sinistra
        }
        siLastUfoUpdate = currentTime;
        // UFO silenzioso all'apparizione (suono solo quando colpito)
      } else {
        // Non spawn questa volta, riprova tra poco
        siLastUfoSpawn = currentTime;
      }
    }
  }

  if (currentTime - siLastUpdate < siUpdateSpeed) return;
  siLastUpdate = currentTime;

  // Controlla se ci sono alieni rimanenti
  bool aliensRemaining = false;
  for (int y = 0; y < 4; y++) {
    int maxAliens = (y == 0) ? 5 : 8;
    for (int x = 0; x < maxAliens; x++) {
      if (siAliens[x][y]) {
        aliensRemaining = true;
        break;
      }
    }
    if (aliensRemaining) break;
  }

  if (!aliensRemaining) {
    // Tutti gli alieni sono morti!

    // Controlla se ci sono altri livelli
    if (siLevel < 5) {
      // Passa al livello successivo
      siLevel++;
      showGameHUD(siScoreP1, siLives, siLevel, true);
      playSILevelComplete(); // Suono livello completato arcade

      resetSpaceInvaders(false); // Reset livello, mantieni vite e punteggi

      // Attiva schermata LEVEL X non bloccante (DOPO reset)
      siShowingLevel = true;
      siLevelScreenStart = currentTime;
      siGameActive = false; // Pausa il gioco per mostrare la schermata
      drawLevelComplete(); // Disegna DOPO il reset

      // siGameActive verrà riattivato dopo 2 secondi in updateSpaceInvaders
      return;
    } else {
      // Hai completato tutti i 5 livelli! Vittoria!
      playSILevelComplete(); // Suono vittoria arcade
      clearMatrix();
      // Disegna "WIN!" al centro
      drawCharacter('W', 2, 5, CRGB(0, 255, 0));
      drawCharacter('I', 6, 5, CRGB(0, 255, 0));
      drawCharacter('N', 9, 5, CRGB(0, 255, 0));
      drawCharacter('!', 13, 5, CRGB(0, 255, 0));
      FastLED.show();
      // RIMOSSO delay(3000) - mostra WIN finché non si preme START di nuovo
      siGameActive = false;
      siLevel = 1; // Reset al livello 1 per la prossima partita
      return;
    }
  }

  // Calcola i limiti DINAMICI in base agli alieni VIVI
  int leftmostOffset = 99;
  int rightmostOffset = -1;

  for (int y = 0; y < 4; y++) {
    int maxAliens = (y == 0) ? 5 : 8;
    for (int x = 0; x < maxAliens; x++) {
      if (siAliens[x][y]) {
        int offsetX, width;
        if (y == 0) {
          offsetX = x * 3;  // Riga 0: alieni da 2 LED, spaziatura 1
          width = 2;
        } else {
          offsetX = x * 2;  // Righe 1-3: alieni da 1 LED, spaziatura 1
          width = 1;
        }
        if (offsetX < leftmostOffset) leftmostOffset = offsetX;
        if (offsetX + width > rightmostOffset) rightmostOffset = offsetX + width;
      }
    }
  }

  // Se non ci sono alieni, esci (gestito dalla logica vittoria sopra)
  if (leftmostOffset == 99) return;

  // Posizioni assolute dei bordi della formazione
  int formationLeft = siAlienX + leftmostOffset;
  int formationRight = siAlienX + rightmostOffset - 1;

  // Controlla se il movimento porterebbe fuori dai limiti (0-15)
  bool willHitBorder = false;
  if (siAlienDir > 0 && formationRight + 1 > 15) {
    willHitBorder = true;
  } else if (siAlienDir < 0 && formationLeft - 1 < 0) {
    willHitBorder = true;
  }

  if (willHitBorder) {
    // Tocca il bordo: inverti direzione
    siAlienDir = -siAlienDir;
    siDirectionChanges++;

    // Scendi solo ogni 2 cambi di direzione (ciclo completo dx-sx)
    if (siDirectionChanges >= 2) {
      siAlienY++;
      siDirectionChanges = 0;
    }
  } else {
    // Non tocca: muovi normalmente
    siAlienX += siAlienDir;
  }

  // SUONO MARCIA ALIENI (pom-pom-pom-pom arcade)
  if (soundEnabled) {
    tone(BUZZER_PIN, siAlienMarchNotes[siAlienMarchStep], 100);
    siAlienMarchStep = (siAlienMarchStep + 1) % 4;
  }

  // AI per modalità single player
  if (siVsAI && random(0, 5) == 0) {
    int targetAlienX = -1;
    for (int y = 3; y >= 0 && targetAlienX == -1; y--) {
      int maxAliens = (y == 0) ? 5 : 8;
      for (int x = 0; x < maxAliens; x++) {
        if (siAliens[x][y]) {
          if (y == 0) {
            targetAlienX = siAlienX + x * 3; // Riga 0: spaziatura 1 LED, alieni da 2 LED
          } else {
            targetAlienX = siAlienX + x * 2; // Righe 1-3: spaziatura 1 LED, alieni da 1 LED
          }
          break;
        }
      }
    }

    if (targetAlienX != -1) {
      if (siPlayer2X < targetAlienX) siPlayer2X++;
      else if (siPlayer2X > targetAlienX) siPlayer2X--;

      if (random(0, 3) == 0) siShoot(2);
    }
  }

  // Logica sparo alieni (come nell'arcade originale)
  if (currentTime - siLastAlienShot >= siAlienShootInterval) {
    // Trova un alieno casuale nella riga più in basso che può sparare
    int shootingAlienX = -1;
    int shootingAlienY = -1;

    // Cerca dalla riga più bassa verso l'alto
    for (int y = 3; y >= 0 && shootingAlienX == -1; y--) {
      int maxAliens = (y == 0) ? 5 : 8;
      // Crea lista di alieni disponibili in questa riga
      int availableAliens[8];
      int count = 0;
      for (int x = 0; x < maxAliens; x++) {
        if (siAliens[x][y]) {
          availableAliens[count++] = x;
        }
      }

      // Se ci sono alieni in questa riga, scegline uno casualmente
      if (count > 0) {
        int randomIndex = random(0, count);
        int x = availableAliens[randomIndex];

        if (y == 0) {
          shootingAlienX = siAlienX + x * 3; // Riga 0: alieni da 2 LED
        } else {
          shootingAlienX = siAlienX + x * 2; // Righe 1-3: alieni da 1 LED
        }
        shootingAlienY = siAlienY + y * 2;
        break;
      }
    }

    // Se abbiamo trovato un alieno, fallo sparare
    if (shootingAlienX != -1) {
      // Trova uno slot libero per il proiettile
      for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (siAlienBulletY[i] == -1) {
          siAlienBulletX[i] = shootingAlienX;
          siAlienBulletY[i] = shootingAlienY + 1; // Spara dalla posizione sotto l'alieno
          siLastAlienShot = currentTime;
          break;
        }
      }
    }
  }

  // Controlla collisione alieni con navicella player 1 (y=14-15)
  bool collision = false;
  for (int y = 0; y < 4; y++) {
    int maxAliens = (y == 0) ? 5 : 8;
    for (int x = 0; x < maxAliens; x++) {
      if (siAliens[x][y]) {
        int alienX, alienY, alienWidth;
        alienY = siAlienY + y * 2;

        if (y == 0) {
          alienX = siAlienX + x * 3;
          alienWidth = 2;
        } else {
          alienX = siAlienX + x * 2;
          alienWidth = 1;
        }

        // Controlla se l'alieno tocca la navicella (y >= 14)
        if (alienY >= 14) {
          // Controlla sovrapposizione orizzontale con player 1 (x-1, x, x+1)
          if ((alienX >= siPlayerX - 1 && alienX <= siPlayerX + 1) ||
              (alienX + alienWidth - 1 >= siPlayerX - 1 && alienX + alienWidth - 1 <= siPlayerX + 1)) {
            collision = true;
            break;
          }
        }
      }
    }
    if (collision) break;
  }

  // Se c'è collisione, perdi una vita
  if (collision) {
    siLives--;
    showGameHUD(siScoreP1, siLives, siLevel, true);
    if (siLives <= 0) {
      // Game Over
      siGameActive = false;
      siGameOver = true;
      hudOverlayActive = false;  // Disattiva HUD per mostrare GAME OVER
      playSIGameOver(); // Suono game over arcade
      drawGameOver();
      delay(3000); // Mostra GAME OVER per 3 secondi
      return;
    } else {
      // Perdi una vita - mantieni alieni e scudi!
      siGameActive = false;
      delay(1000); // Pausa di 1 secondo
      resetSpaceInvadersPlayer(); // Reset solo giocatore
      return;
    }
  }

  // Controlla game over (alieni raggiungono il fondo senza toccare la navicella)
  if (siAlienY > 15) {
    siLives--;
    showGameHUD(siScoreP1, siLives, siLevel, true);
    if (siLives <= 0) {
      siGameActive = false;
      siGameOver = true;
      hudOverlayActive = false;  // Disattiva HUD per mostrare GAME OVER
      playSIGameOver(); // Suono game over arcade
      drawGameOver();
      delay(3000);
      return;
    } else {
      // Perdi una vita - mantieni alieni e scudi!
      siGameActive = false;
      delay(1000);
      resetSpaceInvadersPlayer(); // Reset solo giocatore
      return;
    }
  }

  drawSpaceInvaders();
}

void drawSpaceInvaders() {
  clearMatrixNoShow();

  // Disegna player 1 (in basso) - Cannone a forma di T (3 LED: base + canna)
  CRGB p1Color = CRGB(0, 255, 0);
  setPixel(siPlayerX - 1, 15, p1Color); // Sinistra base
  setPixel(siPlayerX, 15, p1Color);     // Centro base
  setPixel(siPlayerX + 1, 15, p1Color); // Destra base
  setPixel(siPlayerX, 14, p1Color);     // Canna

  // Disegna player 2 (in alto) se modalità 2 giocatori
  if (!siVsAI) {
    CRGB p2Color = CRGB(0, 0, 255);
    setPixel(siPlayer2X - 1, 0, p2Color);
    setPixel(siPlayer2X, 0, p2Color);
    setPixel(siPlayer2X + 1, 0, p2Color);
    setPixel(siPlayer2X, 1, p2Color);
  }

  // Disegna scudi con deterioramento progressivo (rettangoli 3x2)
  for (int s = 0; s < 4; s++) {
    for (int sy = 0; sy < 2; sy++) {
      for (int sx = 0; sx < 3; sx++) {
        if (siShields[s][sy][sx] > 0) {
          int px = siShieldX[s] + sx;
          int py = siShieldY + sy;
          CRGB shieldColor;
          // Colore basato sull'integrità (3=verde pieno, 2=giallo, 1=rosso)
          if (siShields[s][sy][sx] == 3) shieldColor = CRGB(0, 255, 0);
          else if (siShields[s][sy][sx] == 2) shieldColor = CRGB(200, 200, 0);
          else shieldColor = CRGB(150, 0, 0);
          setPixel(px, py, shieldColor);
        }
      }
    }
  }

  // Disegna proiettili
  if (siBulletY != -1) {
    setPixel(siBulletX, siBulletY, CRGB(255, 255, 0));
  }
  if (siBullet2Y != -1 && !siVsAI) {
    setPixel(siBullet2X, siBullet2Y, CRGB(0, 255, 255));
  }

  // Disegna proiettili alieni (rossi)
  for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
    if (siAlienBulletY[i] != -1) {
      setPixel(siAlienBulletX[i], siAlienBulletY[i], CRGB(255, 0, 0));
    }
  }

  // Disegna UFO bonus (3 LED orizzontali in Y=0)
  if (siUfoX != -1) {
    CRGB ufoColor = CRGB(255, 0, 255); // Magenta per UFO
    for (int i = 0; i < 3; i++) {
      int ux = siUfoX + i;
      if (ux >= 0 && ux < 16) {
        setPixel(ux, 0, ufoColor);
      }
    }
  }

  // Disegna alieni con colori diversi per riga e dimensioni corrette
  for (int y = 0; y < 4; y++) {
    int maxAliens = (y == 0) ? 5 : 8;
    for (int x = 0; x < maxAliens; x++) {
      if (siAliens[x][y]) {
        int alienX, alienY;
        CRGB alienColor = siGetAlienColor(y);

        if (y == 0) {
          // Riga 0: alieni da 2 LED, spaziatura orizzontale 1 LED, verticale 1 LED
          alienX = siAlienX + x * 3; // (2 LED alieno + 1 LED spazio)
          alienY = siAlienY + y * 2; // Spaziatura verticale 1 LED tra righe

          if (alienX >= 0 && alienX + 1 < 16 && alienY >= 0 && alienY < 16) {
            setPixel(alienX, alienY, alienColor);
            setPixel(alienX + 1, alienY, alienColor);
          }
        } else {
          // Righe 1-3: alieni da 1 LED, spaziatura orizzontale 1 LED, verticale 1 LED
          alienX = siAlienX + x * 2; // (1 LED alieno + 1 LED spazio)
          alienY = siAlienY + y * 2; // Spaziatura verticale 1 LED tra righe

          if (alienX >= 0 && alienX < 16 && alienY >= 0 && alienY < 16) {
            setPixel(alienX, alienY, alienColor);
          }
        }
      }
    }
  }

  FastLED.show();
}

void drawGameOver() {
  clearMatrixNoShow();

  // Disegna "GAME" in alto con 1 LED di spaziatura tra lettere
  drawCharacter('G', 0, 2, CRGB(255, 0, 0));
  drawCharacter('A', 4, 2, CRGB(255, 0, 0));
  drawCharacter('M', 8, 2, CRGB(255, 0, 0));
  drawCharacter('E', 12, 2, CRGB(255, 0, 0));

  // Disegna "OVER" in basso con 1 LED di spaziatura tra lettere
  drawCharacter('O', 0, 9, CRGB(255, 0, 0));
  drawCharacter('V', 4, 9, CRGB(255, 0, 0));
  drawCharacter('E', 8, 9, CRGB(255, 0, 0));
  drawCharacter('R', 12, 9, CRGB(255, 0, 0));

  FastLED.show();
}

void drawLevelComplete() {
  clearMatrixNoShow();

  // Disegna "LEVEL" in alto con colore verde
  drawCharacter('L', 0, 2, CRGB(0, 255, 0));
  drawCharacter('E', 3, 2, CRGB(0, 255, 0));
  drawCharacter('V', 6, 2, CRGB(0, 255, 0));
  drawCharacter('E', 9, 2, CRGB(0, 255, 0));
  drawCharacter('L', 12, 2, CRGB(0, 255, 0));

  // Disegna numero del livello completato al centro
  char levelChar = '0' + siLevel;
  drawBigDigit(siLevel, 5, 9, CRGB(0, 255, 0));

  FastLED.show();
}

void handleSpaceInvaders() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Space Invaders</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#1a1a1a;padding:20px;border-radius:10px;border:1px solid #333;}";
  html += ".mode-select{margin:20px;}";
  html += ".mode-select button{padding:15px 30px;margin:10px;font-size:18px;border:none;border-radius:5px;cursor:pointer;background:#333;color:#fff;}";
  html += ".mode-select button.active{background:#4CAF50;color:white;}";
  html += ".score{display:flex;justify-content:space-around;margin:20px;font-size:24px;}";
  html += ".gamepad{display:grid;grid-template-columns:repeat(3,80px);gap:10px;margin:20px auto;justify-content:center;}";
  html += ".gamepad button{height:80px;font-size:24px;border:none;border-radius:10px;background:#444;color:white;cursor:pointer;touch-action:none;-webkit-touch-callout:none;-webkit-user-select:none;user-select:none;}";
  html += ".gamepad button:active{background:#666;}";
  html += ".fire-btn{padding:20px 40px;background:#f44336;color:white;border:none;border-radius:10px;font-size:24px;margin:10px;cursor:pointer;touch-action:none;-webkit-touch-callout:none;-webkit-user-select:none;user-select:none;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1,h3{color:#fff;}";
  html += "</style>";
  html += "<script>";
  html += "let moveInterval=null;";
  html += "function startGame(){fetch('/sicontrol?start=1').then(()=>updateScore());}";
  html += "function resetGame(){fetch('/sicontrol?reset=1').then(()=>updateScore());}";
  html += "function move(dir){fetch('/sicontrol?move='+dir);}";
  html += "function startMove(dir){move(dir);if(moveInterval)clearInterval(moveInterval);moveInterval=setInterval(()=>move(dir),100);}";
  html += "function stopMove(){if(moveInterval){clearInterval(moveInterval);moveInterval=null;}}";
  html += "function fire(){fetch('/sicontrol?fire=1');}";
  html += "function updateScore(){fetch('/sicontrol?score=1').then(r=>r.json()).then(d=>{document.getElementById('score1').innerText=d.p1;document.getElementById('lives').innerText=d.lives;document.getElementById('level').innerText=d.level;});}";
  html += "setInterval(updateScore,1000);";
  html += "let activeKeys={};";
  html += "document.addEventListener('keydown',function(e){";
  html += "if(e.key=='ArrowLeft'||e.key=='ArrowRight'||e.key==' '){e.preventDefault();}";
  html += "if(activeKeys[e.key])return;";
  html += "activeKeys[e.key]=true;";
  html += "if(e.key=='ArrowLeft')startMove('left');";
  html += "else if(e.key=='ArrowRight')startMove('right');";
  html += "else if(e.key==' '||e.key=='Spacebar')fire();";
  html += "});";
  html += "document.addEventListener('keyup',function(e){";
  html += "if(e.key=='ArrowLeft'||e.key=='ArrowRight'||e.key==' '){e.preventDefault();}";
  html += "delete activeKeys[e.key];";
  html += "if(e.key=='ArrowLeft'||e.key=='ArrowRight')stopMove();";
  html += "});";
  html += "document.addEventListener('touchmove',function(e){if(e.target.tagName=='BUTTON')e.preventDefault();},{passive:false});";
  html += "window.addEventListener('DOMContentLoaded',function(){";
  html += "document.querySelectorAll('.gamepad button').forEach(b=>{";
  html += "b.addEventListener('touchstart',function(e){e.preventDefault();let dir=this.dataset.dir;if(dir)startMove(dir);},false);";
  html += "b.addEventListener('touchend',function(e){e.preventDefault();stopMove();},false);";
  html += "b.addEventListener('mousedown',function(e){e.preventDefault();let dir=this.dataset.dir;if(dir)startMove(dir);},false);";
  html += "b.addEventListener('mouseup',function(e){e.preventDefault();stopMove();},false);";
  html += "b.addEventListener('mouseleave',function(e){stopMove();},false);";
  html += "});";
  html += "let fireBtn=document.querySelector('.fire-btn');";
  html += "fireBtn.addEventListener('touchstart',function(e){e.preventDefault();fire();},false);";
  html += "fireBtn.addEventListener('mousedown',function(e){e.preventDefault();fire();},false);";
  html += "});";
  html += "function enableBT(){window.location='/enableBluetooth?game=spaceinvaders';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a> <a href='#' onclick='enableBT()' style='background:#2196F3;margin-left:10px;'>🎮 Bluetooth</a></div>";
  html += "<div class='container'>";
  html += "<h1>👾 Space Invaders</h1>";
  html += "<div class='score'>";
  html += "<div>PUNTEGGIO: <span id='score1'>0</span></div>";
  html += "</div>";
  html += "<div style='text-align:center;margin:15px 0;font-size:20px;color:#ff9800;'>❤️ Vite: <span id='lives'>5</span></div>";
  html += "<div style='text-align:center;margin:10px 0;font-size:24px;color:#00ff00;font-weight:bold;'>🎮 Livello: <span id='level'>1</span>/5</div>";
  html += "<div style='margin:15px;display:flex;justify-content:center;gap:10px;'>";
  html += "<button onclick='startGame()' style='padding:12px 25px;font-size:18px;border:none;border-radius:5px;cursor:pointer;background:#4CAF50;color:white;font-weight:bold;'>▶️ START</button>";
  html += "<button onclick='resetGame()' style='padding:12px 25px;font-size:18px;border:none;border-radius:5px;cursor:pointer;background:#FF9800;color:white;font-weight:bold;'>🔄 RESET</button>";
  html += "</div>";
  html += "<h3>Controlli</h3>";
  html += "<button class='fire-btn'>🔥 FIRE</button>";
  html += "<div class='gamepad'>";
  html += "<div></div><div></div><div></div>";
  html += "<button data-dir='left'>⬅️</button>";
  html += "<div></div>";
  html += "<button data-dir='right'>➡️</button>";
  html += "</div>";
  html += "<p style='color:#aaa;margin-top:20px;'>Usa frecce sinistra/destra e SPAZIO per sparare</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleSIControl() {
  if (server.hasArg("start")) {
    siVsAI = true;  // Sempre vs IA (modalità 2 giocatori rimossa)
    resetSpaceInvaders(true); // Reset completo con vite e punteggi dal livello 1
    siGameActive = true;  // Avvia il gioco
    siGameOver = false;
    siLastUpdate = millis();
    changeState(STATE_GAME_SPACE_INVADERS);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("reset")) {
    siVsAI = true;  // Sempre vs IA (modalità 2 giocatori rimossa)
    resetSpaceInvaders(true); // Reset completo con vite e punteggi
    changeState(STATE_GAME_SPACE_INVADERS);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("move")) {
    String dir = server.arg("move");
    if (dir == "left") siMovePlayer(1, -1);
    else if (dir == "right") siMovePlayer(1, 1);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("fire")) {
    siShoot(1);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("score")) {
    String json = "{\"p1\":" + String(siScoreP1) + ",\"lives\":" + String(siLives) + ",\"level\":" + String(siLevel) + "}";
    server.send(200, "application/json", json);
    return;
  }

  server.send(400, "text/plain", "Bad Request");
}

// ============================================
// PONG
// ============================================
void resetPong(bool resetPaddles) {
  // Reset paddle solo se richiesto (nuovo gioco)
  if (resetPaddles) {
    pongPaddle1Y = 6;
    pongPaddle2Y = 6;
    pongLives = 5;
    pongGameOver = false;
    pongScoreP1 = 0;
    pongScoreP2 = 0;
  }

  // Imposta velocità pallina in base alla difficoltà
  switch(pongAIDifficulty) {
    case 0: // FACILE
      pongSpeed = 150;
      break;
    case 1: // MEDIO
      pongSpeed = 150;
      break;
    case 2: // DIFFICILE - pallina più veloce
      pongSpeed = 100;
      break;
    case 3: // IMPOSSIBILE - pallina molto veloce
      pongSpeed = 70;
      break;
    default:
      pongSpeed = 150;
      break;
  }

  // Reset palla - attaccata alla racchetta del giocatore 1
  pongBallX = 1;  // Posizione X vicino alla racchetta
  pongBallY = pongPaddle1Y + 2;  // Centro della racchetta (paddle ha altezza 4)
  pongBallDirX = 1;  // Direzione verso destra
  pongBallDirY = (random(0, 2) == 0) ? 1 : -1;  // Direzione Y casuale
  pongBallOnPaddle = true;  // Pallina attaccata alla racchetta

  // NON avviare automaticamente - aspetta che l'utente prema START
  // Il gioco si avvia solo quando viene premuto il tasto NUOVA PARTITA
  if (resetPaddles) {
    pongGameActive = false;  // Aspetta START
  }
  // Se resetPaddles = false (punto segnato), mantieni lo stato attuale di pongGameActive

  pongLastUpdate = millis();
  drawPong();
}

void pongMovePaddle(int player, int dir) {
  if (!pongGameActive) return;

  if (player == 1) {
    pongPaddle1Y += dir;
    if (pongPaddle1Y < 0) pongPaddle1Y = 0;
    if (pongPaddle1Y > 12) pongPaddle1Y = 12;

    // Se la pallina è attaccata, muovila con la racchetta e poi rilasciala
    if (pongBallOnPaddle) {
      pongBallY = pongPaddle1Y + 2;
      if (pongBallY < 0) pongBallY = 0;
      if (pongBallY > 15) pongBallY = 15;
      pongBallOnPaddle = false;  // Rilascia la pallina dopo il primo movimento
    }
  } else if (player == 2) {
    pongPaddle2Y += dir;
    if (pongPaddle2Y < 0) pongPaddle2Y = 0;
    if (pongPaddle2Y > 12) pongPaddle2Y = 12;
  }
}

void updatePong() {
  // Se GAME OVER, mostra la schermata GAME OVER
  static bool pongGameOverDrawn = false;
  if (pongGameOver) {
    if (!pongGameOverDrawn) {
      drawPongGameOver();
      pongGameOverDrawn = true;
    }
    return;
  } else {
    pongGameOverDrawn = false;
  }

  if (!pongGameActive) return;

  unsigned long currentTime = millis();
  if (currentTime - pongLastUpdate < pongSpeed) return;
  pongLastUpdate = currentTime;

  // AI MIGLIORATA per paddle 2 con predizione e difficoltà configurabile
  if (pongVsAI) {
    // Usa il livello di difficoltà configurato
    int aiLevel = pongAIDifficulty; // 0=facile, 1=medio, 2=difficile, 3=impossibile

    // L'AI reagisce quando la palla si muove verso di lei
    if (pongBallDirX > 0) {
      int targetY = pongBallY; // Posizione target predetta
      int paddleCenter = pongPaddle2Y + 2; // Centro paddle (dimensione 4)

      // PREDIZIONE TRAIETTORIA - Calcola dove arriverà la palla
      if (aiLevel >= 1) {
        // Predici la posizione futura della palla
        int predictedY = pongBallY;
        int predictedX = pongBallX;
        int dirY = pongBallDirY;

        // Simula il movimento della palla fino al paddle AI (X=14)
        while (predictedX < 14) {
          predictedY += dirY;
          predictedX++;

          // Rimbalzo sui bordi durante la predizione
          if (predictedY <= 0) {
            predictedY = 0;
            dirY = 1; // Cambia direzione verso il basso
          } else if (predictedY >= 15) {
            predictedY = 15;
            dirY = -1; // Cambia direzione verso l'alto
          }
        }

        targetY = predictedY;
      }

      // PROBABILITÀ DI REAZIONE basata su livello AI - RIBILANCIATE
      int reactionChance = 0;
      int speedMultiplier = 1; // Velocità movimento AI

      switch(aiLevel) {
        case 0: // FACILE - lento e impreciso (migliorato: 50→65)
          reactionChance = 65;
          speedMultiplier = 1;
          break;
        case 1: // MEDIO - reattivo ma non perfetto
          reactionChance = 75;
          speedMultiplier = 1;
          break;
        case 2: // DIFFICILE - reattivo ma più errori per dare chance al giocatore (90→78)
          reactionChance = 78;
          speedMultiplier = 1;
          break;
        case 3: // IMPOSSIBILE - molto difficile ma vincibile (95→82, speed 2→1)
          reactionChance = 82;
          speedMultiplier = 1;
          break;
      }

      // Aggiungi errore casuale per realismo (anche in modalità impossibile, seppur raramente)
      if (random(0, 100) > reactionChance) {
        // AI "sbaglia" - segue la palla senza predizione o fa movimento casuale
        targetY = pongBallY + random(-3, 4);
      }

      // Errore aggiuntivo per difficile/impossibile: aumentato per dare più chance (8%→15%)
      if (aiLevel >= 2 && random(0, 100) < 15) {
        // 15% chance di sbagliare completamente il tiro
        targetY = pongBallY + random(-5, 6);
      }

      // MOVIMENTO AI - adattivo in base alla difficoltà
      int tolerance = 1; // Tolleranza per evitare oscillazioni
      if (aiLevel >= 2) tolerance = 0; // Difficile e Impossibile più precisi

      if (targetY < paddleCenter - tolerance) {
        // Muovi verso l'alto
        for (int i = 0; i < speedMultiplier; i++) {
          pongPaddle2Y--;
          if (pongPaddle2Y < 0) {
            pongPaddle2Y = 0;
            break;
          }
        }
      } else if (targetY > paddleCenter + tolerance) {
        // Muovi verso il basso
        for (int i = 0; i < speedMultiplier; i++) {
          pongPaddle2Y++;
          if (pongPaddle2Y > 12) {
            pongPaddle2Y = 12;
            break;
          }
        }
      }
    }
  }

  // Se la pallina è attaccata alla racchetta, non muoverla
  if (pongBallOnPaddle) {
    // Tieni la pallina attaccata alla racchetta del giocatore 1
    pongBallX = 1;
    pongBallY = pongPaddle1Y + 2;
    if (pongBallY < 0) pongBallY = 0;
    if (pongBallY > 15) pongBallY = 15;
    drawPong();
    return;
  }

  // Muovi palla
  pongBallX += pongBallDirX;
  pongBallY += pongBallDirY;

  // Rimbalzo sui bordi orizzontali
  if (pongBallY <= 0 || pongBallY >= 15) {
    pongBallDirY = -pongBallDirY;
    tone(BUZZER_PIN, 400, 50); // Suono breve acuto
  }

  // Rimbalzo su paddle 1 (sinistra)
  if (pongBallX == 1 && pongBallY >= pongPaddle1Y && pongBallY <= pongPaddle1Y + 3) {
    pongBallDirX = -pongBallDirX;
    playBeep(); // Suono rimbalzo paddle
  }

  // Rimbalzo su paddle 2 (destra)
  if (pongBallX == 14 && pongBallY >= pongPaddle2Y && pongBallY <= pongPaddle2Y + 3) {
    pongBallDirX = -pongBallDirX;
    playBeep(); // Suono rimbalzo paddle
  }

  // Punto per player 2 (AI) - player 1 perde una vita
  if (pongBallX < 0) {
    pongScoreP2++;
    tone(BUZZER_PIN, 150, 1000); // Suono grave 1 secondo
    pongLives--;
    showGameHUD(pongScoreP1, pongLives, 0, false);

    if (pongLives <= 0) {
      // GAME OVER
      pongGameOver = true;
      pongGameActive = false;
      drawPongGameOver();
      return;
    }

    resetPong(false); // NON resettare le paddle
    return;
  }

  // Punto per player 1
  if (pongBallX > 15) {
    pongScoreP1++;
    tone(BUZZER_PIN, 150, 1000); // Suono grave 1 secondo
    resetPong(false); // NON resettare le paddle
    return;
  }

  drawPong();
}

void drawPong() {
  clearMatrixNoShow();

  // Disegna paddle 1 (sinistra - verde)
  for (int i = 0; i < 4; i++) {
    setPixel(0, pongPaddle1Y + i, CRGB(0, 255, 0));
  }

  // Disegna paddle 2 (destra - blu o rosso)
  CRGB p2Color = pongVsAI ? CRGB(255, 0, 0) : CRGB(0, 0, 255);
  for (int i = 0; i < 4; i++) {
    setPixel(15, pongPaddle2Y + i, p2Color);
  }

  // Disegna palla (bianca)
  setPixel(pongBallX, pongBallY, CRGB(255, 255, 255));

  // Linea centrale
  for (int y = 0; y < 16; y += 2) {
    setPixel(8, y, CRGB(40, 40, 40));
  }

  FastLED.show();
}

void drawPongGameOver() {
  // Mostra "GAME OVER" in rosso con font 3x5, lettere distanziate e centrato
  clearMatrixNoShow();

  // "GAME" alla riga Y=2 (centrato verticalmente nella parte alta)
  drawCharacter('G', 0, 2, CRGB(255, 0, 0));
  drawCharacter('A', 4, 2, CRGB(255, 0, 0));
  drawCharacter('M', 8, 2, CRGB(255, 0, 0));
  drawCharacter('E', 12, 2, CRGB(255, 0, 0));

  // "OVER" alla riga Y=9 (centrato verticalmente nella parte bassa)
  drawCharacter('O', 0, 9, CRGB(255, 0, 0));
  drawCharacter('V', 4, 9, CRGB(255, 0, 0));
  drawCharacter('E', 8, 9, CRGB(255, 0, 0));
  drawCharacter('R', 12, 9, CRGB(255, 0, 0));

  FastLED.show();

  // Suono GAME OVER
  playGameOver();
}

void handlePong() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Pong</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#1a1a1a;padding:20px;border-radius:10px;border:1px solid #333;}";
  html += ".mode-select{margin:20px;}";
  html += ".mode-select button{padding:15px 30px;margin:10px;font-size:18px;border:none;border-radius:5px;cursor:pointer;background:#333;color:#fff;}";
  html += ".mode-select button.active{background:#4CAF50;color:white;}";
  html += ".difficulty-select{margin:20px;padding:15px;background:#222;border-radius:8px;border:1px solid #444;}";
  html += ".difficulty-select h4{margin:0 0 10px 0;color:#aaa;font-size:14px;}";
  html += ".difficulty-select button{padding:10px 20px;margin:5px;font-size:16px;border:none;border-radius:5px;cursor:pointer;background:#444;color:#fff;}";
  html += ".difficulty-select button.active{background:#FF5722;}";
  html += ".score{display:flex;justify-content:space-around;margin:20px;font-size:24px;}";
  html += ".controls{display:flex;justify-content:space-around;margin:20px;}";
  html += ".paddle-control{display:flex;flex-direction:column;gap:10px;}";
  html += ".paddle-control button{padding:20px 40px;font-size:20px;border:none;border-radius:10px;background:#444;color:white;cursor:pointer;}";
  html += ".paddle-control button:active{background:#666;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1,h3{color:#fff;}";
  html += "</style>";
  html += "<script>";
  html += "let mode='ai';";
  html += "let difficulty=1;";
  html += "let moveIntervals={};";
  html += "function setMode(m){mode=m;fetch('/pongcontrol?reset=1&mode='+m+'&difficulty='+difficulty);document.querySelectorAll('.mode-select button').forEach(b=>b.classList.remove('active'));document.getElementById('mode-'+m).classList.add('active');document.getElementById('difficulty-section').style.display=(m=='ai'?'block':'none');updateScore();}";
  html += "function setDifficulty(d){difficulty=d;fetch('/pongcontrol?difficulty='+d);document.querySelectorAll('.difficulty-select button').forEach(b=>b.classList.remove('active'));document.getElementById('diff-'+d).classList.add('active');}";
  html += "function move(player,dir){fetch('/pongcontrol?player='+player+'&move='+dir);}";
  html += "function startMove(player,dir){move(player,dir);let key='p'+player;if(moveIntervals[key])clearInterval(moveIntervals[key]);moveIntervals[key]=setInterval(()=>move(player,dir),80);}";
  html += "function stopMove(player){let key='p'+player;if(moveIntervals[key]){clearInterval(moveIntervals[key]);delete moveIntervals[key];}}";
  html += "function updateScore(){fetch('/pongcontrol?score=1').then(r=>r.json()).then(d=>{document.getElementById('score1').innerText=d.p1;document.getElementById('score2').innerText=d.p2;});}";
  html += "function resetScore(){fetch('/pongcontrol?resetscore=1').then(()=>updateScore());}";
  html += "function newGame(){fetch('/pongcontrol?reset=1&start=1&mode='+mode+'&difficulty='+difficulty).then(()=>updateScore());}";
  html += "setInterval(updateScore,1000);";
  html += "let keys={};";
  html += "document.addEventListener('keydown',function(e){if(keys[e.key])return;keys[e.key]=true;if(e.key=='w'||e.key=='W')startMove(1,'up');else if(e.key=='s'||e.key=='S')startMove(1,'down');else if(e.key=='ArrowUp')startMove(2,'up');else if(e.key=='ArrowDown')startMove(2,'down');});";
  html += "document.addEventListener('keyup',function(e){delete keys[e.key];if(e.key=='w'||e.key=='W'||e.key=='s'||e.key=='S')stopMove(1);else if(e.key=='ArrowUp'||e.key=='ArrowDown')stopMove(2);});";
  html += "window.addEventListener('DOMContentLoaded',function(){";
  html += "document.querySelectorAll('.paddle-control button').forEach(b=>{";
  html += "b.addEventListener('touchstart',function(e){e.preventDefault();let p=parseInt(this.dataset.player);let d=this.dataset.dir;startMove(p,d);},false);";
  html += "b.addEventListener('touchend',function(e){e.preventDefault();let p=parseInt(this.dataset.player);stopMove(p);},false);";
  html += "b.addEventListener('mousedown',function(e){e.preventDefault();let p=parseInt(this.dataset.player);let d=this.dataset.dir;startMove(p,d);},false);";
  html += "b.addEventListener('mouseup',function(e){e.preventDefault();let p=parseInt(this.dataset.player);stopMove(p);},false);";
  html += "b.addEventListener('mouseleave',function(e){let p=parseInt(this.dataset.player);stopMove(p);},false);";
  html += "});});";
  html += "function enableBT(){window.location='/enableBluetooth?game=pong';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a> <a href='#' onclick='enableBT()' style='background:#2196F3;margin-left:10px;'>🎮 Bluetooth</a></div>";
  html += "<div class='container'>";
  html += "<h1>🏓 Pong</h1>";
  html += "<div class='mode-select'>";
  html += "<button id='mode-ai' class='active' onclick='setMode(\"ai\")'>🤖 VS IA</button>";
  html += "<button id='mode-2p' onclick='setMode(\"2p\")'>👥 2 Giocatori</button>";
  html += "</div>";
  html += "<div id='difficulty-section' class='difficulty-select'>";
  html += "<h4>⚙️ DIFFICOLTÀ IA</h4>";
  html += "<button id='diff-0' onclick='setDifficulty(0)'>😊 Facile</button>";
  html += "<button id='diff-1' class='active' onclick='setDifficulty(1)'>🙂 Medio</button>";
  html += "<button id='diff-2' onclick='setDifficulty(2)'>😎 Difficile</button>";
  html += "<button id='diff-3' onclick='setDifficulty(3)'>💀 Impossibile</button>";
  html += "</div>";
  html += "<div class='score'>";
  html += "<div>P1: <span id='score1'>0</span></div>";
  html += "<div>P2: <span id='score2'>0</span></div>";
  html += "</div>";
  html += "<div style='margin:15px;display:flex;justify-content:center;gap:10px;'>";
  html += "<button onclick='newGame()' style='padding:10px 20px;font-size:16px;border:none;border-radius:5px;cursor:pointer;background:#4CAF50;color:white;'>🎮 NUOVA PARTITA</button>";
  html += "<button onclick='resetScore()' style='padding:10px 20px;font-size:16px;border:none;border-radius:5px;cursor:pointer;background:#FF9800;color:white;'>🔄 RESET PUNTEGGIO</button>";
  html += "</div>";
  html += "<div class='controls'>";
  html += "<div>";
  html += "<h3>Player 1 (W/S)</h3>";
  html += "<div class='paddle-control'>";
  html += "<button data-player='1' data-dir='up'>⬆️</button>";
  html += "<button data-player='1' data-dir='down'>⬇️</button>";
  html += "</div>";
  html += "</div>";
  html += "<div id='p2controls'>";
  html += "<h3>Player 2 (↑/↓)</h3>";
  html += "<div class='paddle-control'>";
  html += "<button data-player='2' data-dir='up'>⬆️</button>";
  html += "<button data-player='2' data-dir='down'>⬇️</button>";
  html += "</div>";
  html += "</div>";
  html += "</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handlePongControl() {
  if (server.hasArg("reset")) {
    pongVsAI = (server.arg("mode") == "ai");
    if (server.hasArg("difficulty")) {
      pongAIDifficulty = server.arg("difficulty").toInt();
    }
    resetPong(); // Resetta tutto inclusi punteggi (resetPaddles = true di default)
    // Avvia il gioco solo se è presente il parametro "start"
    if (server.hasArg("start")) {
      pongGameActive = true;
      pongLastUpdate = millis();
    }
    changeState(STATE_GAME_PONG);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("resetscore")) {
    pongScoreP1 = 0;
    pongScoreP2 = 0;
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("difficulty")) {
    pongAIDifficulty = server.arg("difficulty").toInt();

    // Aggiorna velocità pallina in base alla nuova difficoltà
    switch(pongAIDifficulty) {
      case 0: // FACILE
        pongSpeed = 150;
        break;
      case 1: // MEDIO
        pongSpeed = 150;
        break;
      case 2: // DIFFICILE - pallina più veloce
        pongSpeed = 100;
        break;
      case 3: // IMPOSSIBILE - pallina molto veloce
        pongSpeed = 70;
        break;
      default:
        pongSpeed = 150;
        break;
    }

    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("player") && server.hasArg("move")) {
    int player = server.arg("player").toInt();
    String move = server.arg("move");
    int dir = (move == "up") ? -1 : 1;
    pongMovePaddle(player, dir);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("score")) {
    String json = "{\"p1\":" + String(pongScoreP1) + ",\"p2\":" + String(pongScoreP2) + "}";
    server.send(200, "application/json", json);
    return;
  }

  server.send(400, "text/plain", "Bad Request");
}

// ============================================
// SNAKE
// ============================================
void resetSnake() {
  snakeLength = 3;
  snakeX[0] = 8;
  snakeY[0] = 8;
  snakeX[1] = 7;
  snakeY[1] = 8;
  snakeX[2] = 6;
  snakeY[2] = 8;
  snakeDirX = 1;
  snakeDirY = 0;
  snakeScore = 0;
  snakeLives = 5;
  snakeGameActive = true;
  snakeGameOver = false;
  snakeSpeed = 200;
  snakeLastUpdate = millis();
  snakePlaceFood();

  // Disegna subito il gioco (rimossa visualizzazione vite iniziale che bloccava gamepad)
  drawSnake();
}

void snakePlaceFood() {
  bool validPos = false;
  while (!validPos) {
    snakeFoodX = random(0, 16);
    snakeFoodY = random(0, 16);
    validPos = true;

    for (int i = 0; i < snakeLength; i++) {
      if (snakeX[i] == snakeFoodX && snakeY[i] == snakeFoodY) {
        validPos = false;
        break;
      }
    }
  }
}

void snakeChangeDir(int dx, int dy) {
  if (!snakeGameActive) return;

  // Impedisci inversione di direzione
  if ((dx != 0 && snakeDirX == -dx) || (dy != 0 && snakeDirY == -dy)) return;

  snakeDirX = dx;
  snakeDirY = dy;
}

void updateSnake() {
  static bool gameOverDrawn = false;

  // Se è GAME OVER, mostra il messaggio SOLO UNA VOLTA
  if (snakeGameOver) {
    if (!gameOverDrawn) {
      drawGameOver();
      gameOverDrawn = true;
    }
    return;
  } else {
    gameOverDrawn = false; // Reset quando il gioco è attivo
  }

  if (!snakeGameActive) return;

  unsigned long currentTime = millis();
  if (currentTime - snakeLastUpdate < snakeSpeed) return;
  snakeLastUpdate = currentTime;

  // Calcola nuova posizione testa
  int newX = snakeX[0] + snakeDirX;
  int newY = snakeY[0] + snakeDirY;

  // Controllo collisione con i bordi
  if (newX < 0 || newX > 15 || newY < 0 || newY > 15) {
    snakeLives--;
    playGameOver(); // Suono morte sempre
    showGameHUD(snakeScore, snakeLives, 0, false);
    if (snakeLives <= 0) {
      snakeGameOver = true;
      snakeGameActive = false;
    } else {
      // Reset posizione serpente senza perdere punteggio
      snakeLength = 3;
      snakeX[0] = 8;
      snakeY[0] = 8;
      snakeX[1] = 7;
      snakeY[1] = 8;
      snakeX[2] = 6;
      snakeY[2] = 8;
      snakeDirX = 1;
      snakeDirY = 0;
      snakePlaceFood();
      // Usa snakeLastUpdate per creare una pausa non bloccante
      snakeLastUpdate = millis() + 500; // Pausa di 500ms prima di ricominciare
    }
    return;
  }

  // Controllo collisione con se stesso
  for (int i = 0; i < snakeLength; i++) {
    if (snakeX[i] == newX && snakeY[i] == newY) {
      snakeLives--;
      playGameOver(); // Suono morte sempre
      showGameHUD(snakeScore, snakeLives, 0, false);
      if (snakeLives <= 0) {
        snakeGameOver = true;
        snakeGameActive = false;
      } else {
        // Reset posizione serpente senza perdere punteggio
        snakeLength = 3;
        snakeX[0] = 8;
        snakeY[0] = 8;
        snakeX[1] = 7;
        snakeY[1] = 8;
        snakeX[2] = 6;
        snakeY[2] = 8;
        snakeDirX = 1;
        snakeDirY = 0;
        snakePlaceFood();
        // Usa snakeLastUpdate per creare una pausa non bloccante
        snakeLastUpdate = millis() + 500; // Pausa di 500ms prima di ricominciare
      }
      return;
    }
  }

  // Sposta il serpente
  for (int i = snakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }
  snakeX[0] = newX;
  snakeY[0] = newY;

  // Controllo se mangia il cibo
  if (newX == snakeFoodX && newY == snakeFoodY) {
    snakeLength++;
    snakeScore += 10;
    if (snakeLength < 64) {
      snakeX[snakeLength - 1] = snakeX[snakeLength - 2];
      snakeY[snakeLength - 1] = snakeY[snakeLength - 2];
    }
    snakePlaceFood();
    playEat(); // Suono mangiare cibo

    // Aumenta difficoltà
    if (snakeSpeed > 80) snakeSpeed -= 5;
  }

  drawSnake();
}

void drawSnake() {
  clearMatrixNoShow();

  // Disegna serpente (verde, testa più chiara)
  for (int i = 0; i < snakeLength; i++) {
    if (i == 0) {
      setPixel(snakeX[i], snakeY[i], CRGB(0, 255, 0));
    } else {
      setPixel(snakeX[i], snakeY[i], CRGB(0, 150, 0));
    }
  }

  // Disegna cibo (rosso)
  setPixel(snakeFoodX, snakeFoodY, CRGB(255, 0, 0));

  FastLED.show();
}

void handleSnake() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Snake</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#1a1a1a;padding:20px;border-radius:10px;border:1px solid #333;}";
  html += ".score{font-size:32px;margin:20px;color:#0f0;}";
  html += ".gamepad{display:grid;grid-template-columns:repeat(3,80px);gap:10px;margin:20px auto;justify-content:center;}";
  html += ".gamepad button{height:80px;font-size:24px;border:none;border-radius:10px;background:#444;color:white;cursor:pointer;}";
  html += ".gamepad button:active{background:#666;}";
  html += ".reset-btn{padding:15px 30px;background:#4CAF50;color:white;border:none;border-radius:10px;font-size:18px;margin:20px;cursor:pointer;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{color:#fff;}";
  html += "</style>";
  html += "<script>";
  html += "function reset(){fetch('/snakecontrol?reset=1');updateScore();}";
  html += "function move(dir){fetch('/snakecontrol?dir='+dir);}";
  html += "function updateScore(){fetch('/snakecontrol?score=1').then(r=>r.json()).then(d=>{document.getElementById('score').innerText=d.score;document.getElementById('lives').innerText=d.lives;document.getElementById('status').innerText=d.active?'🟢 In gioco':'🔴 Game Over';});}";
  html += "setInterval(updateScore,500);";
  html += "document.addEventListener('keydown',function(e){e.preventDefault();if(e.key=='ArrowUp')move('up');else if(e.key=='ArrowDown')move('down');else if(e.key=='ArrowLeft')move('left');else if(e.key=='ArrowRight')move('right');});";
  html += "window.addEventListener('DOMContentLoaded',function(){";
  html += "document.querySelectorAll('.gamepad button').forEach(b=>{";
  html += "b.addEventListener('touchstart',function(e){e.preventDefault();let d=this.dataset.dir;if(d)move(d);},false);";
  html += "b.addEventListener('mousedown',function(e){e.preventDefault();let d=this.dataset.dir;if(d)move(d);},false);";
  html += "});});";
  html += "function enableBT(){window.location='/enableBluetooth?game=snake';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a> <a href='#' onclick='enableBT()' style='background:#2196F3;margin-left:10px;'>🎮 Bluetooth</a></div>";
  html += "<div class='container'>";
  html += "<h1>🐍 Snake</h1>";
  html += "<div class='score'>Punteggio: <span id='score'>0</span></div>";
  html += "<div style='font-size:24px;margin:10px;color:#ff4444;'>Vite: <span id='lives'>5</span> ❤️</div>";
  html += "<div id='status' style='font-size:20px;margin:10px;'>🟢 In gioco</div>";
  html += "<button class='reset-btn' onclick='reset()'>🔄 Nuova Partita</button>";
  html += "<div class='gamepad'>";
  html += "<div></div><button data-dir='up'>⬆️</button><div></div>";
  html += "<button data-dir='left'>⬅️</button>";
  html += "<div></div>";
  html += "<button data-dir='right'>➡️</button>";
  html += "<div></div><button data-dir='down'>⬇️</button><div></div>";
  html += "</div>";
  html += "<p style='color:#aaa;margin-top:20px;'>Usa le frecce direzionali per muoverti</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleSnakeControl() {
  if (server.hasArg("reset")) {
    resetSnake();
    changeState(STATE_GAME_SNAKE);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    if (dir == "up") snakeChangeDir(0, -1);
    else if (dir == "down") snakeChangeDir(0, 1);
    else if (dir == "left") snakeChangeDir(-1, 0);
    else if (dir == "right") snakeChangeDir(1, 0);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("score")) {
    String json = "{\"score\":" + String(snakeScore) + ",\"lives\":" + String(snakeLives) + ",\"active\":" + (snakeGameActive ? "true" : "false") + "}";
    server.send(200, "application/json", json);
    return;
  }

  server.send(400, "text/plain", "Bad Request");
}

// ============================================
// BREAKOUT
// ============================================
void resetBreakout() {
  breakoutPaddleX = 6;
  breakoutBallX = 8;
  breakoutBallY = 14;
  breakoutBallPosX = 8.0;
  breakoutBallPosY = 14.0;
  breakoutBallVelX = 0.8;  // Velocità ridotta
  breakoutBallVelY = -1.0; // Velocità ridotta
  breakoutBallDirX = 1;
  breakoutBallDirY = -1;
  breakoutScore = 0;
  breakoutLives = 5;  // 5 vite invece di 3
  breakoutLevel = 1;
  breakoutGameActive = true;
  breakoutLevelComplete = false;
  breakoutGameOver = false;
  breakoutBallOnPaddle = true;  // Inizia con palla sulla racchetta
  breakoutLastUpdate = millis();

  initBreakoutLevel(breakoutLevel);
  forceBreakoutRedraw();  // Forza ridisegno completo
  drawBreakout();         // Disegna la schermata iniziale
  forceBreakoutRedraw();  // Riattiva il flag per il prossimo ciclo
}

void initBreakoutLevel(int level) {
  // Resetta posizione palla e paddle
  breakoutPaddleX = 6;
  breakoutBallX = 8;
  breakoutBallY = 14;
  breakoutBallPosX = 8.0;
  breakoutBallPosY = 14.0;
  breakoutBallDirX = (random(0, 2) == 0) ? 1 : -1;
  breakoutBallDirY = -1;
  breakoutBallVelX = (breakoutBallDirX == 1) ? 0.8 : -0.8;  // Velocità ridotta
  breakoutBallVelY = -1.0;  // Velocità ridotta
  breakoutLevelComplete = false;
  breakoutBallOnPaddle = true;  // Nuova vita inizia con palla sulla racchetta

  // Pulisci tutti i mattoni
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 6; y++) {
      breakoutBricks[x][y] = false;
    }
  }

  // Pattern diversi per ogni livello (come l'arcade originale)
  switch((level - 1) % 8) {
    case 0: // Livello 1 - Muro pieno
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          breakoutBricks[x][y] = true;
        }
      }
      break;

    case 1: // Livello 2 - Linee alternate
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          if (y % 2 == 0) breakoutBricks[x][y] = true;
        }
      }
      break;

    case 2: // Livello 3 - Colonne
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          if (x % 3 != 1) breakoutBricks[x][y] = true;
        }
      }
      break;

    case 3: // Livello 4 - Scacchiera
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          if ((x + y) % 2 == 0) breakoutBricks[x][y] = true;
        }
      }
      break;

    case 4: // Livello 5 - Piramide
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          int distFromCenter = abs(x - 8);
          if (y >= distFromCenter / 2) breakoutBricks[x][y] = true;
        }
      }
      break;

    case 5: // Livello 6 - Diamante
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          int distFromCenter = abs(x - 8);
          if (y < 3) {
            if (distFromCenter <= 7 - y * 2) breakoutBricks[x][y] = true;
          } else {
            if (distFromCenter <= (y - 3) * 2 + 1) breakoutBricks[x][y] = true;
          }
        }
      }
      break;

    case 6: // Livello 7 - Croce
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          if (x >= 6 && x <= 9) breakoutBricks[x][y] = true;
          else if (y >= 2 && y <= 3) breakoutBricks[x][y] = true;
        }
      }
      break;

    case 7: // Livello 8 - Cornice
      for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 6; y++) {
          if (x == 0 || x == 15 || y == 0 || y == 5) {
            breakoutBricks[x][y] = true;
          }
        }
      }
      break;
  }

  // Conta i mattoni iniziali del livello
  breakoutBricksCount = 0;
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 6; y++) {
      if (breakoutBricks[x][y]) {
        breakoutBricksCount++;
      }
    }
  }

  forceBreakoutRedraw();  // Forza ridisegno per nuovo livello
}

void breakoutNextLevel() {
  breakoutLevel++;
  showGameHUD(breakoutScore, breakoutLives, breakoutLevel, true);
  breakoutLevelComplete = false;

  // Aumenta velocità ogni 2 livelli (minimo 70ms invece di 50ms per compensare la velocità ridotta)
  if (breakoutLevel % 2 == 0 && breakoutSpeed > 70) {
    breakoutSpeed -= 10;
  }

  initBreakoutLevel(breakoutLevel);
}

void breakoutMovePaddle(int dir) {
  if (!breakoutGameActive) return;
  if (hudOverlayActive) return;  // Non muovere durante visualizzazione HUD

  breakoutPaddleX += dir;
  if (breakoutPaddleX < 0) breakoutPaddleX = 0;
  if (breakoutPaddleX > 12) breakoutPaddleX = 12;

  // Se la palla è sulla racchetta, la muove insieme al paddle
  if (breakoutBallOnPaddle) {
    breakoutBallPosX = breakoutPaddleX + 1.5;  // Centro del paddle
    breakoutBallX = (int)round(breakoutBallPosX);

    // Lancia la pallina quando si muove la racchetta
    breakoutBallOnPaddle = false;
    breakoutBallVelX = (dir > 0) ? 0.8 : -0.8;  // Direzione basata sul movimento
    breakoutBallVelY = -1.0;
    breakoutBallDirX = (dir > 0) ? 1 : -1;
    breakoutBallDirY = -1;
  }
}

void updateBreakout() {
  // Gestione GAME OVER
  if (breakoutGameOver) {
    unsigned long currentTime = millis();

    // Mostra messaggio "GAME OVER" per 3 secondi
    if (currentTime - breakoutGameOverTime < 3000) {
      // Pulisce COMPLETAMENTE la matrice prima di disegnare
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
      }

      // Mostra "GAME" centrato con 1 pixel di gap tra le lettere
      drawCharacter('G', 0, 3, CRGB(255, 0, 0));
      drawCharacter('A', 4, 3, CRGB(255, 0, 0));
      drawCharacter('M', 8, 3, CRGB(255, 0, 0));
      drawCharacter('E', 12, 3, CRGB(255, 0, 0));

      // Mostra "OVER" centrato sotto con 1 pixel di gap tra le lettere
      drawCharacter('O', 0, 10, CRGB(255, 0, 0));
      drawCharacter('V', 4, 10, CRGB(255, 0, 0));
      drawCharacter('E', 8, 10, CRGB(255, 0, 0));
      drawCharacter('R', 12, 10, CRGB(255, 0, 0));

      FastLED.show();
      return;
    } else {
      // Dopo 3 secondi, mantieni solo il messaggio GAME OVER senza ridisegnare
      breakoutGameOver = false;
      // Non chiamare drawBreakout() per evitare di mostrare la racchetta
      return;
    }
  }

  if (!breakoutGameActive) return;

  // Gestione completamento livello
  if (breakoutLevelComplete) {
    unsigned long currentTime = millis();

    // Mostra messaggio "LEVEL X" per 2 secondi
    if (currentTime - breakoutLevelCompleteTime < 2000) {
      clearMatrix();

      // Mostra "LEVEL" centrato
      drawCharacter('L', 0, 3, CRGB(0, 255, 0));
      drawCharacter('E', 3, 3, CRGB(0, 255, 0));
      drawCharacter('V', 6, 3, CRGB(0, 255, 0));
      drawCharacter('E', 9, 3, CRGB(0, 255, 0));
      drawCharacter('L', 12, 3, CRGB(0, 255, 0));

      // Mostra numero livello centrato sotto
      String levelStr = String(breakoutLevel);
      int xPos = 8 - (levelStr.length() * 2);
      for (unsigned int i = 0; i < levelStr.length(); i++) {
        drawCharacter(levelStr.charAt(i), xPos + i * 4, 10, CRGB(255, 255, 0));
      }

      FastLED.show();
      return;
    } else {
      // Passa al livello successivo
      breakoutNextLevel();
    }
  }

  unsigned long currentTime = millis();
  if (currentTime - breakoutLastUpdate < breakoutSpeed) return;
  breakoutLastUpdate = currentTime;

  // Se la palla è sulla racchetta, tienila ferma
  if (breakoutBallOnPaddle) {
    breakoutBallPosX = breakoutPaddleX + 1.5;  // Centro del paddle
    breakoutBallPosY = 14.0;
    breakoutBallX = (int)round(breakoutBallPosX);
    breakoutBallY = 14;
    drawBreakout();
    return;  // Non muovere la palla
  }

  // Salva posizione precedente FLOAT per controllo percorso
  float prevBallPosX = breakoutBallPosX;
  float prevBallPosY = breakoutBallPosY;
  int prevBallX = breakoutBallX;
  int prevBallY = breakoutBallY;

  // FISICA MIGLIORATA - Muovi palla con velocità frazionarie
  breakoutBallPosX += breakoutBallVelX;
  breakoutBallPosY += breakoutBallVelY;

  // Converti posizioni float in int per rendering e collisioni
  breakoutBallX = (int)round(breakoutBallPosX);
  breakoutBallY = (int)round(breakoutBallPosY);

  // Rimbalzo sui bordi laterali con fisica migliorata
  if (breakoutBallPosX < 0) {
    breakoutBallPosX = 0;
    breakoutBallVelX = -breakoutBallVelX;
    breakoutBallDirX = (breakoutBallVelX > 0) ? 1 : -1;
  }
  if (breakoutBallPosX > 15) {
    breakoutBallPosX = 15;
    breakoutBallVelX = -breakoutBallVelX;
    breakoutBallDirX = (breakoutBallVelX > 0) ? 1 : -1;
  }

  // Rimbalzo sul bordo superiore con fisica migliorata
  if (breakoutBallPosY < 0) {
    breakoutBallPosY = 0;
    breakoutBallVelY = -breakoutBallVelY;
    breakoutBallDirY = (breakoutBallVelY > 0) ? 1 : -1;
  }

  // Controllo collisione con paddle - FISICA MIGLIORATA
  if (breakoutBallY >= 14 && breakoutBallY <= 15 && breakoutBallVelY > 0) {
    if (breakoutBallX >= breakoutPaddleX && breakoutBallX <= breakoutPaddleX + 3) {
      playBeep(); // Suono rimbalzo su paddle
      // Calcola punto di impatto relativo sulla racchetta (0.0 = estremo sinistro, 1.0 = estremo destro)
      float hitPosition = (breakoutBallPosX - breakoutPaddleX) / 3.0;

      // Normalizza tra -1.0 e 1.0 (0 = centro)
      float hitOffset = (hitPosition - 0.5) * 2.0;

      // Calcola nuovo angolo: centro = verticale, bordi = angoli estremi (fino a ±75°)
      // Velocità orizzontale proporzionale alla distanza dal centro
      breakoutBallVelX = hitOffset * 1.1; // Massimo ±1.1 (ridotto)

      // Velocità verticale sempre verso l'alto, maggiore al centro
      breakoutBallVelY = -1.2 + abs(hitOffset) * 0.2; // Da -1.2 (centro) a -1.0 (bordi) - ridotto

      // Normalizza per mantenere velocità totale costante (~1.3) - ridotto
      float totalSpeed = sqrt(breakoutBallVelX * breakoutBallVelX + breakoutBallVelY * breakoutBallVelY);
      if (totalSpeed > 0.1) {
        breakoutBallVelX = (breakoutBallVelX / totalSpeed) * 1.3;
        breakoutBallVelY = (breakoutBallVelY / totalSpeed) * 1.3;
      }

      // Assicura velocità minima verticale per evitare loop orizzontali
      if (abs(breakoutBallVelY) < 0.3) {
        breakoutBallVelY = (breakoutBallVelY < 0) ? -0.3 : 0.3;
      }

      // Aggiorna direzioni per compatibilità
      breakoutBallDirX = (breakoutBallVelX > 0) ? 1 : -1;
      breakoutBallDirY = -1;

      // Riposiziona pallina sopra la racchetta
      breakoutBallPosY = 14;
      breakoutBallY = 14;
    }
  }

  // Palla persa
  if (breakoutBallY > 15) {
    breakoutLives--;
    playGameOver(); // Suono morte sempre quando palla esce
    if (breakoutLives <= 0) {
      // GAME OVER - non mostrare HUD, mostra direttamente GAME OVER
      breakoutGameActive = false;
      breakoutGameOver = true;
      breakoutGameOverTime = millis();
      hudOverlayActive = false;  // Disattiva HUD per mostrare GAME OVER
      clearMatrix();  // Pulisce la matrice subito
      FastLED.show();
      return;  // Esce subito senza disegnare, il GAME OVER verrà mostrato al prossimo ciclo
    } else {
      // Ancora vite rimaste - mostra HUD per 5 secondi
      showGameHUD(breakoutScore, breakoutLives, breakoutLevel, true);
      // Rimetti la palla sulla racchetta - aspetta input giocatore per ripartire
      breakoutBallPosX = breakoutPaddleX + 1.5;
      breakoutBallPosY = 14.0;
      breakoutBallX = (int)round(breakoutBallPosX);
      breakoutBallY = 14;
      breakoutBallDirX = 1;
      breakoutBallDirY = -1;
      breakoutBallVelX = 0.8;
      breakoutBallVelY = -1.0;
      breakoutBallOnPaddle = true;  // Palla ferma sulla racchetta, parte quando giocatore muove
      forceBreakoutRedraw();  // Forza ridisegno dopo perdita vita
      return;  // Esce per mostrare HUD, non chiamare drawBreakout() che resetterebbe breakoutFullRedraw
    }
  }

  // *** CONTROLLO COLLISIONE CON MATTONI - ALGORITMO ROBUSTO ***
  // Controlla TUTTO il percorso della pallina per evitare che "salti attraverso" i mattoni
  bool brickHit = false;
  int hitBrickX = -1;
  int hitBrickY = -1;

  // Calcola tutti i punti del percorso usando interpolazione lineare
  int numSteps = (int)(max(abs(breakoutBallPosX - prevBallPosX), abs(breakoutBallPosY - prevBallPosY)) * 2) + 1;
  if (numSteps < 2) numSteps = 2;

  for (int step = 0; step <= numSteps && !brickHit; step++) {
    float t = (float)step / (float)numSteps;
    float checkX = prevBallPosX + (breakoutBallPosX - prevBallPosX) * t;
    float checkY = prevBallPosY + (breakoutBallPosY - prevBallPosY) * t;

    int cellX = (int)round(checkX);
    int cellY = (int)round(checkY);

    // Verifica se la posizione è nell'area dei mattoni
    if (cellY >= 0 && cellY < 6 && cellX >= 0 && cellX < 16) {
      if (breakoutBricks[cellX][cellY]) {
        // MATTONE TROVATO!
        brickHit = true;
        hitBrickX = cellX;
        hitBrickY = cellY;
        break;
      }
    }
  }

  // Se NON è stato trovato un mattone nel percorso, controlla anche la posizione finale
  if (!brickHit && breakoutBallY >= 0 && breakoutBallY < 6 && breakoutBallX >= 0 && breakoutBallX < 16) {
    if (breakoutBricks[breakoutBallX][breakoutBallY]) {
      brickHit = true;
      hitBrickX = breakoutBallX;
      hitBrickY = breakoutBallY;
    }
  }

  // Se è stato colpito un mattone, gestisci la collisione
  if (brickHit && hitBrickX >= 0 && hitBrickY >= 0) {
    // Determina direzione di collisione in base alla velocità
    bool hitFromSide = false;
    bool hitFromTopBottom = false;

    // Analizza la direzione di movimento per determinare il tipo di collisione
    if (abs(breakoutBallVelX) > abs(breakoutBallVelY) * 0.7) {
      // Movimento prevalentemente orizzontale
      hitFromSide = true;
    }
    if (abs(breakoutBallVelY) > abs(breakoutBallVelX) * 0.7) {
      // Movimento prevalentemente verticale
      hitFromTopBottom = true;
    }

    // Se la pallina si muove in diagonale, controlla i mattoni adiacenti
    if (!hitFromSide && !hitFromTopBottom) {
      // Movimento diagonale bilanciato - controlla da dove arriva
      bool leftClear = (hitBrickX > 0) ? !breakoutBricks[hitBrickX - 1][hitBrickY] : true;
      bool rightClear = (hitBrickX < 15) ? !breakoutBricks[hitBrickX + 1][hitBrickY] : true;
      bool topClear = (hitBrickY > 0) ? !breakoutBricks[hitBrickX][hitBrickY - 1] : true;
      bool bottomClear = (hitBrickY < 5) ? !breakoutBricks[hitBrickX][hitBrickY + 1] : true;

      if ((breakoutBallVelX > 0 && leftClear) || (breakoutBallVelX < 0 && rightClear)) {
        hitFromSide = true;
      }
      if ((breakoutBallVelY > 0 && topClear) || (breakoutBallVelY < 0 && bottomClear)) {
        hitFromTopBottom = true;
      }

      // Fallback: se ancora non determinato, usa la velocità
      if (!hitFromSide && !hitFromTopBottom) {
        hitFromTopBottom = true; // Default: rimbalzo verticale
      }
    }

    // FISICA MIGLIORATA - Inverti velocità in base al tipo di collisione
    if (hitFromSide && hitFromTopBottom) {
      // Collisione angolare - inverti entrambe
      breakoutBallVelX = -breakoutBallVelX;
      breakoutBallVelY = -breakoutBallVelY;
    } else if (hitFromSide) {
      // Colpito dal lato - inverti X
      breakoutBallVelX = -breakoutBallVelX;
    } else {
      // Colpito da sopra/sotto - inverti Y
      breakoutBallVelY = -breakoutBallVelY;
    }

    // Aggiorna direzioni
    breakoutBallDirX = (breakoutBallVelX > 0) ? 1 : -1;
    breakoutBallDirY = (breakoutBallVelY > 0) ? 1 : -1;

    // Riposiziona la pallina alla posizione precedente per evitare compenetrazioni
    breakoutBallX = prevBallX;
    breakoutBallY = prevBallY;
    breakoutBallPosX = prevBallPosX;
    breakoutBallPosY = prevBallPosY;

    // *** DISTRUGGI IL MATTONE - GARANTITO AL 100% ***
    breakoutBricks[hitBrickX][hitBrickY] = false;
    breakoutScore += 1;  // Ridotto da 10 a 1 per mantenere punteggio a 3 cifre
    playBeep();

    // Forza ridisegno completo
    forceBreakoutRedraw();

    // Conta mattoni rimasti
    breakoutBricksCount = 0;
    for (int x = 0; x < 16; x++) {
      for (int y = 0; y < 6; y++) {
        if (breakoutBricks[x][y]) {
          breakoutBricksCount++;
        }
      }
    }

    // Controlla vittoria livello
    if (breakoutBricksCount == 0) {
      breakoutLevelComplete = true;
      breakoutLevelCompleteTime = millis();
      playLevelUp();
    }

    // AIUTO AUTOMATICO: ultimi 10 mattoni più facili da colpire
    if (breakoutBricksCount <= 10 && breakoutBricksCount > 0) {
      // Angoli più verticali per facilitare il gioco
      if (abs(breakoutBallVelY) < 0.7) {
        breakoutBallVelY = (breakoutBallVelY < 0) ? -0.7 : 0.7;
      }
      if (abs(breakoutBallVelX) > 0.7) {
        breakoutBallVelX = breakoutBallVelX * 0.6;
      }
    }
  }

  drawBreakout();
}

// Funzione helper per ottenere il colore del mattone in base al livello e alla riga
CRGB getBreakoutBrickColor(int level, int y) {
  // 8 schemi di colore diversi che si ripetono
  switch ((level - 1) % 8) {
    case 0: // Livello 1 - Classico: Rosso, Giallo, Verde
      if (y < 2) return CRGB(255, 0, 0);
      else if (y < 4) return CRGB(255, 255, 0);
      else return CRGB(0, 255, 0);

    case 1: // Livello 2 - Oceano: Blu, Ciano, Bianco
      if (y < 2) return CRGB(0, 0, 255);
      else if (y < 4) return CRGB(0, 255, 255);
      else return CRGB(255, 255, 255);

    case 2: // Livello 3 - Tramonto: Magenta, Arancione, Rosa
      if (y < 2) return CRGB(255, 0, 255);
      else if (y < 4) return CRGB(255, 128, 0);
      else return CRGB(255, 100, 150);

    case 3: // Livello 4 - Foresta: Verde scuro, Verde, Verde chiaro
      if (y < 2) return CRGB(0, 128, 0);
      else if (y < 4) return CRGB(0, 255, 0);
      else return CRGB(150, 255, 150);

    case 4: // Livello 5 - Fuoco: Rosso, Arancione, Giallo
      if (y < 2) return CRGB(255, 0, 0);
      else if (y < 4) return CRGB(255, 100, 0);
      else return CRGB(255, 200, 0);

    case 5: // Livello 6 - Ghiaccio: Bianco, Ciano, Blu
      if (y < 2) return CRGB(255, 255, 255);
      else if (y < 4) return CRGB(100, 200, 255);
      else return CRGB(0, 100, 255);

    case 6: // Livello 7 - Neon: Verde, Rosa, Viola
      if (y < 2) return CRGB(0, 255, 100);
      else if (y < 4) return CRGB(255, 0, 150);
      else return CRGB(150, 0, 255);

    case 7: // Livello 8 - Arcobaleno: ogni riga colore diverso
      switch (y) {
        case 0: return CRGB(255, 0, 0);     // Rosso
        case 1: return CRGB(255, 128, 0);   // Arancione
        case 2: return CRGB(255, 255, 0);   // Giallo
        case 3: return CRGB(0, 255, 0);     // Verde
        case 4: return CRGB(0, 150, 255);   // Azzurro
        case 5: return CRGB(150, 0, 255);   // Viola
        default: return CRGB(255, 255, 255);
      }

    default:
      return CRGB(0, 255, 0);
  }
}

void drawBreakout() {
  // Ridisegno completo solo se necessario (inizio gioco, mattone distrutto, ecc.)
  if (breakoutFullRedraw) {
    clearMatrixNoShow();

    // Disegna tutti i mattoni con colori basati sul livello
    for (int x = 0; x < 16; x++) {
      for (int y = 0; y < 6; y++) {
        if (breakoutBricks[x][y]) {
          CRGB color = getBreakoutBrickColor(breakoutLevel, y);
          setPixel(x, y, color);
        }
      }
    }

    breakoutFullRedraw = false;
  } else {
    // Cancella posizione precedente del paddle
    if (breakoutPrevPaddleX >= 0) {
      for (int i = 0; i < 4; i++) {
        int px = breakoutPrevPaddleX + i;
        if (px >= 0 && px < 16) {
          setPixel(px, 15, CRGB(0, 0, 0));
        }
      }
    }

    // Cancella posizione precedente della palla
    if (breakoutPrevBallX >= 0 && breakoutPrevBallY >= 0) {
      // Controlla se c'era un mattone in quella posizione (per ridisegnarlo)
      if (breakoutPrevBallY < 6 && breakoutBricks[breakoutPrevBallX][breakoutPrevBallY]) {
        CRGB color = getBreakoutBrickColor(breakoutLevel, breakoutPrevBallY);
        setPixel(breakoutPrevBallX, breakoutPrevBallY, color);
      } else {
        setPixel(breakoutPrevBallX, breakoutPrevBallY, CRGB(0, 0, 0));
      }
    }
  }

  // Disegna paddle nella nuova posizione (bianco)
  for (int i = 0; i < 4; i++) {
    setPixel(breakoutPaddleX + i, 15, CRGB(255, 255, 255));
  }

  // Disegna palla nella nuova posizione (cyan)
  setPixel(breakoutBallX, breakoutBallY, CRGB(0, 255, 255));

  // Salva posizioni per il prossimo frame
  breakoutPrevPaddleX = breakoutPaddleX;
  breakoutPrevBallX = breakoutBallX;
  breakoutPrevBallY = breakoutBallY;

  FastLED.show();
}

void forceBreakoutRedraw() {
  breakoutFullRedraw = true;
}

void handleBreakout() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Breakout</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#1a1a1a;padding:20px;border-radius:10px;border:1px solid #333;}";
  html += ".stats{display:flex;justify-content:space-around;margin:20px;font-size:24px;}";
  html += ".gamepad{display:grid;grid-template-columns:repeat(3,80px);gap:10px;margin:20px auto;justify-content:center;}";
  html += ".gamepad button{height:80px;font-size:24px;border:none;border-radius:10px;background:#444;color:white;cursor:pointer;}";
  html += ".gamepad button:active{background:#666;}";
  html += ".reset-btn{padding:15px 30px;background:#4CAF50;color:white;border:none;border-radius:10px;font-size:18px;margin:20px;cursor:pointer;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{color:#fff;}";
  html += "</style>";
  html += "<script>";
  html += "let moveInterval=null;";
  html += "function reset(){fetch('/breakoutcontrol?reset=1');updateStats();}";
  html += "function move(dir){fetch('/breakoutcontrol?move='+dir);}";
  html += "function startMove(dir){move(dir);if(moveInterval)clearInterval(moveInterval);moveInterval=setInterval(()=>move(dir),80);}";
  html += "function stopMove(){if(moveInterval){clearInterval(moveInterval);moveInterval=null;}}";
  html += "function updateStats(){fetch('/breakoutcontrol?stats=1').then(r=>r.json()).then(d=>{document.getElementById('score').innerText=d.score;document.getElementById('lives').innerText=d.lives;document.getElementById('level').innerText=d.level;document.getElementById('status').innerText=d.active?'🟢 In gioco':'🔴 Game Over';});}";
  html += "setInterval(updateStats,500);";
  html += "let keys={};";
  html += "document.addEventListener('keydown',function(e){if(keys[e.key])return;keys[e.key]=true;if(e.key=='ArrowLeft')startMove('left');else if(e.key=='ArrowRight')startMove('right');});";
  html += "document.addEventListener('keyup',function(e){delete keys[e.key];stopMove();});";
  html += "window.addEventListener('DOMContentLoaded',function(){";
  html += "document.querySelectorAll('.gamepad button').forEach(b=>{";
  html += "b.addEventListener('touchstart',function(e){e.preventDefault();let d=this.dataset.dir;if(d)startMove(d);},false);";
  html += "b.addEventListener('touchend',function(e){e.preventDefault();stopMove();},false);";
  html += "b.addEventListener('mousedown',function(e){e.preventDefault();let d=this.dataset.dir;if(d)startMove(d);},false);";
  html += "b.addEventListener('mouseup',function(e){e.preventDefault();stopMove();},false);";
  html += "b.addEventListener('mouseleave',function(e){stopMove();},false);";
  html += "});});";
  html += "function enableBT(){window.location='/enableBluetooth?game=breakout';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a> <a href='#' onclick='enableBT()' style='background:#2196F3;margin-left:10px;'>🎮 Bluetooth</a></div>";
  html += "<div class='container'>";
  html += "<h1>🧱 Breakout</h1>";
  html += "<div class='stats'>";
  html += "<div>Livello: <span id='level' style='color:#ff0;'>1</span></div>";
  html += "<div>Punteggio: <span id='score' style='color:#0ff;'>0</span></div>";
  html += "<div>Vite: <span id='lives' style='color:#f00;'>5</span></div>";
  html += "</div>";
  html += "<div id='status' style='font-size:20px;margin:10px;'>🟢 In gioco</div>";
  html += "<button class='reset-btn' onclick='reset()'>🔄 Nuova Partita</button>";
  html += "<div class='gamepad'>";
  html += "<div></div><div></div><div></div>";
  html += "<button data-dir='left'>⬅️</button>";
  html += "<div></div>";
  html += "<button data-dir='right'>➡️</button>";
  html += "</div>";
  html += "<p style='color:#aaa;margin-top:20px;'>Usa frecce sinistra/destra per muovere il paddle</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleBreakoutControl() {
  if (server.hasArg("reset")) {
    resetBreakout();
    changeState(STATE_GAME_BREAKOUT);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("move")) {
    String dir = server.arg("move");
    if (dir == "left") breakoutMovePaddle(-1);
    else if (dir == "right") breakoutMovePaddle(1);
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("stats")) {
    String json = "{\"score\":" + String(breakoutScore) + ",\"lives\":" + String(breakoutLives) + ",\"level\":" + String(breakoutLevel) + ",\"active\":" + (breakoutGameActive ? "true" : "false") + "}";
    server.send(200, "application/json", json);
    return;
  }

  server.send(400, "text/plain", "Bad Request");
}

// ============================================
// TESTO SCORREVOLE
// ============================================
void handleTextScroll() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Testo Scorrevole</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:500px;margin:0 auto;background:#1a1a1a;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(255,255,255,0.1);border:1px solid #333;}";
  html += "input{padding:12px;width:100%;margin:10px 0;border:1px solid #444;border-radius:5px;font-size:16px;box-sizing:border-box;background:#222;color:#fff;}";
  html += "button{padding:12px 24px;margin:5px;background:#2196F3;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;}";
  html += "button:hover{background:#1976D2;}";
  html += ".speed-controls,.color-controls,.size-controls{margin:20px 0;padding:15px;background:#222;border-radius:8px;}";
  html += ".color-btn{padding:10px 15px;margin:3px;min-width:70px;border:3px solid transparent;}"
       ".color-btn.selected{border:3px solid #fff;box-shadow:0 0 10px #fff;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1,h3,p{color:#fff;}";
  html += "input[type='range']{width:100%;margin:10px 0;}";
  html += ".slider-value{display:inline-block;margin-left:10px;color:#2196F3;font-weight:bold;}";
  html += "</style>";
  html += "<script>";
  html += "let selectedColor=" + String(scrollTextColor) + ";";
  html += "let selectedSize=" + String(scrollTextSize) + ";";
  html += "function updateText(){";
  html += "let text=document.getElementById('textInput').value;";
  html += "if(text.trim()!=''){";
  html += "fetch('/updateText?text='+encodeURIComponent(text)+'&color='+selectedColor+'&size='+selectedSize).then(r=>console.log('Text updated'));";
  html += "}";
  html += "}";
  html += "function changeSpeed(speed){";
  html += "fetch('/updateText?speed='+speed).then(r=>console.log('Speed:'+speed));";
  html += "}";
  html += "function changeColor(color){";
  html += "selectedColor=color;";
  html += "document.querySelectorAll('.color-btn').forEach(b=>b.classList.remove('selected'));";
  html += "var btn=document.querySelector('.color-btn[data-color=\"'+color+'\"]');if(btn)btn.classList.add('selected');";
  html += "fetch('/updateText?color='+color).then(r=>{console.log('Color:'+color);location.reload();});";
  html += "}";
  html += "function changeSize(size){";
  html += "selectedSize=size;";
  html += "let sizeName=['Piccolo','Medio','Grande'][size];";
  html += "document.getElementById('sizeValue').innerText=sizeName;";
  html += "fetch('/updateText?size='+size).then(r=>console.log('Size:'+size));";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>✨ Testo Scorrevole</h1>";
  html += "<p>Scrivi il testo da far scorrere sulla matrice LED:</p>";
  html += "<input type='text' id='textInput' value='CONSOLE QUADRA' placeholder='Inserisci il testo qui...'>";
  html += "<button onclick='updateText()'>📝 Aggiorna Testo</button>";
  html += "<div class='color-controls'>";
  html += "<h3>🎨 Colore testo:</h3>";
  html += "<button class='color-btn"; if(scrollTextColor==0) html+=" selected"; html+="' data-color='0' onclick='changeColor(0)' style='background:#f44336;'>Rosso</button>";
  html += "<button class='color-btn"; if(scrollTextColor==1) html+=" selected"; html+="' data-color='1' onclick='changeColor(1)' style='background:#4CAF50;'>Verde</button>";
  html += "<button class='color-btn"; if(scrollTextColor==2) html+=" selected"; html+="' data-color='2' onclick='changeColor(2)' style='background:#2196F3;'>Blu</button>";
  html += "<button class='color-btn"; if(scrollTextColor==3) html+=" selected"; html+="' data-color='3' onclick='changeColor(3)' style='background:#FFEB3B;color:#000;'>Giallo</button>";
  html += "<button class='color-btn"; if(scrollTextColor==4) html+=" selected"; html+="' data-color='4' onclick='changeColor(4)' style='background:#00BCD4;'>Ciano</button>";
  html += "<button class='color-btn"; if(scrollTextColor==5) html+=" selected"; html+="' data-color='5' onclick='changeColor(5)' style='background:#E91E63;'>Magenta</button>";
  html += "<button class='color-btn"; if(scrollTextColor==6) html+=" selected"; html+="' data-color='6' onclick='changeColor(6)' style='background:#FFF;color:#000;'>Bianco</button>";
  html += "<button class='color-btn"; if(scrollTextColor==7) html+=" selected"; html+="' data-color='7' onclick='changeColor(7)' style='background:#FF9800;'>Arancio</button>";
  html += "<button class='color-btn"; if(scrollTextColor==8) html+=" selected"; html+="' data-color='8' onclick='changeColor(8)' style='background:linear-gradient(90deg,red,orange,yellow,green,blue,indigo,violet);'>Rainbow</button>";
  html += "</div>";
  html += "<div class='size-controls'>";
  html += "<h3>📏 Dimensione testo: <span id='sizeValue' class='slider-value'>Piccolo</span></h3>";
  html += "<button onclick='changeSize(0)'>Piccolo</button>";
  html += "<button onclick='changeSize(1)'>Medio</button>";
  html += "<button onclick='changeSize(2)'>Grande</button>";
  html += "</div>";
  html += "<div class='speed-controls'>";
  html += "<h3>⚡ Velocità di scorrimento:</h3>";
  html += "<button onclick='changeSpeed(100)'>🐢 Lento</button>";
  html += "<button onclick='changeSpeed(50)'>🚶 Normale</button>";
  html += "<button onclick='changeSpeed(20)'>🐇 Veloce</button>";
  html += "</div>";
  html += "<p style='color:#aaa;margin-top:30px;'>Il testo inizierà a scorrere automaticamente sulla matrice LED.</p>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleUpdateText() {
  bool needSave = false;
  bool needStateChange = false;

  // Gestisce testo (può arrivare insieme a colore e size)
  if (server.hasArg("text")) {
    scrollText = server.arg("text");
    needStateChange = true;
  }

  // Gestisce colore (può arrivare da solo o insieme al testo)
  if (server.hasArg("color")) {
    int newColor = server.arg("color").toInt();
    Serial.print("[TEXT] Color change: ");
    Serial.print(scrollTextColor);
    Serial.print(" -> ");
    Serial.println(newColor);
    scrollTextColor = newColor;
    needSave = true;
    needStateChange = true;
  }

  // Gestisce dimensione (può arrivare da solo o insieme al testo)
  if (server.hasArg("size")) {
    scrollTextSize = server.arg("size").toInt();
    needSave = true;
    needStateChange = true;
  }

  // Gestisce velocità (sempre da solo)
  if (server.hasArg("speed")) {
    scrollSpeed = server.arg("speed").toInt();
    needSave = true;
  }

  // Salva config se necessario
  if (needSave) {
    saveConfig();
  }

  // Cambia stato se necessario
  if (needStateChange) {
    ipScrollActive = false;  // IMPORTANTE: disattiva IP scroll per usare il colore selezionato
    changeState(STATE_GAME_TEXT_SCROLL);
    scrollPosition = MATRIX_WIDTH;
    lastScrollUpdate = millis();
  }

  // Risposta
  if (server.hasArg("text") || server.hasArg("color") || server.hasArg("size") || server.hasArg("speed")) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Parametro mancante");
  }
}

// ============================================
// OROLOGIO DIGITALE
// ============================================
void handleClock() {
  // Usa chunked transfer per evitare problemi di memoria con HTML grande
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  // Invia HTML in blocchi per evitare frammentazione memoria e blocco WiFi
  server.sendContent("<!DOCTYPE html><html><head>");
  server.sendContent("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  server.sendContent("<meta charset='UTF-8'>");
  server.sendContent("<title>Orologio Digitale</title>");
  server.sendContent("<style>");
  server.sendContent("body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}");
  server.sendContent(".container{max-width:500px;margin:0 auto;background:#1a1a1a;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(255,255,255,0.1);border:1px solid #333;}");
  server.sendContent(".time-display{font-size:48px;font-family:monospace;margin:20px 0;color:#0f0;text-shadow:0 0 15px #0f0;}");
  server.sendContent(".timezone-input{padding:10px;margin:8px;width:80%;border:1px solid #444;border-radius:5px;background:#222;color:#fff;font-size:14px;}");
  server.sendContent("button{padding:12px 24px;margin:8px;background:#4CAF50;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;}");
  server.sendContent("button:hover{background:#45a049;}");
  server.sendContent(".color-buttons{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:15px 0;max-width:350px;margin-left:auto;margin-right:auto;}");
  server.sendContent(".type-buttons{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin:15px 0;max-width:350px;margin-left:auto;margin-right:auto;}");
  server.sendContent(".color-btn{padding:12px;border:2px solid #444;border-radius:8px;cursor:pointer;font-size:13px;font-weight:bold;}");
  server.sendContent(".color-btn.selected{border:3px solid #fff;box-shadow:0 0 10px rgba(255,255,255,0.5);}");
  server.sendContent("@media(max-width:400px){.type-buttons{grid-template-columns:1fr;}.color-buttons{grid-template-columns:repeat(3,1fr);gap:5px;}.color-btn{padding:8px;font-size:11px;}}");
  server.sendContent(".nav{text-align:center;margin-bottom:20px;}");
  server.sendContent(".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}");
  server.sendContent("h1{color:#fff;font-size:28px;margin:10px 0;}");
  server.sendContent("h3{color:#fff;font-size:16px;margin:15px 0 5px 0;}");
  server.sendContent("p{color:#fff;}");
  server.sendContent("</style>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<script>");
  server.sendContent("let currentColor=" + String(clockColorMode) + ";");
  server.sendContent("let currentSecondsColor=" + String(secondsLedColorMode) + ";");
  server.sendContent("let currentDisplayType=" + String(clockDisplayType) + ";");
  server.sendContent("let autoSwitchEnabled=" + String(clockWeatherAutoSwitch ? "true" : "false") + ";");
  server.sendContent("let clockInterval=" + String(clockDisplayInterval) + ";");
  server.sendContent("let weatherInterval=" + String(weatherDisplayInterval) + ";");
  server.sendContent("let dateDisplayEnabled=" + String(dateDisplayEnabled ? "true" : "false") + ";");
  server.sendContent("let dateInterval=" + String(dateDisplayInterval) + ";");
  server.sendContent("let dateColorMode=" + String(dateColorMode) + ";");
  server.sendContent("let dateDisplaySize=" + String(dateDisplaySize) + ";");
  server.sendContent("let displaySequence=" + String(displaySequence) + ";");
  server.sendContent("let localSensorEnabled=" + String(localSensorDisplayEnabled ? "true" : "false") + ";");
  server.sendContent("let localSensorInterval=" + String(localSensorDisplayInterval) + ";");
  server.sendContent("let matrixLedEnabled=" + String(matrixLedEnabled ? "true" : "false") + ";");
  server.sendContent("let nightShutdownEnabled=" + String(nightShutdownEnabled ? "true" : "false") + ";");
  server.sendContent("let dayShutdownEnabled=" + String(dayShutdownEnabled ? "true" : "false") + ";");
  server.sendContent("let dayShutdownDays=" + String(dayShutdownDays) + ";");
  server.sendContent("function syncNTP(){");
  server.sendContent("let tz=document.getElementById('timezone').value;");
  server.sendContent("fetch('/setClock?sync=1&tz='+encodeURIComponent(tz)+'&color='+currentColor).then(r=>r.text()).then(data=>{");
  server.sendContent("alert(data);updateClock();});");
  server.sendContent("}");
  yield(); // Permette al WiFi di processare
  server.sendContent("function setColor(colorIndex){");
  server.sendContent("currentColor=colorIndex;");
  server.sendContent("document.querySelectorAll('.color-btn').forEach(btn=>btn.classList.remove('selected'));");
  server.sendContent("document.getElementById('color'+colorIndex).classList.add('selected');");
  server.sendContent("fetch('/setClock?color='+colorIndex+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setSecondsColor(colorIndex){");
  server.sendContent("currentSecondsColor=colorIndex;");
  server.sendContent("document.querySelectorAll('.seconds-color-btn').forEach(btn=>btn.classList.remove('selected'));");
  server.sendContent("document.getElementById('secColor'+colorIndex).classList.add('selected');");
  server.sendContent("fetch('/setClock?secondscolor='+colorIndex+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setDisplayType(typeIndex){");
  server.sendContent("currentDisplayType=typeIndex;");
  server.sendContent("document.querySelectorAll('.display-type-btn').forEach(btn=>btn.classList.remove('selected'));");
  server.sendContent("document.getElementById('type'+typeIndex).classList.add('selected');");
  server.sendContent("fetch('/setClock?displaytype='+typeIndex+'&apply=1');");
  server.sendContent("}");
  yield(); // Permette al WiFi di processare
  server.sendContent("function toggleAutoSwitch(){");
  server.sendContent("autoSwitchEnabled=document.getElementById('autoSwitch').checked;");
  server.sendContent("fetch('/setClock?autoswitch='+(autoSwitchEnabled?'1':'0')+'&apply=1').then(r=>r.text()).then(data=>{");
  server.sendContent("document.getElementById('clockIntervalInput').disabled=!autoSwitchEnabled;");
  server.sendContent("document.getElementById('weatherIntervalInput').disabled=!autoSwitchEnabled;");
  server.sendContent("});");
  server.sendContent("}");
  server.sendContent("function setClockInterval(){");
  server.sendContent("clockInterval=document.getElementById('clockIntervalInput').value;");
  server.sendContent("if(clockInterval<5)clockInterval=5;");
  server.sendContent("if(clockInterval>600)clockInterval=600;");
  server.sendContent("document.getElementById('clockIntervalInput').value=clockInterval;");
  server.sendContent("fetch('/setClock?clockinterval='+clockInterval+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setWeatherInterval(){");
  server.sendContent("weatherInterval=document.getElementById('weatherIntervalInput').value;");
  server.sendContent("if(weatherInterval<5)weatherInterval=5;");
  server.sendContent("if(weatherInterval>600)weatherInterval=600;");
  server.sendContent("document.getElementById('weatherIntervalInput').value=weatherInterval;");
  server.sendContent("fetch('/setClock?weatherinterval='+weatherInterval+'&apply=1');");
  server.sendContent("}");
  yield(); // Permette al WiFi di processare
  server.sendContent("function toggleDateDisplay(){");
  server.sendContent("dateDisplayEnabled=document.getElementById('dateDisplay').checked;");
  server.sendContent("fetch('/setClock?datedisplay='+(dateDisplayEnabled?'1':'0')+'&apply=1').then(r=>r.text()).then(data=>{");
  server.sendContent("document.getElementById('dateIntervalInput').disabled=!dateDisplayEnabled;");
  server.sendContent("});");
  server.sendContent("}");
  server.sendContent("function setDateInterval(){");
  server.sendContent("dateInterval=document.getElementById('dateIntervalInput').value;");
  server.sendContent("if(dateInterval<5)dateInterval=5;");
  server.sendContent("if(dateInterval>600)dateInterval=600;");
  server.sendContent("document.getElementById('dateIntervalInput').value=dateInterval;");
  server.sendContent("fetch('/setClock?dateinterval='+dateInterval+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setDateColor(colorIndex){");
  server.sendContent("dateColorMode=colorIndex;");
  server.sendContent("document.querySelectorAll('.date-color-btn').forEach(btn=>btn.classList.remove('selected'));");
  server.sendContent("document.getElementById('dateColor'+colorIndex).classList.add('selected');");
  server.sendContent("fetch('/setClock?datecolor='+colorIndex+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setDateSize(sizeIndex){");
  server.sendContent("dateDisplaySize=sizeIndex;");
  server.sendContent("document.querySelectorAll('.date-size-btn').forEach(btn=>btn.classList.remove('selected'));");
  server.sendContent("document.getElementById('dateSize'+sizeIndex).classList.add('selected');");
  server.sendContent("fetch('/setClock?datesize='+sizeIndex+'&apply=1');");
  server.sendContent("}");
  yield(); // Permette al WiFi di processare
  server.sendContent("function setDisplaySequence(){");
  server.sendContent("displaySequence=document.getElementById('sequenceSelect').value;");
  server.sendContent("fetch('/setClock?sequence='+displaySequence+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function toggleLocalSensor(){");
  server.sendContent("localSensorEnabled=document.getElementById('localSensorDisplay').checked;");
  server.sendContent("fetch('/setClock?localsensor='+(localSensorEnabled?'1':'0')+'&apply=1').then(r=>r.text()).then(data=>{");
  server.sendContent("document.getElementById('localSensorIntervalInput').disabled=!localSensorEnabled;");
  server.sendContent("});");
  server.sendContent("}");
  server.sendContent("function setLocalSensorInterval(){");
  server.sendContent("localSensorInterval=document.getElementById('localSensorIntervalInput').value;");
  server.sendContent("if(localSensorInterval<5)localSensorInterval=5;");
  server.sendContent("if(localSensorInterval>300)localSensorInterval=300;");
  server.sendContent("document.getElementById('localSensorIntervalInput').value=localSensorInterval;");
  server.sendContent("fetch('/setClock?localsensorinterval='+localSensorInterval+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setSensorOffset(){");
  server.sendContent("var v=parseFloat(document.getElementById('sensorTempOffset').value);");
  server.sendContent("if(isNaN(v))v=0;");
  server.sendContent("if(v<-5)v=-5;if(v>5)v=5;");
  server.sendContent("document.getElementById('sensorTempOffset').value=v.toFixed(1);");
  server.sendContent("fetch('/setClock?sensoroffset='+v).then(r=>r.text()).then(d=>{console.log('Offset saved:'+v);});");
  server.sendContent("}");
  server.sendContent("function toggleMatrixLed(){");
  server.sendContent("matrixLedEnabled=document.getElementById('matrixLedToggle').checked;");
  server.sendContent("fetch('/setClock?matrixled='+(matrixLedEnabled?'1':'0')+'&apply=1');");
  server.sendContent("}");
  yield(); // Permette al WiFi di processare
  server.sendContent("function toggleNightShutdown(){");
  server.sendContent("nightShutdownEnabled=document.getElementById('nightShutdown').checked;");
  server.sendContent("fetch('/setClock?nightshutdown='+(nightShutdownEnabled?'1':'0')+'&apply=1').then(r=>r.text()).then(data=>{");
  server.sendContent("document.getElementById('nightStartTime').disabled=!nightShutdownEnabled;");
  server.sendContent("document.getElementById('nightEndTime').disabled=!nightShutdownEnabled;");
  server.sendContent("});");
  server.sendContent("}");
  server.sendContent("function toggleDayShutdown(){");
  server.sendContent("dayShutdownEnabled=document.getElementById('dayShutdown').checked;");
  server.sendContent("fetch('/setClock?dayshutdown='+(dayShutdownEnabled?'1':'0')+'&apply=1').then(r=>r.text()).then(data=>{");
  server.sendContent("document.getElementById('dayStartTime').disabled=!dayShutdownEnabled;");
  server.sendContent("document.getElementById('dayEndTime').disabled=!dayShutdownEnabled;");
  server.sendContent("document.getElementById('dayShutdownDaysRow').style.opacity=dayShutdownEnabled?'1':'0.4';");
  server.sendContent("for(let i=0;i<7;i++)document.getElementById('dsd'+i).style.pointerEvents=dayShutdownEnabled?'auto':'none';");
  server.sendContent("});");
  server.sendContent("}");
  server.sendContent("function toggleDayShutdownDay(bit){");
  server.sendContent("dayShutdownDays^=(1<<bit);");
  server.sendContent("document.getElementById('dsd'+bit).classList.toggle('active');");
  server.sendContent("fetch('/setClock?dayshutdowndays='+dayShutdownDays+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function setNightTime(){");
  server.sendContent("let start=document.getElementById('nightStartTime').value.split(':');");
  server.sendContent("let end=document.getElementById('nightEndTime').value.split(':');");
  server.sendContent("fetch('/setClock?nightstart='+start[0]+':'+start[1]+'&nightend='+end[0]+':'+end[1]+'&apply=1');");
  server.sendContent("}");
  yield(); // Permette al WiFi di processare
  server.sendContent("function setDayTime(){");
  server.sendContent("let start=document.getElementById('dayStartTime').value.split(':');");
  server.sendContent("let end=document.getElementById('dayEndTime').value.split(':');");
  server.sendContent("fetch('/setClock?daystart='+start[0]+':'+start[1]+'&dayend='+end[0]+':'+end[1]+'&apply=1');");
  server.sendContent("}");
  server.sendContent("function updateClock(){");
  server.sendContent("fetch('/setClock?get=1').then(r=>r.json()).then(data=>{");
  server.sendContent("document.getElementById('currentTime').innerText=data.time;");
  server.sendContent("});");
  server.sendContent("}");
  server.sendContent("setInterval(updateClock,1000);");
  server.sendContent("document.addEventListener('DOMContentLoaded',()=>{");
  server.sendContent("updateClock();");
  server.sendContent("document.getElementById('color'+currentColor).classList.add('selected');");
  server.sendContent("document.getElementById('secColor'+currentSecondsColor).classList.add('selected');");
  server.sendContent("document.getElementById('type'+currentDisplayType).classList.add('selected');");
  server.sendContent("document.getElementById('autoSwitch').checked=autoSwitchEnabled;");
  server.sendContent("document.getElementById('clockIntervalInput').value=clockInterval;");
  server.sendContent("document.getElementById('weatherIntervalInput').value=weatherInterval;");
  server.sendContent("document.getElementById('clockIntervalInput').disabled=!autoSwitchEnabled;");
  server.sendContent("document.getElementById('weatherIntervalInput').disabled=!autoSwitchEnabled;");
  yield(); // Permette al WiFi di processare
  server.sendContent("document.getElementById('dateDisplay').checked=dateDisplayEnabled;");
  server.sendContent("document.getElementById('dateIntervalInput').value=dateInterval;");
  server.sendContent("document.getElementById('dateIntervalInput').disabled=!dateDisplayEnabled;");
  server.sendContent("document.getElementById('dateColor'+dateColorMode).classList.add('selected');");
  server.sendContent("document.getElementById('dateSize'+dateDisplaySize).classList.add('selected');");
  server.sendContent("document.getElementById('sequenceSelect').value=displaySequence;");
  server.sendContent("document.getElementById('localSensorDisplay').checked=localSensorEnabled;");
  server.sendContent("document.getElementById('localSensorIntervalInput').value=localSensorInterval;");
  server.sendContent("document.getElementById('localSensorIntervalInput').disabled=!localSensorEnabled;");
  server.sendContent("document.getElementById('matrixLedToggle').checked=matrixLedEnabled;");
  server.sendContent("document.getElementById('nightShutdown').checked=nightShutdownEnabled;");
  server.sendContent("document.getElementById('dayShutdown').checked=dayShutdownEnabled;");
  server.sendContent("document.getElementById('nightStartTime').value='" + String(nightShutdownStartHour < 10 ? "0" : "") + String(nightShutdownStartHour) + ":" + String(nightShutdownStartMinute < 10 ? "0" : "") + String(nightShutdownStartMinute) + "';");
  server.sendContent("document.getElementById('nightEndTime').value='" + String(nightShutdownEndHour < 10 ? "0" : "") + String(nightShutdownEndHour) + ":" + String(nightShutdownEndMinute < 10 ? "0" : "") + String(nightShutdownEndMinute) + "';");
  server.sendContent("document.getElementById('dayStartTime').value='" + String(dayShutdownStartHour < 10 ? "0" : "") + String(dayShutdownStartHour) + ":" + String(dayShutdownStartMinute < 10 ? "0" : "") + String(dayShutdownStartMinute) + "';");
  server.sendContent("document.getElementById('dayEndTime').value='" + String(dayShutdownEndHour < 10 ? "0" : "") + String(dayShutdownEndHour) + ":" + String(dayShutdownEndMinute < 10 ? "0" : "") + String(dayShutdownEndMinute) + "';");
  server.sendContent("document.getElementById('nightStartTime').disabled=!nightShutdownEnabled;");
  server.sendContent("document.getElementById('nightEndTime').disabled=!nightShutdownEnabled;");
  server.sendContent("document.getElementById('dayStartTime').disabled=!dayShutdownEnabled;");
  server.sendContent("document.getElementById('dayEndTime').disabled=!dayShutdownEnabled;");
  server.sendContent("for(let i=0;i<7;i++){if(dayShutdownDays&(1<<i))document.getElementById('dsd'+i).classList.add('active');}");
  server.sendContent("document.getElementById('dayShutdownDaysRow').style.opacity=dayShutdownEnabled?'1':'0.4';");
  server.sendContent("for(let i=0;i<7;i++)document.getElementById('dsd'+i).style.pointerEvents=dayShutdownEnabled?'auto':'none';");
  server.sendContent("});");
  server.sendContent("</script>");
  yield(); // Permette al WiFi di processare
  server.sendContent("</head><body>");
  server.sendContent("<div class='nav'><a href='/'>🏠 Menu</a></div>");
  server.sendContent("<div class='container'>");
  server.sendContent("<h1>🕐 Orologio Digitale</h1>");
  server.sendContent("<div class='time-display' id='currentTime'>--:--:--</div>");
  server.sendContent("<input type='text' id='timezone' class='timezone-input' value='" + clockTimezone + "' placeholder='Timezone (es: Europe/Rome)'>");
  server.sendContent("<button onclick='syncNTP()'>🌐 Sincronizza con NTP</button>");
  server.sendContent("<h3 style='margin-top:30px;'>Colore Orologio:</h3>");
  server.sendContent("<div class='color-buttons'>");
  server.sendContent("<button class='color-btn' id='color0' onclick='setColor(0)' style='background:#f00;color:#fff;'>Rosso</button>");
  server.sendContent("<button class='color-btn' id='color1' onclick='setColor(1)' style='background:#0f0;color:#000;'>Verde</button>");
  server.sendContent("<button class='color-btn' id='color2' onclick='setColor(2)' style='background:#00f;color:#fff;'>Blu</button>");
  server.sendContent("<button class='color-btn' id='color3' onclick='setColor(3)' style='background:#ff0;color:#000;'>Giallo</button>");
  server.sendContent("<button class='color-btn' id='color4' onclick='setColor(4)' style='background:#0ff;color:#000;'>Ciano</button>");
  server.sendContent("<button class='color-btn' id='color5' onclick='setColor(5)' style='background:#f0f;color:#fff;'>Magenta</button>");
  server.sendContent("<button class='color-btn' id='color6' onclick='setColor(6)' style='background:#fff;color:#000;'>Bianco</button>");
  server.sendContent("<button class='color-btn' id='color7' onclick='setColor(7)' style='background:#f80;color:#000;'>Arancione</button>");
  server.sendContent("<button class='color-btn' id='color8' onclick='setColor(8)' style='background:linear-gradient(90deg,red,orange,yellow,green,blue,indigo,violet);color:#fff;'>🌈 Rainbow</button>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<h3 style='margin-top:30px;'>Tipo di Orologio:</h3>");
  server.sendContent("<div class='type-buttons'>");
  server.sendContent("<button class='color-btn display-type-btn' id='type0' onclick='setDisplayType(0)' style='background:#333;color:#fff;'>📊 Classico</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type1' onclick='setDisplayType(1)' style='background:#333;color:#fff;'>📏 Compatto</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type2' onclick='setDisplayType(2)' style='background:#333;color:#fff;'>🔠 Grande</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type3' onclick='setDisplayType(3)' style='background:#333;color:#fff;'>💾 Binario</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type4' onclick='setDisplayType(4)' style='background:#333;color:#fff;'>🕐 Analogico</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type5' onclick='setDisplayType(5)' style='background:#333;color:#fff;'>⬆️ Verticale</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type6' onclick='setDisplayType(6)' style='background:#333;color:#fff;'>➡️ Scorrevole</button>");
  server.sendContent("<button class='color-btn display-type-btn' id='type7' onclick='setDisplayType(7)' style='background:#333;color:#fff;'>📅 Compatto+Giorno</button>");
  server.sendContent("</div>");
  server.sendContent("<h3 style='margin-top:30px;'>Colore LED Secondi:</h3>");
  server.sendContent("<div class='color-buttons'>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor0' onclick='setSecondsColor(0)' style='background:#f00;color:#fff;'>Rosso</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor1' onclick='setSecondsColor(1)' style='background:#0f0;color:#000;'>Verde</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor2' onclick='setSecondsColor(2)' style='background:#00f;color:#fff;'>Blu</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor3' onclick='setSecondsColor(3)' style='background:#ff0;color:#000;'>Giallo</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor4' onclick='setSecondsColor(4)' style='background:#0ff;color:#000;'>Ciano</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor5' onclick='setSecondsColor(5)' style='background:#f0f;color:#fff;'>Magenta</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor6' onclick='setSecondsColor(6)' style='background:#fff;color:#000;'>Bianco</button>");
  server.sendContent("<button class='color-btn seconds-color-btn' id='secColor7' onclick='setSecondsColor(7)' style='background:#f80;color:#000;'>Arancione</button>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<h3 style='margin-top:30px;'>⏱️ Alternanza Automatica Orologio/Meteo:</h3>");
  server.sendContent("<div style='background:#1a3a1a;padding:15px;border-radius:8px;margin:15px 0;border:1px solid #2a5a2a;'>");
  server.sendContent("<label style='display:block;margin:10px 0;font-size:16px;'>");
  server.sendContent("<input type='checkbox' id='autoSwitch' onchange='toggleAutoSwitch()' style='width:20px;height:20px;vertical-align:middle;margin-right:10px;'>");
  server.sendContent("Abilita alternanza automatica");
  server.sendContent("</label>");
  server.sendContent("<div style='margin-top:15px;display:grid;grid-template-columns:1fr 1fr;gap:15px;'>");
  server.sendContent("<div>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#4CAF50;font-weight:bold;'>🕐 Tempo Orologio:</label>");
  server.sendContent("<input type='number' id='clockIntervalInput' min='5' max='600' onchange='setClockInterval()' style='padding:10px;width:90%;border:1px solid #4CAF50;border-radius:5px;background:#222;color:#fff;font-size:16px;'>");
  server.sendContent("<span style='display:block;margin-top:5px;color:#aaa;font-size:12px;'>secondi (5-600)</span>");
  server.sendContent("</div>");
  server.sendContent("<div>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#03A9F4;font-weight:bold;'>🌤️ Tempo Meteo:</label>");
  server.sendContent("<input type='number' id='weatherIntervalInput' min='5' max='600' onchange='setWeatherInterval()' style='padding:10px;width:90%;border:1px solid #03A9F4;border-radius:5px;background:#222;color:#fff;font-size:16px;'>");
  server.sendContent("<span style='display:block;margin-top:5px;color:#aaa;font-size:12px;'>secondi (5-600)</span>");
  server.sendContent("</div>");
  server.sendContent("</div>");
  server.sendContent("<p style='margin-top:15px;color:#aaa;font-size:13px;'>Quando attivo, il display mostrerà l'orologio per N secondi, poi il meteo per M secondi, e continuerà ad alternare.</p>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<h3 style='margin-top:30px;'>📅 Visualizzazione Data (DD/MM):</h3>");
  server.sendContent("<div style='background:#1a2a3a;padding:15px;border-radius:8px;margin:15px 0;border:1px solid #2a4a5a;'>");
  server.sendContent("<label style='display:block;margin:10px 0;font-size:16px;'>");
  server.sendContent("<input type='checkbox' id='dateDisplay' onchange='toggleDateDisplay()' style='width:20px;height:20px;vertical-align:middle;margin-right:10px;'>");
  server.sendContent("Abilita visualizzazione data nell'alternanza automatica");
  server.sendContent("</label>");
  server.sendContent("<div style='margin-top:15px;'>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#FFB74D;font-weight:bold;'>📅 Tempo Data:</label>");
  server.sendContent("<input type='number' id='dateIntervalInput' min='5' max='600' onchange='setDateInterval()' style='padding:10px;width:90%;border:1px solid #FFB74D;border-radius:5px;background:#222;color:#fff;font-size:16px;'>");
  server.sendContent("<span style='display:block;margin-top:5px;color:#aaa;font-size:12px;'>secondi (5-600)</span>");
  server.sendContent("</div>");
  server.sendContent("<h4 style='margin-top:20px;color:#FFB74D;'>Colore Data:</h4>");
  server.sendContent("<div class='color-buttons'>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor0' onclick='setDateColor(0)' style='background:#f00;color:#fff;'>Rosso</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor1' onclick='setDateColor(1)' style='background:#0f0;color:#000;'>Verde</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor2' onclick='setDateColor(2)' style='background:#00f;color:#fff;'>Blu</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor3' onclick='setDateColor(3)' style='background:#ff0;color:#000;'>Giallo</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor4' onclick='setDateColor(4)' style='background:#0ff;color:#000;'>Ciano</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor5' onclick='setDateColor(5)' style='background:#f0f;color:#fff;'>Magenta</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor6' onclick='setDateColor(6)' style='background:#fff;color:#000;'>Bianco</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor7' onclick='setDateColor(7)' style='background:#f80;color:#000;'>Arancione</button>");
  server.sendContent("<button class='color-btn date-color-btn' id='dateColor8' onclick='setDateColor(8)' style='background:linear-gradient(90deg,red,orange,yellow,green,blue,indigo,violet);color:#fff;'>🌈 Rainbow</button>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<h4 style='margin-top:20px;color:#FFB74D;'>Dimensione Data:</h4>");
  server.sendContent("<div style='display:grid;grid-template-columns:1fr 1fr;gap:10px;max-width:350px;margin:10px auto;'>");
  server.sendContent("<button class='color-btn date-size-btn' id='dateSize0' onclick='setDateSize(0)' style='background:#333;color:#fff;'>📏 Piccolo</button>");
  server.sendContent("<button class='color-btn date-size-btn' id='dateSize1' onclick='setDateSize(1)' style='background:#333;color:#fff;'>🔠 Grande</button>");
  server.sendContent("</div>");
  server.sendContent("<h4 style='margin-top:20px;color:#FFB74D;'>Sequenza Alternanza:</h4>");
  server.sendContent("<select id='sequenceSelect' onchange='setDisplaySequence()' style='padding:10px;width:100%;border:1px solid #FFB74D;border-radius:5px;background:#222;color:#fff;font-size:14px;margin:10px 0;'>");
  server.sendContent("<option value='0'>🕐 Orologio → 🌤️ Meteo → 📅 Data</option>");
  server.sendContent("<option value='1'>🕐 Orologio → 📅 Data → 🌤️ Meteo</option>");
  server.sendContent("<option value='2'>📅 Data → 🕐 Orologio → 🌤️ Meteo</option>");
  server.sendContent("<option value='4'>🌤️ Meteo → 🕐 Orologio → 📅 Data</option>");
  server.sendContent("<option value='5'>🌤️ Meteo → 📅 Data → 🕐 Orologio</option>");
  server.sendContent("</select>");
  server.sendContent("<p style='margin-top:15px;color:#aaa;font-size:13px;'>Quando abilitata, la data (giorno e mese) verrà mostrata nell'alternanza automatica secondo la sequenza scelta. La data viene visualizzata con GIORNO in alto e MESE in basso.</p>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<h3 style='margin-top:30px;'>🌡️ Sensore Locale (Temp./Umidità):</h3>");
  server.sendContent("<div style='background:#1a2a1a;padding:15px;border-radius:8px;margin:15px 0;border:1px solid #2a5a2a;'>");
  server.sendContent("<label style='display:block;margin:10px 0;font-size:16px;'>");
  server.sendContent("<input type='checkbox' id='localSensorDisplay' onchange='toggleLocalSensor()' style='width:20px;height:20px;vertical-align:middle;margin-right:10px;'>");
  server.sendContent("Abilita visualizzazione sensore HTU21D nell'alternanza");
  server.sendContent("</label>");
  if (sensorAvailable) {
    server.sendContent("<p style='color:#4CAF50;font-weight:bold;margin:10px 0;'>✅ Sensore HTU21D rilevato e funzionante</p>");
    server.sendContent("<p style='color:#aaa;font-size:13px;margin:5px 0;'>Temperatura: " + String(currentTemperature, 1) + "°C | Umidità: " + String(currentHumidity, 1) + "%</p>");
  } else {
    server.sendContent("<p style='color:#FF5722;font-weight:bold;margin:10px 0;'>⚠️ Sensore HTU21D non disponibile</p>");
    server.sendContent("<p style='color:#aaa;font-size:12px;margin:5px 0;'>Verifica connessione I2C (SDA=GPIO8, SCL=GPIO9)</p>");
  }
  // Taratura manuale temperatura
  server.sendContent("<div style='margin-top:15px;background:#1a2a2a;padding:12px;border-radius:8px;border:1px solid #2a4a4a;'>");
  server.sendContent("<label style='display:block;margin-bottom:8px;color:#FFB74D;font-weight:bold;'>🔧 Taratura Temperatura:</label>");
  server.sendContent("<input type='number' id='sensorTempOffset' min='-5' max='5' step='0.1' value='" + String(sensorTempOffset, 1) + "' onchange='setSensorOffset()' style='padding:10px;width:90%;border:1px solid #FFB74D;border-radius:5px;background:#222;color:#fff;font-size:16px;'>");
  server.sendContent("<span style='display:block;margin-top:5px;color:#aaa;font-size:12px;'>Offset in °C (da -5.0 a +5.0). Valore 0 = nessuna correzione.</span>");
  if (sensorAvailable) {
    float rawTemp = currentTemperature - sensorTempOffset;
    server.sendContent("<p style='color:#aaa;font-size:13px;margin-top:8px;'>Valore raw: " + String(rawTemp, 1) + "°C → Calibrato: " + String(currentTemperature, 1) + "°C (offset: " + (sensorTempOffset >= 0 ? "+" : "") + String(sensorTempOffset, 1) + "°C)</p>");
  }
  server.sendContent("</div>");
  server.sendContent("<div style='margin-top:15px;'>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#66BB6A;font-weight:bold;'>🕐 Tempo Sensore:</label>");
  server.sendContent("<input type='number' id='localSensorIntervalInput' min='5' max='300' onchange='setLocalSensorInterval()' style='padding:10px;width:90%;border:1px solid #66BB6A;border-radius:5px;background:#222;color:#fff;font-size:16px;'>");
  server.sendContent("<span style='display:block;margin-top:5px;color:#aaa;font-size:12px;'>secondi (5-300)</span>");
  server.sendContent("</div>");
  server.sendContent("<p style='margin-top:15px;color:#aaa;font-size:13px;'>");
  server.sendContent("Il sensore HTU21D mostra temperatura e umidità ambiente con icone.<br>");
  server.sendContent("🌡️ <b>Temperatura:</b> cifre BIANCHE<br>");
  server.sendContent("💧 <b>Umidità:</b> cifre CIANO<br>");
  server.sendContent("I dati vengono aggiornati ogni 2 secondi.");
  server.sendContent("</p>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<h3 style='margin-top:30px;'>💡 Controllo LED Matrice:</h3>");
  server.sendContent("<div style='background:#2a1a1a;padding:15px;border-radius:8px;margin:15px 0;border:1px solid #5a2a2a;'>");
  server.sendContent("<label style='display:block;margin:10px 0;font-size:16px;'>");
  server.sendContent("<input type='checkbox' id='matrixLedToggle' onchange='toggleMatrixLed()' style='width:20px;height:20px;vertical-align:middle;margin-right:10px;'>");
  server.sendContent("💡 Accendi/Spegni LED Matrice");
  server.sendContent("</label>");
  server.sendContent("<p style='margin-top:10px;color:#aaa;font-size:13px;'>Toggle rapido per accendere/spegnere i LED della matrice.</p>");
  server.sendContent("</div>");
  server.sendContent("<h3 style='margin-top:30px;'>🌙 Spegnimento Programmato:</h3>");
  server.sendContent("<div style='background:#1a1a2a;padding:15px;border-radius:8px;margin:15px 0;border:1px solid #3a3a5a;'>");
  server.sendContent("<div style='background:#2a2a4a;padding:15px;border-radius:8px;margin-bottom:15px;'>");
  server.sendContent("<label style='display:block;margin:10px 0;font-size:16px;'>");
  server.sendContent("<input type='checkbox' id='nightShutdown' onchange='toggleNightShutdown()' style='width:20px;height:20px;vertical-align:middle;margin-right:10px;'>");
  server.sendContent("🌙 Spegnimento Notte");
  server.sendContent("</label>");
  server.sendContent("<div style='display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px;'>");
  server.sendContent("<div>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#9FA8DA;font-size:14px;'>Inizio (HH:MM):</label>");
  server.sendContent("<input type='time' id='nightStartTime' onchange='setNightTime()' style='padding:8px;width:90%;border:1px solid #5C6BC0;border-radius:5px;background:#1a1a2a;color:#fff;font-size:14px;'>");
  server.sendContent("</div>");
  server.sendContent("<div>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#9FA8DA;font-size:14px;'>Fine (HH:MM):</label>");
  server.sendContent("<input type='time' id='nightEndTime' onchange='setNightTime()' style='padding:8px;width:90%;border:1px solid #5C6BC0;border-radius:5px;background:#1a1a2a;color:#fff;font-size:14px;'>");
  server.sendContent("</div>");
  server.sendContent("</div>");
  server.sendContent("</div>");
  yield(); // Permette al WiFi di processare
  server.sendContent("<div style='background:#2a3a2a;padding:15px;border-radius:8px;'>");
  server.sendContent("<label style='display:block;margin:10px 0;font-size:16px;'>");
  server.sendContent("<input type='checkbox' id='dayShutdown' onchange='toggleDayShutdown()' style='width:20px;height:20px;vertical-align:middle;margin-right:10px;'>");
  server.sendContent("☀️ Spegnimento Giorno");
  server.sendContent("</label>");
  server.sendContent("<div id='dayShutdownDaysRow' style='display:flex;justify-content:center;gap:6px;margin:10px 0;flex-wrap:wrap;'>");
  {
    const char* dayNames[] = {"Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"};
    for (int i = 0; i < 7; i++) {
      bool isActive = dayShutdownDays & (1 << i);
      String cls = isActive ? "active" : "";
      server.sendContent("<div id='dsd" + String(i) + "' class='" + cls + "' onclick='toggleDayShutdownDay(" + String(i) + ")' style='cursor:pointer;padding:6px 10px;border-radius:5px;border:2px solid #FFA726;font-size:13px;text-align:center;min-width:36px;background:" + String(isActive ? "#FFA726" : "transparent") + ";color:" + String(isActive ? "#000" : "#FFA726") + ";transition:all 0.2s;'>" + String(dayNames[i]) + "</div>");
    }
  }
  server.sendContent("</div>");
  server.sendContent("<style>#dayShutdownDaysRow .active{background:#FFA726!important;color:#000!important;}#dayShutdownDaysRow div:not(.active){background:transparent!important;color:#FFA726!important;}</style>");
  server.sendContent("<div style='display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px;'>");
  server.sendContent("<div>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#FFD54F;font-size:14px;'>Inizio (HH:MM):</label>");
  server.sendContent("<input type='time' id='dayStartTime' onchange='setDayTime()' style='padding:8px;width:90%;border:1px solid #FFA726;border-radius:5px;background:#2a2a1a;color:#fff;font-size:14px;'>");
  server.sendContent("</div>");
  server.sendContent("<div>");
  server.sendContent("<label style='display:block;margin-bottom:5px;color:#FFD54F;font-size:14px;'>Fine (HH:MM):</label>");
  server.sendContent("<input type='time' id='dayEndTime' onchange='setDayTime()' style='padding:8px;width:90%;border:1px solid #FFA726;border-radius:5px;background:#2a2a1a;color:#fff;font-size:14px;'>");
  server.sendContent("</div>");
  server.sendContent("</div>");
  server.sendContent("</div>");
  server.sendContent("<p style='margin-top:15px;color:#aaa;font-size:13px;'>Durante le fasce orarie impostate, il display si spegnerà completamente. Nel resto della giornata funzionerà normalmente.</p>");
  server.sendContent("</div>");
  server.sendContent("<p style='margin-top:30px;color:#aaa;'>L'orologio si sincronizza automaticamente con i server NTP. ORE in alto, MINUTI in basso sulla matrice LED.</p>");
  server.sendContent("</div></body></html>");

  // Chiudi la connessione chunked
  server.sendContent("");
}

void handleSetClock() {
  if (server.hasArg("get")) {
    String timeStr = myTZ.dateTime("H:i:s");
    String json = "{\"time\":\"" + timeStr + "\"}";
    server.send(200, "application/json", json);
    return;
  }

  if (server.hasArg("secondscolor")) {
    secondsLedColorMode = server.arg("secondscolor").toInt();
    if (secondsLedColorMode < 0 || secondsLedColorMode > 7) secondsLedColorMode = 0;
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("displaytype")) {
    clockDisplayType = server.arg("displaytype").toInt();
    if (clockDisplayType < 0 || clockDisplayType > 7) clockDisplayType = 0;
    saveConfig(); // Salva in EEPROM

    // Forza visualizzazione orologio anche se alternanza automatica è attiva
    showingClock = true; // Forza orologio
    currentDisplayMode = 0; // Forza modalità orologio (0=orologio, 1=meteo, 2=data)
    sequenceIndex = 0; // Reset indice sequenza alternanza (orologio è sempre primo)
    lastClockWeatherSwitch = millis(); // Reset timer alternanza
    forceRedraw = true; // Forza ridisegno immediato

    // Forza SEMPRE il cambio di stato a orologio quando si cambia tipo
    changeState(STATE_GAME_CLOCK);

    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("color")) {
    clockColorMode = server.arg("color").toInt();
    if (clockColorMode < 0 || clockColorMode > 8) clockColorMode = 1;
    saveConfig(); // Salva in EEPROM

    // Forza visualizzazione orologio anche se alternanza automatica è attiva
    showingClock = true; // Forza orologio
    currentDisplayMode = 0; // Forza modalità orologio (0=orologio, 1=meteo, 2=data)
    sequenceIndex = 0; // Reset indice sequenza alternanza (orologio è sempre primo)
    lastClockWeatherSwitch = millis(); // Reset timer alternanza
    forceRedraw = true; // Forza ridisegno immediato

    // Forza SEMPRE il cambio di stato a orologio quando si cambia colore
    changeState(STATE_GAME_CLOCK);

    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("autoswitch")) {
    clockWeatherAutoSwitch = (server.arg("autoswitch") == "1");

    if (clockWeatherAutoSwitch) {
      // Reset del timer quando si abilita l'alternanza
      lastClockWeatherSwitch = millis();
      showingClock = true;
    }
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("clockinterval")) {
    clockDisplayInterval = server.arg("clockinterval").toInt();
    if (clockDisplayInterval < 5) clockDisplayInterval = 5;
    if (clockDisplayInterval > 600) clockDisplayInterval = 600;

    // Reset del timer quando si cambia l'intervallo
    lastClockWeatherSwitch = millis();
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("weatherinterval")) {
    weatherDisplayInterval = server.arg("weatherinterval").toInt();
    if (weatherDisplayInterval < 5) weatherDisplayInterval = 5;
    if (weatherDisplayInterval > 600) weatherDisplayInterval = 600;

    // Reset del timer quando si cambia l'intervallo
    lastClockWeatherSwitch = millis();
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("datedisplay")) {
    dateDisplayEnabled = (server.arg("datedisplay") == "1");
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("dateinterval")) {
    dateDisplayInterval = server.arg("dateinterval").toInt();
    if (dateDisplayInterval < 5) dateDisplayInterval = 5;
    if (dateDisplayInterval > 600) dateDisplayInterval = 600;

    // Reset del timer quando si cambia l'intervallo
    lastClockWeatherSwitch = millis();
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("datecolor")) {
    dateColorMode = server.arg("datecolor").toInt();
    if (dateColorMode < 0 || dateColorMode > 8) dateColorMode = 1;
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("datesize")) {
    dateDisplaySize = server.arg("datesize").toInt();
    if (dateDisplaySize < 0 || dateDisplaySize > 1) dateDisplaySize = 0;
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  // === IMPOSTAZIONI SENSORE LOCALE (TEMPERATURA/UMIDITÀ) ===
  if (server.hasArg("localsensor")) {
    localSensorDisplayEnabled = (server.arg("localsensor") == "1");
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("sensoroffset")) {
    sensorTempOffset = server.arg("sensoroffset").toFloat();
    if (sensorTempOffset < -5.0) sensorTempOffset = -5.0;
    if (sensorTempOffset > 5.0) sensorTempOffset = 5.0;
    saveConfig();
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("localsensorinterval")) {
    localSensorDisplayInterval = server.arg("localsensorinterval").toInt();
    if (localSensorDisplayInterval < 5) localSensorDisplayInterval = 5;
    if (localSensorDisplayInterval > 300) localSensorDisplayInterval = 300;

    // Reset del timer quando si cambia l'intervallo
    lastClockWeatherSwitch = millis();
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("sequence")) {
    displaySequence = server.arg("sequence").toInt();
    if (displaySequence < 0 || displaySequence > 5) displaySequence = 0;

    // Reset del timer e della sequenza quando si cambia
    lastClockWeatherSwitch = millis();
    currentDisplayMode = 0;
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("matrixled")) {
    matrixLedEnabled = (server.arg("matrixled") == "1");
    saveConfig(); // Salva in EEPROM
    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("nightshutdown")) {
    nightShutdownEnabled = (server.arg("nightshutdown") == "1");
    saveConfig(); // Salva in EEPROM
    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("dayshutdown")) {
    dayShutdownEnabled = (server.arg("dayshutdown") == "1");
    saveConfig(); // Salva in EEPROM
    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("dayshutdowndays")) {
    uint8_t val = server.arg("dayshutdowndays").toInt();
    if (val > 0 && val <= 0x7F) dayShutdownDays = val;
    saveConfig();
    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("nightstart") && server.hasArg("nightend")) {
    String startTime = server.arg("nightstart");
    String endTime = server.arg("nightend");

    int startHour = startTime.substring(0, 2).toInt();
    int startMinute = startTime.substring(3, 5).toInt();
    int endHour = endTime.substring(0, 2).toInt();
    int endMinute = endTime.substring(3, 5).toInt();

    nightShutdownStartHour = startHour;
    nightShutdownStartMinute = startMinute;
    nightShutdownEndHour = endHour;
    nightShutdownEndMinute = endMinute;
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("daystart") && server.hasArg("dayend")) {
    String startTime = server.arg("daystart");
    String endTime = server.arg("dayend");

    int startHour = startTime.substring(0, 2).toInt();
    int startMinute = startTime.substring(3, 5).toInt();
    int endHour = endTime.substring(0, 2).toInt();
    int endMinute = endTime.substring(3, 5).toInt();

    dayShutdownStartHour = startHour;
    dayShutdownStartMinute = startMinute;
    dayShutdownEndHour = endHour;
    dayShutdownEndMinute = endMinute;
    saveConfig(); // Salva in EEPROM

    if (server.hasArg("apply")) {
      changeState(STATE_GAME_CLOCK);
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("sync")) {
    if (server.hasArg("tz")) {
      clockTimezone = server.arg("tz");
      myTZ.setPosix(clockTimezone);
    }
    if (server.hasArg("color")) {
      clockColorMode = server.arg("color").toInt();
      if (clockColorMode < 0 || clockColorMode > 8) clockColorMode = 1;
    }
    saveConfig(); // Salva in EEPROM
    changeState(STATE_GAME_CLOCK);
    server.send(200, "text/plain", "Orologio sincronizzato con NTP!");
    return;
  }

  server.send(400, "text/plain", "Parametri non validi");
}

void handleBrightness() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Controllo Luminosità</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:500px;margin:0 auto;background:#1a1a1a;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(255,255,255,0.1);border:1px solid #333;}";
  html += ".brightness-slider{width:100%;height:50px;margin:30px 0;-webkit-appearance:none;background:#333;outline:none;border-radius:25px;}";
  html += ".brightness-slider::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:60px;height:60px;background:#FF9800;cursor:pointer;border-radius:50%;box-shadow:0 0 20px rgba(255,152,0,0.5);}";
  html += ".brightness-slider::-moz-range-thumb{width:60px;height:60px;background:#FF9800;cursor:pointer;border-radius:50%;box-shadow:0 0 20px rgba(255,152,0,0.5);}";
  html += ".brightness-value{font-size:48px;margin:20px;color:#FF9800;}";
  html += ".preset-buttons button{padding:15px 30px;margin:10px;background:#555;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;}";
  html += ".preset-buttons button:hover{background:#666;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1,h3,p{color:#fff;}";
  html += "</style>";
  html += "<script>";
  html += "function updateBrightness(val){";
  html += "document.getElementById('brightnessValue').innerText=val;";
  html += "fetch('/brightness?val='+val);";
  html += "}";
  html += "function setBrightness(val){";
  html += "document.getElementById('brightnessSlider').value=val;";
  html += "updateBrightness(val);";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>💡 Controllo Luminosità</h1>";
  html += "<div class='brightness-value' id='brightnessValue'>" + String(currentBrightness) + "</div>";
  html += "<input type='range' min='1' max='128' step='1' value='" + String(currentBrightness) + "' class='brightness-slider' id='brightnessSlider' oninput='updateBrightness(this.value)'>";
  html += "<div class='preset-buttons'>";
  html += "<h3>Preset:</h3>";
  html += "<button onclick='setBrightness(1)'>🌑 Minimo (1)</button>";
  html += "<button onclick='setBrightness(10)'>🌙 Medio (10)</button>";
  html += "<button onclick='setBrightness(20)'>🌃 Notte (20)</button>";
  html += "<button onclick='setBrightness(40)'>🌤️ Giorno (40)</button>";
  html += "<button onclick='setBrightness(100)'>☀️ Alto (100)</button>";
  html += "<button onclick='setBrightness(128)'>🔆 Massimo (128)</button>";
  html += "</div>";
  html += "<p style='margin-top:30px;color:#aaa;'>Regola la luminosità della matrice LED (1-128).</p>";
  html += "</div></body></html>";

  if (server.hasArg("val")) {
    int brightness = server.arg("val").toInt();
    if (brightness >= 1 && brightness <= 128) {
      currentBrightness = brightness;

      // Setta flag per applicare luminosità nel loop principale
      // Evita artefatti causati dagli interrupt WiFi durante FastLED.show()
      pendingBrightnessUpdate = true;

      // Salva configurazione in EEPROM
      saveConfig();
    }
  }

  server.send(200, "text/html", html);
}

void handleReboot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "</head><body style='text-align:center;padding:50px;background:#FF5722;color:white;font-family:Arial;'>";
  html += "<h1>🔌 Riavvio in corso...</h1>";
  html += "<p style='font-size:18px;'>Il dispositivo si sta riavviando.</p>";
  html += "<p style='font-size:16px;color:#ffeb3b;'>Attendi circa 10 secondi e ricarica la pagina.</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);

  Serial.println("Reboot richiesto dall'utente via web...");
  delay(1000);
  ESP.restart();
}

void handleReset() {
  Serial.println("WiFi reset requested from web interface");

  // Leggi configurazione attuale per preservare le altre impostazioni
  WiFiConfig config;
  EEPROM.get(0, config);

  // Cancella solo le credenziali WiFi, preserva tutto il resto
  memset(config.ssid, 0, sizeof(config.ssid));
  memset(config.password, 0, sizeof(config.password));

  // Salva in EEPROM
  EEPROM.put(0, config);
  EEPROM.commit();

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;text-align:center;padding:50px;background:linear-gradient(135deg,#d32f2f,#f44336);color:white;min-height:100vh;display:flex;align-items:center;justify-content:center;margin:0;}";
  html += ".message{background:rgba(255,255,255,0.95);color:#333;padding:40px;border-radius:15px;box-shadow:0 8px 32px rgba(0,0,0,0.3);max-width:500px;}";
  html += "h1{color:#d32f2f;margin-bottom:20px;font-size:2em;}";
  html += "p{font-size:1.1em;line-height:1.6;margin:10px 0;}";
  html += ".icon{font-size:4em;margin-bottom:20px;}";
  html += ".instructions{background:#fff3cd;border:2px solid #ff9800;border-radius:8px;padding:15px;margin-top:20px;color:#856404;}";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='5;url=/'></head><body>";
  html += "<div class='message'>";
  html += "<div class='icon'>🔄</div>";
  html += "<h1>Configurazione WiFi Resettata!</h1>";
  html += "<p>Le credenziali WiFi sono state cancellate.</p>";
  html += "<p>Il dispositivo si riavvierà in modalità Access Point.</p>";
  html += "<div class='instructions'>";
  html += "<strong>📱 Prossimi passi:</strong><br>";
  html += "1. Collegati alla rete WiFi <strong>CONSOLE QUADRA</strong><br>";
  html += "2. Apri il browser e vai su <strong>192.168.4.1</strong><br>";
  html += "3. Configura la nuova rete WiFi";
  html += "</div>";
  html += "<p style='margin-top:20px;color:#888;font-size:0.9em;'>Riavvio in corso...</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);

  delay(3000);
  ESP.restart();
}

// ============================================
// WEATHER STATION (Open-Meteo API - No API Key Required)
// ============================================

// Mappa codici WMO alle icone esistenti (compatibile con OpenWeatherMap)
String mapWMOCodeToIcon(int wmoCode, bool isNight) {
  String suffix = isNight ? "n" : "d";

  switch (wmoCode) {
    case 0:                     // Cielo sereno
      return "01" + suffix;
    case 1:                     // Prevalentemente sereno
      return "02" + suffix;
    case 2:                     // Parzialmente nuvoloso
      return "03" + suffix;
    case 3:                     // Coperto
      return "04" + suffix;
    case 45: case 48:           // Nebbia
      return "50" + suffix;
    case 51: case 53: case 55:  // Pioggerella
    case 56: case 57:           // Pioggerella gelata
      return "09" + suffix;
    case 61: case 63: case 65:  // Pioggia
    case 66: case 67:           // Pioggia gelata
    case 80: case 81: case 82:  // Rovesci
      return "10" + suffix;
    case 71: case 73: case 75:  // Neve
    case 77:                    // Granelli di neve
    case 85: case 86:           // Rovesci di neve
      return "13" + suffix;
    case 95:                    // Temporale
    case 96: case 99:           // Temporale con grandine
      return "11" + suffix;
    default:
      return "03" + suffix;     // Default: parzialmente nuvoloso
  }
}

// Ottiene descrizione meteo in italiano dal codice WMO
String getWMODescription(int wmoCode) {
  switch (wmoCode) {
    case 0:  return "Cielo sereno";
    case 1:  return "Prevalentemente sereno";
    case 2:  return "Parzialmente nuvoloso";
    case 3:  return "Coperto";
    case 45: return "Nebbia";
    case 48: return "Nebbia con brina";
    case 51: return "Pioggerella leggera";
    case 53: return "Pioggerella moderata";
    case 55: return "Pioggerella intensa";
    case 56: return "Pioggerella gelata leggera";
    case 57: return "Pioggerella gelata intensa";
    case 61: return "Pioggia leggera";
    case 63: return "Pioggia moderata";
    case 65: return "Pioggia intensa";
    case 66: return "Pioggia gelata leggera";
    case 67: return "Pioggia gelata intensa";
    case 71: return "Neve leggera";
    case 73: return "Neve moderata";
    case 75: return "Neve intensa";
    case 77: return "Granelli di neve";
    case 80: return "Rovesci leggeri";
    case 81: return "Rovesci moderati";
    case 82: return "Rovesci violenti";
    case 85: return "Rovesci di neve leggeri";
    case 86: return "Rovesci di neve intensi";
    case 95: return "Temporale";
    case 96: return "Temporale con grandine leggera";
    case 99: return "Temporale con grandine forte";
    default: return "Non disponibile";
  }
}

// URL encode completo per caratteri speciali e accentati
String encodeCity(String str) {
  String encoded = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    unsigned char c = (unsigned char)str.charAt(i);
    // Caratteri ASCII sicuri per URL
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      encoded += (char)c;
    } else if (c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      // Encode tutti gli altri caratteri (inclusi UTF-8 bytes)
      char hex[4];
      sprintf(hex, "%%%02X", c);
      encoded += hex;
    }
  }
  return encoded;
}

// Funzione helper per connessione HTTPS sicura
WiFiClientSecure* getSecureClient() {
  WiFiClientSecure* client = new WiFiClientSecure();
  client->setInsecure();
  return client;
}

// Geocoding: converte nome città in coordinate usando Open-Meteo Geocoding API
bool getCoordinatesFromCity(String city, float &lat, float &lon) {
  HTTPClient http;

  // URL encode della città
  String cityEncoded = city;
  cityEncoded.replace(" ", "%20");

  String url = "http://geocoding-api.open-meteo.com/v1/search?name=" + cityEncoded + "&count=1&language=it&format=json";

  Serial.println("Geocoding URL: " + url);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc.containsKey("results") && doc["results"].size() > 0) {
      lat = doc["results"][0]["latitude"].as<float>();
      lon = doc["results"][0]["longitude"].as<float>();
      String foundCity = doc["results"][0]["name"].as<String>();

      Serial.println("Geocoding OK: " + foundCity);
      Serial.println("Lat: " + String(lat, 4) + ", Lon: " + String(lon, 4));

      http.end();
      return true;
    }
  }

  Serial.println("Geocoding failed. HTTP Code: " + String(httpCode));
  http.end();
  return false;
}

bool updateWeatherData() {
  WiFiConfig config;
  EEPROM.get(0, config);

  // Verifica che le coordinate siano configurate
  if (config.weatherLatitude == 0.0 && config.weatherLongitude == 0.0) {
    Serial.println("Weather coordinates not configured");
    return false;
  }

  HTTPClient http;

  // Open-Meteo API per meteo corrente (gratuita, senza API key)
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(config.weatherLatitude, 4) +
               "&longitude=" + String(config.weatherLongitude, 4) +
               "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,surface_pressure,is_day" +
               "&timezone=Europe/Rome";

  Serial.println("Open-Meteo API URL: " + url);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Nome città dalla configurazione
      currentWeather.city = String(config.weatherCity);

      // Dati meteo correnti
      currentWeather.temperature = (int)doc["current"]["temperature_2m"].as<float>();
      currentWeather.humidity = doc["current"]["relative_humidity_2m"].as<int>();
      currentWeather.pressure = (int)doc["current"]["surface_pressure"].as<float>();
      currentWeather.windSpeed = (int)doc["current"]["wind_speed_10m"].as<float>();

      int wmoCode = doc["current"]["weather_code"].as<int>();
      bool isNight = (doc["current"]["is_day"].as<int>() == 0);

      currentWeather.description = getWMODescription(wmoCode);
      currentWeather.icon = mapWMOCodeToIcon(wmoCode, isNight);
      currentWeather.isValid = true;

      Serial.println("Weather data updated successfully (Open-Meteo)");
      Serial.println("City: " + currentWeather.city);
      Serial.println("Temp: " + String(currentWeather.temperature) + "°C");
      Serial.println("Wind: " + String(currentWeather.windSpeed) + " km/h");
      Serial.println("Description: " + currentWeather.description);
      Serial.println("Icon: " + currentWeather.icon);

      http.end();
      return true;
    } else {
      Serial.println("JSON parsing error");
    }
  }

  String errorMsg = "Failed to update weather data. HTTP Code: " + String(httpCode);
  if (httpCode < 0) {
    errorMsg += " (Connection error)";
  }
  Serial.println(errorMsg);
  http.end();
  return false;
}

bool updateForecastData() {
  WiFiConfig config;
  EEPROM.get(0, config);

  // Verifica che le coordinate siano configurate
  if (config.weatherLatitude == 0.0 && config.weatherLongitude == 0.0) {
    return false;
  }

  HTTPClient http;

  // Open-Meteo API per previsioni giornaliere (gratuita, senza API key)
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(config.weatherLatitude, 4) +
               "&longitude=" + String(config.weatherLongitude, 4) +
               "&daily=temperature_2m_max,temperature_2m_min,weather_code,relative_humidity_2m_max" +
               "&timezone=Europe/Rome&forecast_days=5";

  Serial.println("Forecast URL: " + url);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      for (int i = 0; i < 5; i++) {
        forecast[i].date = doc["daily"]["time"][i].as<String>();
        int wmoCode = doc["daily"]["weather_code"][i].as<int>();
        forecast[i].description = getWMODescription(wmoCode);
        forecast[i].tempMin = (int)doc["daily"]["temperature_2m_min"][i].as<float>();
        forecast[i].tempMax = (int)doc["daily"]["temperature_2m_max"][i].as<float>();
        forecast[i].humidity = doc["daily"]["relative_humidity_2m_max"][i].as<int>();
        forecast[i].icon = mapWMOCodeToIcon(wmoCode, false);
      }

      Serial.println("Forecast data updated successfully (Open-Meteo)");
      http.end();
      return true;
    } else {
      Serial.println("Forecast JSON parsing error");
    }
  }

  Serial.println("Forecast update failed. HTTP Code: " + String(httpCode));
  http.end();
  return false;
}

void drawWeatherIcon(String icon, int xOffset, int yOffset, CRGB color, int animFrame) {
  // Icone meteo animate per matrice 16x16
  // Le icone sono 8x8 pixel

  if (icon.startsWith("01")) {  // Clear sky - Sole ANIMATO o Luna ANIMATA
    // Controlla se è notte (icona termina con "n")
    if (icon.endsWith("n")) {
      // LUNA ANIMATA - Cielo sereno notturno
      // Cerchio luna con effetto pulsante
      CRGB moonColor1, moonColor2;
      if (animFrame == 0 || animFrame == 2) {
        moonColor1 = CRGB(200, 200, 220); // Bianco-blu chiaro
        moonColor2 = CRGB(150, 150, 180); // Bianco-blu medio
      } else {
        moonColor1 = CRGB(180, 180, 200); // Bianco-blu medio
        moonColor2 = CRGB(120, 120, 160); // Bianco-blu scuro
      }

      // Corpo principale della luna (cerchio)
      for (int x = 2; x <= 5; x++) {
        for (int y = 2; y <= 5; y++) {
          if ((x == 2 || x == 5) && (y == 2 || y == 5)) continue;
          // Alterna colori per dare profondità
          if ((x + y) % 2 == 0) {
            setPixel(xOffset + x, yOffset + y, moonColor1);
          } else {
            setPixel(xOffset + x, yOffset + y, moonColor2);
          }
        }
      }

      // Crateri lunari che appaiono/scompaiono (animazione)
      if (animFrame == 0 || animFrame == 2) {
        setPixel(xOffset + 3, yOffset + 3, CRGB(100, 100, 140)); // Cratere
        setPixel(xOffset + 5, yOffset + 4, CRGB(110, 110, 150)); // Cratere
      } else {
        setPixel(xOffset + 2, yOffset + 4, CRGB(90, 90, 130)); // Cratere alternativo
        setPixel(xOffset + 4, yOffset + 2, CRGB(100, 100, 140)); // Cratere alternativo
      }

      // Bagliore lunare che pulsa (stelle intorno)
      CRGB glowColor;
      if (animFrame == 0 || animFrame == 2) {
        glowColor = CRGB(180, 180, 200); // Bagliore
      } else {
        glowColor = CRGB(100, 100, 140); // Bagliore ridotto
      }

      // Piccole stelle intorno alla luna
      setPixel(xOffset + 1, yOffset + 1, glowColor);
      setPixel(xOffset + 6, yOffset + 1, glowColor);
      setPixel(xOffset + 1, yOffset + 6, glowColor);
      setPixel(xOffset + 6, yOffset + 6, glowColor);

    } else {
      // SOLE ANIMATO - Cielo sereno diurno
      // Cerchio centrale sempre acceso
      for (int x = 2; x <= 5; x++) {
        for (int y = 2; y <= 5; y++) {
          if ((x == 2 || x == 5) && (y == 2 || y == 5)) continue;
          setPixel(xOffset + x, yOffset + y, CRGB(255, 200, 0));
        }
      }
      // Raggi che pulsano - alternanza molto visibile
      CRGB rayColor;
      if (animFrame == 0 || animFrame == 2) {
        rayColor = CRGB(255, 200, 0); // Giallo brillante
      } else {
        rayColor = CRGB(100, 80, 0); // Giallo scuro (quasi spento)
      }

      setPixel(xOffset + 3, yOffset + 0, rayColor);
      setPixel(xOffset + 4, yOffset + 0, rayColor);
      setPixel(xOffset + 3, yOffset + 7, rayColor);
      setPixel(xOffset + 4, yOffset + 7, rayColor);
      setPixel(xOffset + 0, yOffset + 3, rayColor);
      setPixel(xOffset + 0, yOffset + 4, rayColor);
      setPixel(xOffset + 7, yOffset + 3, rayColor);
      setPixel(xOffset + 7, yOffset + 4, rayColor);
      setPixel(xOffset + 1, yOffset + 1, rayColor);
      setPixel(xOffset + 6, yOffset + 1, rayColor);
      setPixel(xOffset + 1, yOffset + 6, rayColor);
      setPixel(xOffset + 6, yOffset + 6, rayColor);
    }
  }
  else if (icon.startsWith("02")) {  // Few clouds - Sole/Luna con nuvola ANIMATO
    // Controlla se è notte (icona termina con "n")
    if (icon.endsWith("n")) {
      // LUNA con nuvola - Poche nuvole notturno
      // Luna a mezzaluna SPESSA (2 file di LED)
      CRGB moonColor1, moonColor2;
      if (animFrame == 0 || animFrame == 2) {
        moonColor1 = CRGB(220, 220, 240); // Bianco-blu brillante
        moonColor2 = CRGB(180, 180, 210); // Bianco-blu medio
      } else {
        moonColor1 = CRGB(200, 200, 230); // Bianco-blu chiaro
        moonColor2 = CRGB(160, 160, 190); // Bianco-blu scuro
      }

      // Mezzaluna DOPPIA SPESSA (forma C con 2 file di LED) - ARROTONDATA
      // Prima fila (esterna)
      setPixel(xOffset + 1, yOffset + 0, moonColor1);
      setPixel(xOffset + 0, yOffset + 1, moonColor1);
      setPixel(xOffset + 0, yOffset + 2, moonColor2);
      setPixel(xOffset + 0, yOffset + 3, moonColor1);
      setPixel(xOffset + 1, yOffset + 4, moonColor1);

      // Seconda fila (interna - spessore)
      setPixel(xOffset + 2, yOffset + 0, moonColor2);
      setPixel(xOffset + 1, yOffset + 1, moonColor2);
      setPixel(xOffset + 1, yOffset + 2, moonColor1);
      setPixel(xOffset + 1, yOffset + 3, moonColor2);
      setPixel(xOffset + 2, yOffset + 4, moonColor2);

      // LED alle estremità per arrotondare (alto e basso)
      setPixel(xOffset + 3, yOffset + 0, moonColor2); // Estremità superiore
      setPixel(xOffset + 3, yOffset + 4, moonColor2); // Estremità inferiore

      // Cratere che pulsa
      if (animFrame == 0 || animFrame == 2) {
        setPixel(xOffset + 1, yOffset + 2, CRGB(140, 140, 170)); // Cratere
      }

    } else {
      // SOLE con nuvola - Poche nuvole diurno
      // Sole piccolo che pulsa
      CRGB sunColor;
      if (animFrame == 0 || animFrame == 2) {
        sunColor = CRGB(255, 200, 0); // Brillante
      } else {
        sunColor = CRGB(200, 150, 0); // Più scuro
      }
      for (int x = 1; x <= 3; x++) {
        for (int y = 1; y <= 3; y++) {
          setPixel(xOffset + x, yOffset + y, sunColor);
        }
      }
    }

    // NUVOLA IDENTICA ALL'ICONA "03" (solo nuvole) - SPOSTATA 3 LED DESTRA + 3 LED BASSO
    int moveOffset = animFrame % 2;
    int cloudXShift = 3; // Spostamento a destra
    int cloudYShift = 3; // Spostamento in basso

    // Colori con sfumature blu per realismo (UGUALI A ICONA 03)
    CRGB cloudColor1, cloudColor2, cloudColorBlue;
    if (animFrame == 0 || animFrame == 2) {
      cloudColor1 = CRGB(140, 140, 160); // Grigio con tinta blu
      cloudColor2 = CRGB(100, 100, 130); // Grigio più scuro blu
      cloudColorBlue = CRGB(80, 90, 150); // Blu grigio
    } else {
      cloudColor1 = CRGB(100, 100, 130); // Grigio più scuro blu
      cloudColor2 = CRGB(140, 140, 160); // Grigio con tinta blu
      cloudColorBlue = CRGB(60, 70, 130); // Blu grigio scuro
    }

    // Corpo principale nuvola - movimento ondulatorio
    for (int x = 1; x <= 6; x++) {
      if ((x + animFrame) % 2 == 0) {
        setPixel(xOffset + x + cloudXShift, yOffset + 3 + cloudYShift, cloudColor1);
        setPixel(xOffset + x + cloudXShift, yOffset + 4 + cloudYShift, cloudColor1);
      } else {
        setPixel(xOffset + x + cloudXShift, yOffset + 3 + cloudYShift, cloudColor2);
        setPixel(xOffset + x + cloudXShift, yOffset + 4 + cloudYShift, cloudColor2);
      }
    }

    // LED blu per profondità
    if (animFrame == 0 || animFrame == 2) {
      setPixel(xOffset + 2 + cloudXShift, yOffset + 3 + cloudYShift, cloudColorBlue);
      setPixel(xOffset + 5 + cloudXShift, yOffset + 4 + cloudYShift, cloudColorBlue);
    } else {
      setPixel(xOffset + 3 + cloudXShift, yOffset + 3 + cloudYShift, cloudColorBlue);
      setPixel(xOffset + 4 + cloudXShift, yOffset + 4 + cloudYShift, cloudColorBlue);
    }

    // Bordi nuvola - si muovono
    if (moveOffset == 0) {
      setPixel(xOffset + 0 + cloudXShift, yOffset + 4 + cloudYShift, CRGB(120, 120, 140));
      setPixel(xOffset + 7 + cloudXShift, yOffset + 4 + cloudYShift, CRGB(120, 120, 140));
    } else {
      setPixel(xOffset + 1 + cloudXShift, yOffset + 4 + cloudYShift, CRGB(100, 100, 120));
      setPixel(xOffset + 6 + cloudXShift, yOffset + 4 + cloudYShift, CRGB(100, 100, 120));
    }

    // Parte alta nuvola con sfumatura blu
    for (int x = 2; x <= 5; x++) {
      if (x == 3 || x == 4) {
        setPixel(xOffset + x + cloudXShift, yOffset + 2 + cloudYShift, cloudColorBlue); // Centro più blu
      } else {
        setPixel(xOffset + x + cloudXShift, yOffset + 2 + cloudYShift, cloudColor1);
      }
    }
  }
  else if (icon.startsWith("03") || icon.startsWith("04")) {  // Clouds - Nuvole ANIMATE
    // Movimento più veloce - cambio ogni frame
    int moveOffset = animFrame % 2; // Alterna 0,1,0,1 più velocemente

    // Colori con sfumature blu per realismo
    CRGB cloudColor1, cloudColor2, cloudColorBlue;
    if (animFrame == 0 || animFrame == 2) {
      cloudColor1 = CRGB(140, 140, 160); // Grigio con tinta blu
      cloudColor2 = CRGB(100, 100, 130); // Grigio più scuro blu
      cloudColorBlue = CRGB(80, 90, 150); // Blu grigio
    } else {
      cloudColor1 = CRGB(100, 100, 130); // Grigio più scuro blu
      cloudColor2 = CRGB(140, 140, 160); // Grigio con tinta blu
      cloudColorBlue = CRGB(60, 70, 130); // Blu grigio scuro
    }

    // Corpo principale nuvola - movimento ondulatorio più evidente
    for (int x = 1; x <= 6; x++) {
      // Alterna pattern più velocemente
      if ((x + animFrame) % 2 == 0) {
        setPixel(xOffset + x, yOffset + 3, cloudColor1);
        setPixel(xOffset + x, yOffset + 4, cloudColor1);
      } else {
        setPixel(xOffset + x, yOffset + 3, cloudColor2);
        setPixel(xOffset + x, yOffset + 4, cloudColor2);
      }
    }

    // Aggiungi LED blu per profondità
    if (animFrame == 0 || animFrame == 2) {
      setPixel(xOffset + 2, yOffset + 3, cloudColorBlue);
      setPixel(xOffset + 5, yOffset + 4, cloudColorBlue);
    } else {
      setPixel(xOffset + 3, yOffset + 3, cloudColorBlue);
      setPixel(xOffset + 4, yOffset + 4, cloudColorBlue);
    }

    // Bordi nuvola - si muovono più velocemente
    if (moveOffset == 0) {
      setPixel(xOffset + 0, yOffset + 4, CRGB(120, 120, 140));
      setPixel(xOffset + 7, yOffset + 4, CRGB(120, 120, 140));
    } else {
      setPixel(xOffset + 1, yOffset + 4, CRGB(100, 100, 120));
      setPixel(xOffset + 6, yOffset + 4, CRGB(100, 100, 120));
    }

    // Parte alta nuvola con sfumatura blu
    for (int x = 2; x <= 5; x++) {
      if (x == 3 || x == 4) {
        setPixel(xOffset + x, yOffset + 2, cloudColorBlue); // Centro più blu
      } else {
        setPixel(xOffset + x, yOffset + 2, cloudColor1);
      }
    }
  }
  else if (icon.startsWith("09") || icon.startsWith("10")) {  // Rain - Pioggia ANIMATA
    // Nuvola
    for (int x = 1; x <= 6; x++) {
      setPixel(xOffset + x, yOffset + 1, CRGB(100, 100, 150));
      setPixel(xOffset + x, yOffset + 2, CRGB(100, 100, 150));
    }
    // Gocce animate che cadono (3 colonne di gocce)
    // Goccia sinistra
    int pos1 = (animFrame) % 4;
    if (pos1 < 3) {
      setPixel(xOffset + 2, yOffset + 4 + pos1, CRGB(0, 150, 255));
    }

    // Goccia centro
    int pos2 = (animFrame + 1) % 4;
    if (pos2 < 3) {
      setPixel(xOffset + 4, yOffset + 4 + pos2, CRGB(0, 150, 255));
    }

    // Goccia destra
    int pos3 = (animFrame + 2) % 4;
    if (pos3 < 3) {
      setPixel(xOffset + 6, yOffset + 4 + pos3, CRGB(0, 150, 255));
    }
  }
  else if (icon.startsWith("11")) {  // Thunderstorm - Temporale ANIMATO
    // Nuvola scura
    for (int x = 1; x <= 6; x++) {
      setPixel(xOffset + x, yOffset + 1, CRGB(50, 50, 80));
      setPixel(xOffset + x, yOffset + 2, CRGB(50, 50, 80));
    }
    // Fulmine lampeggiante - molto evidente
    if (animFrame == 0 || animFrame == 2) {
      // Fulmine acceso - BRILLANTE
      setPixel(xOffset + 4, yOffset + 3, CRGB(255, 255, 100));
      setPixel(xOffset + 3, yOffset + 4, CRGB(255, 255, 100));
      setPixel(xOffset + 4, yOffset + 5, CRGB(255, 255, 100));
      setPixel(xOffset + 3, yOffset + 6, CRGB(255, 255, 100));
    }
    // Altrimenti fulmine spento (buio)
  }
  else if (icon.startsWith("13")) {  // Snow - Neve ANIMATA
    // Nuvola
    for (int x = 1; x <= 6; x++) {
      setPixel(xOffset + x, yOffset + 1, CRGB(150, 150, 180));
    }
    // Fiocchi di neve che cadono
    int pos1 = (animFrame) % 5;
    if (pos1 < 4) {
      setPixel(xOffset + 2, yOffset + 3 + pos1, CRGB(255, 255, 255));
    }

    int pos2 = (animFrame + 2) % 5;
    if (pos2 < 4) {
      setPixel(xOffset + 4, yOffset + 3 + pos2, CRGB(255, 255, 255));
    }

    int pos3 = (animFrame + 1) % 5;
    if (pos3 < 4) {
      setPixel(xOffset + 6, yOffset + 3 + pos3, CRGB(255, 255, 255));
    }
  }
  else if (icon.startsWith("50")) {  // Mist - Nebbia ANIMATA
    // Nebbia che ondeggia (linee orizzontali che si muovono)
    int mistOffset = (animFrame == 0 || animFrame == 2) ? 0 : 1;

    // Colori alternati per effetto nebbia densa/leggera
    CRGB mistColor1, mistColor2;
    if (animFrame == 0 || animFrame == 2) {
      mistColor1 = CRGB(180, 180, 180); // Più chiaro
      mistColor2 = CRGB(140, 140, 140); // Più scuro
    } else {
      mistColor1 = CRGB(140, 140, 140); // Più scuro
      mistColor2 = CRGB(180, 180, 180); // Più chiaro
    }

    // Linee di nebbia alternate
    for (int y = 2; y <= 5; y++) {
      CRGB lineColor = (y % 2 == 0) ? mistColor1 : mistColor2;
      for (int x = 1; x <= 6; x += 2) {
        if (xOffset + x + mistOffset < xOffset + 8) {
          setPixel(xOffset + x + mistOffset, yOffset + y, lineColor);
        }
      }
    }

    // Aggiungi qualche pixel extra per densità
    if (animFrame == 1 || animFrame == 3) {
      setPixel(xOffset + 2, yOffset + 3, CRGB(160, 160, 160));
      setPixel(xOffset + 5, yOffset + 4, CRGB(160, 160, 160));
    }
  }
}

void drawSmallDigit(int digit, int x, int y, CRGB color) {
  // Numeri 3x5 pixel
  switch(digit) {
    case 0:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 1:
      setPixel(x+1, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 2:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 3:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 4:
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      break;
    case 5:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 6:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 7:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      setPixel(x+1, y+4, color);
      break;
    case 8:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 9:
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
  }
}

void drawWeatherOnMatrix() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastAnimUpdate = 0;
  static int animFrame = 0;
  static bool firstDraw = true;

  // Reset quando si entra nello stato meteo o quando c'è un force redraw
  if (previousState != STATE_GAME_WEATHER && currentState == STATE_GAME_WEATHER) {
    firstDraw = true;
    lastUpdate = 0;
    lastAnimUpdate = 0;
    animFrame = 0;
  }

  if (forceRedraw) {
    firstDraw = true;
    lastUpdate = 0;
    lastAnimUpdate = 0;
    animFrame = 0;
  }

  // Aggiorna animazione ogni 500ms
  if (millis() - lastAnimUpdate > 500) {
    animFrame = (animFrame + 1) % 4; // 4 frame di animazione
    lastAnimUpdate = millis();
  }

  // Ridisegna tutto solo ogni 5 secondi o al primo avvio
  if (millis() - lastUpdate > 5000 || lastUpdate == 0 || firstDraw) {
    clearMatrixNoShow();
    firstDraw = false;

    if (!currentWeather.isValid) {
      // Mostra messaggio "NO DATA"
      drawCharacter('N', 2, 6, CRGB(255, 0, 0));
      drawCharacter('O', 6, 6, CRGB(255, 0, 0));
      FastLED.show();
      lastUpdate = millis();
      return;
    }

    // ===== ICONA METEO CENTRATA IN ALTO (8x8) =====
    // Posizione: x=4 (centrata), y=0
    drawWeatherIcon(currentWeather.icon, 4, 0, CRGB(255, 255, 255), animFrame);

    // ===== TEMPERATURA A SINISTRA =====
    int temp = currentWeather.temperature;
    CRGB tempColor = CRGB(255, 255, 255); // BIANCO

    int tempX = 0;
    int tempY = 9;

    // Temperatura (es: "23°" o "-5°")
    if (temp < 0) {
      // Segno meno
      setPixel(tempX, tempY+2, tempColor);
      setPixel(tempX+1, tempY+2, tempColor);
      tempX += 2;
    }

    if (abs(temp) >= 10) {
      int digit1 = abs(temp) / 10;
      int digit2 = abs(temp) % 10;
      drawSmallDigit(digit1, tempX, tempY, tempColor);
      drawSmallDigit(digit2, tempX + 4, tempY, tempColor);
      // Simbolo gradi (°) - solo LED superiore - distanziato di 1 LED
      setPixel(tempX + 8, tempY, tempColor);
    } else {
      int digit2 = abs(temp) % 10;
      drawSmallDigit(digit2, tempX, tempY, tempColor);
      // Simbolo gradi (°) - solo LED superiore - distanziato di 1 LED
      setPixel(tempX + 4, tempY, tempColor);
    }

    // ===== UMIDITÀ A DESTRA =====
    int humidity = currentWeather.humidity;
    CRGB humidityColor = humidity > 70 ? CRGB(0, 100, 255) :
                             humidity > 40 ? CRGB(0, 150, 200) :
                             CRGB(100, 100, 255);

    int humX = 9;
    int humY = 9;

    // Umidità (es: "65%" o "100%")
    if (humidity >= 100) {
      drawSmallDigit(1, humX, humY, humidityColor);
      drawSmallDigit(0, humX + 4, humY, humidityColor);
      drawSmallDigit(0, humX + 8, humY, humidityColor);
      // Simbolo %
      setPixel(humX + 11, humY, humidityColor);
      setPixel(humX + 11, humY + 4, humidityColor);
      setPixel(humX + 12, humY + 2, humidityColor);
      setPixel(humX + 13, humY, humidityColor);
      setPixel(humX + 13, humY + 4, humidityColor);
    } else {
      int digit1 = humidity / 10;
      int digit2 = humidity % 10;
      drawSmallDigit(digit1, humX, humY, humidityColor);
      drawSmallDigit(digit2, humX + 4, humY, humidityColor);
      // Simbolo %
      setPixel(humX + 7, humY, humidityColor);
      setPixel(humX + 7, humY + 4, humidityColor);
      setPixel(humX + 8, humY + 2, humidityColor);
      setPixel(humX + 9, humY, humidityColor);
      setPixel(humX + 9, humY + 4, humidityColor);
    }

    // ===== BARRA VENTO IN BASSO =====
    // Velocità vento (km/h) mappata su 16 LED
    int windBars = map(currentWeather.windSpeed, 0, 50, 0, 16); // Max 50 km/h = barra piena
    if (windBars > 16) windBars = 16;

    CRGB windColor;
    for (int x = 0; x < windBars; x++) {
      // Colore progressivo: verde->giallo->rosso
      if (x < 5) {
        windColor = CRGB(0, 255, 0); // Verde (vento leggero)
      } else if (x < 11) {
        windColor = CRGB(255, 200, 0); // Giallo (vento moderato)
      } else {
        windColor = CRGB(255, 50, 0); // Rosso (vento forte)
      }
      setPixel(x, 15, windColor);
    }

    FastLED.show();
    lastUpdate = millis();
  } else {
    // Aggiorna solo l'icona animata senza clear
    drawWeatherIcon(currentWeather.icon, 4, 0, CRGB(255, 255, 255), animFrame);
    FastLED.show();
  }
}

void handleWeather() {
  WiFiConfig config;
  EEPROM.get(0, config);

  // Open-Meteo non richiede API key, verifica solo le coordinate
  bool hasCoordinates = (config.weatherLatitude != 0.0 || config.weatherLongitude != 0.0);
  bool hasCity = strlen(config.weatherCity) > 0;

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Stazione Meteo</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:800px;margin:0 auto;background:#1a1a1a;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(255,255,255,0.1);border:1px solid #333;}";
  html += ".weather-card{background:#2a2a2a;padding:20px;margin:20px 0;border-radius:10px;border:1px solid #444;}";
  html += ".weather-icon{font-size:64px;margin:10px;}";
  html += ".temperature{font-size:48px;font-weight:bold;color:#03A9F4;}";
  html += ".description{font-size:24px;margin:10px;text-transform:capitalize;}";
  html += ".weather-details{display:grid;grid-template-columns:repeat(2,1fr);gap:15px;margin-top:20px;}";
  html += ".detail-item{background:#1a1a1a;padding:15px;border-radius:5px;}";
  html += ".detail-label{color:#aaa;font-size:14px;}";
  html += ".detail-value{font-size:24px;font-weight:bold;margin-top:5px;}";
  html += ".forecast{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:15px;margin-top:20px;}";
  html += ".forecast-day{background:#2a2a2a;padding:15px;border-radius:8px;border:1px solid #444;}";
  html += ".forecast-date{font-size:14px;color:#aaa;margin-bottom:10px;}";
  html += ".forecast-icon{font-size:32px;margin:10px 0;}";
  html += ".forecast-temp{font-size:18px;font-weight:bold;}";
  html += ".forecast-desc{font-size:12px;margin-top:5px;}";
  html += "button{padding:15px 30px;margin:10px;background:#03A9F4;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;}";
  html += "button:hover{background:#0288D1;}";
  html += ".config-btn{background:#FF9800;}";
  html += ".config-btn:hover{background:#F57C00;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".warning{background:#ff5722;padding:15px;border-radius:5px;margin:20px 0;}";
  html += "h1,h2,h3,p{color:#fff;}";
  html += "</style>";
  html += "<script>";
  html += "function updateWeather(){";
  html += "document.getElementById('status').innerText='⏳ Scaricamento dati meteo in corso...';";
  html += "document.getElementById('status').style.color='#FFC107';";
  html += "fetch('/weatherupdate').then(r=>r.text()).then(data=>{";
  html += "document.getElementById('status').innerText=data;";
  html += "document.getElementById('status').style.color='#4CAF50';";
  html += "setTimeout(()=>location.reload(),3000);";
  html += "}).catch(err=>{";
  html += "document.getElementById('status').innerText='❌ Errore di connessione';";
  html += "document.getElementById('status').style.color='#f44336';";
  html += "});";
  html += "}";
  html += "function showOnMatrix(){";
  html += "document.getElementById('status').innerText='📺 Attivazione visualizzazione...';";
  html += "document.getElementById('status').style.color='#FFC107';";
  html += "fetch('/weatherupdate?display=1').then(r=>r.text()).then(data=>{";
  html += "document.getElementById('status').innerText=data;";
  html += "document.getElementById('status').style.color='#4CAF50';";
  html += "}).catch(err=>{";
  html += "document.getElementById('status').innerText='❌ Errore';";
  html += "document.getElementById('status').style.color='#f44336';";
  html += "});";
  html += "}";
  html += "setInterval(()=>{location.reload();},300000);";  // Auto-refresh ogni 5 minuti
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>🌤️ Stazione Meteo</h1>";

  if (!hasCoordinates || !hasCity) {
    html += "<div class='warning'>";
    html += "<h2>⚠️ Configurazione Mancante</h2>";
    html += "<p>Per utilizzare la stazione meteo, inserisci il nome della tua città.</p>";
    html += "<p><a href='/weatherconfig'><button class='config-btn'>⚙️ Configura Meteo</button></a></p>";
    html += "</div>";
  } else {
    html += "<button class='config-btn' onclick=\"location.href='/weatherconfig'\">⚙️ Configura</button>";
    html += "<button onclick='updateWeather()'>🔄 Aggiorna Dati</button>";
    html += "<button onclick='showOnMatrix()'>📺 Mostra su Matrice</button>";

    if (currentWeather.isValid) {
      html += "<div class='weather-card'>";
      html += "<h2>" + currentWeather.city + "</h2>";
      html += "<div class='weather-icon'>";
      String iconCode = currentWeather.icon;
      if (iconCode.startsWith("01")) html += "☀️";
      else if (iconCode.startsWith("02")) html += "⛅";
      else if (iconCode.startsWith("03")) html += "☁️";
      else if (iconCode.startsWith("04")) html += "☁️";
      else if (iconCode.startsWith("09")) html += "🌧️";
      else if (iconCode.startsWith("10")) html += "🌦️";
      else if (iconCode.startsWith("11")) html += "⛈️";
      else if (iconCode.startsWith("13")) html += "❄️";
      else if (iconCode.startsWith("50")) html += "🌫️";
      html += "</div>";
      html += "<div class='temperature'>" + String(currentWeather.temperature) + "°C</div>";
      html += "<div class='description'>" + currentWeather.description + "</div>";
      html += "<div class='weather-details'>";
      html += "<div class='detail-item'><div class='detail-label'>💧 Umidità</div><div class='detail-value'>" + String(currentWeather.humidity) + "%</div></div>";
      html += "<div class='detail-item'><div class='detail-label'>🌬️ Vento</div><div class='detail-value'>" + String(currentWeather.windSpeed) + " km/h</div></div>";
      html += "<div class='detail-item'><div class='detail-label'>🔽 Pressione</div><div class='detail-value'>" + String(currentWeather.pressure) + " hPa</div></div>";
      unsigned long minutesSinceUpdate = (millis() - lastWeatherUpdate) / 60000;
      unsigned long minutesUntilUpdate = minutesSinceUpdate < 10 ? 10 - minutesSinceUpdate : 0;
      html += "<div class='detail-item'><div class='detail-label'>🕐 Ultimo Agg.</div><div class='detail-value'>" + String(minutesSinceUpdate) + " min fa</div>";
      if (minutesUntilUpdate > 0) {
        html += "<div style='font-size:12px;color:#aaa;margin-top:5px;'>Prossimo: " + String(minutesUntilUpdate) + " min</div>";
      } else {
        html += "<div style='font-size:12px;color:#4CAF50;margin-top:5px;'>⏳ Aggiornamento in corso...</div>";
      }
      html += "</div>";
      html += "</div></div>";

      // Previsioni 5 giorni
      html += "<h2 style='margin-top:40px;'>Previsioni 5 Giorni</h2>";
      html += "<div class='forecast'>";
      for (int i = 0; i < 5; i++) {
        if (forecast[i].date.length() > 0) {
          html += "<div class='forecast-day'>";
          html += "<div class='forecast-date'>" + forecast[i].date + "</div>";
          html += "<div class='forecast-icon'>";
          String fIconCode = forecast[i].icon;
          if (fIconCode.startsWith("01")) html += "☀️";
          else if (fIconCode.startsWith("02")) html += "⛅";
          else if (fIconCode.startsWith("03")) html += "☁️";
          else if (fIconCode.startsWith("04")) html += "☁️";
          else if (fIconCode.startsWith("09")) html += "🌧️";
          else if (fIconCode.startsWith("10")) html += "🌦️";
          else if (fIconCode.startsWith("11")) html += "⛈️";
          else if (fIconCode.startsWith("13")) html += "❄️";
          else if (fIconCode.startsWith("50")) html += "🌫️";
          html += "</div>";
          html += "<div class='forecast-temp'>" + String(forecast[i].tempMin) + "° / " + String(forecast[i].tempMax) + "°</div>";
          html += "<div class='forecast-desc'>" + forecast[i].description + "</div>";
          html += "<div class='detail-label' style='margin-top:10px;'>💧 " + String(forecast[i].humidity) + "%</div>";
          html += "</div>";
        }
      }
      html += "</div>";
    } else {
      html += "<div class='warning'>";
      html += "<p>Nessun dato meteo disponibile. Clicca 'Aggiorna Dati' per scaricare le informazioni meteo.</p>";
      html += "</div>";
    }
  }

  html += "<div id='status' style='margin-top:20px;color:#4CAF50;font-size:18px;'></div>";
  html += "<p style='margin-top:30px;color:#aaa;'>I dati vengono aggiornati automaticamente ogni 10 minuti sulla matrice.</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleWeatherConfig() {
  WiFiConfig config;
  EEPROM.get(0, config);

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Configurazione Meteo</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#1a1a1a;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(255,255,255,0.1);border:1px solid #333;}";
  html += "input{padding:15px;margin:15px;width:90%;max-width:400px;border:1px solid #444;border-radius:5px;font-size:16px;background:#222;color:#fff;}";
  html += "button{padding:15px 30px;background:#4CAF50;color:white;border:none;border-radius:5px;font-size:18px;cursor:pointer;margin:10px;}";
  html += "button:hover{background:#45a049;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".info{background:#2a2a2a;padding:15px;margin:20px 0;border-radius:5px;border:1px solid #444;text-align:left;}";
  html += ".coords{background:#1a3a1a;padding:10px;margin:10px 0;border-radius:5px;border:1px solid #4CAF50;}";
  html += "h1,h2,h3,p{color:#fff;}";
  html += "a{color:#03A9F4;}";
  html += "code{background:#333;padding:2px 6px;border-radius:3px;color:#4CAF50;}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/weather'>← Indietro</a></div>";
  html += "<div class='container'>";
  html += "<h1>⚙️ Configurazione Meteo</h1>";
  html += "<div class='info'>";
  html += "<h3>🌍 Open-Meteo API (Gratuita)</h3>";
  html += "<p style='text-align:left;'>Utilizza <a href='https://open-meteo.com/' target='_blank'>Open-Meteo</a>, un servizio meteo gratuito e open source.</p>";
  html += "<p style='text-align:left;'><strong>Non serve API Key!</strong> Basta inserire il nome della città.</p>";
  html += "<p style='margin-top:10px;'><strong>Esempi di città italiane:</strong></p>";
  html += "<ul style='text-align:left;'>";
  html += "<li><code>Roma</code></li>";
  html += "<li><code>Milano</code></li>";
  html += "<li><code>Napoli</code></li>";
  html += "<li><code>Torino</code></li>";
  html += "</ul>";
  html += "</div>";

  // Mostra coordinate attuali se configurate
  if (config.weatherLatitude != 0.0 || config.weatherLongitude != 0.0) {
    html += "<div class='coords'>";
    html += "<p><strong>Coordinate attuali:</strong></p>";
    html += "<p>Lat: " + String(config.weatherLatitude, 4) + " | Lon: " + String(config.weatherLongitude, 4) + "</p>";
    html += "</div>";
  }

  html += "<form action='/saveweatherconfig' method='post' id='weatherForm'>";
  html += "<input type='text' name='city' id='city' placeholder='Nome della città (es: Roma, Milano...)' value='" + String(config.weatherCity) + "' required minlength='2' maxlength='32'><br>";
  html += "<button type='submit'>💾 Salva e Scarica Dati Meteo</button>";
  html += "</form>";
  html += "<p style='margin-top:20px;'><a href='/weathertest'><button style='background:#2196F3;'>🔧 Test Connessione</button></a></p>";
  html += "<p style='margin-top:30px;color:#aaa;'>Le coordinate verranno rilevate automaticamente dal nome della città.</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleSaveWeatherConfig() {
  if (server.hasArg("city")) {
    String city = server.arg("city");

    // FIX: Usa chunked transfer per inviare subito una risposta al browser
    // e mantenere la connessione attiva durante le operazioni lunghe
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    // Invia header HTML immediatamente
    server.sendContent("<!DOCTYPE html><html><head>");
    server.sendContent("<meta charset='UTF-8'>");
    server.sendContent("<style>");
    server.sendContent("body{text-align:center;padding:50px;font-family:Arial,sans-serif;color:white;}");
    server.sendContent(".loading{font-size:24px;margin:20px;}");
    server.sendContent("@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}");
    server.sendContent(".spinner{border:4px solid #f3f3f3;border-top:4px solid #3498db;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;}");
    server.sendContent("</style>");
    server.sendContent("</head><body style='background:#2196F3;'>");
    server.sendContent("<h1>⏳ Ricerca città in corso...</h1>");
    server.sendContent("<div class='spinner'></div>");
    server.sendContent("<p class='loading'>Ricerca coordinate per: <strong>" + city + "</strong></p>");

    yield(); // Permette al WiFi di inviare i dati

    WiFiConfig config;
    EEPROM.get(0, config);
    city.toCharArray(config.weatherCity, sizeof(config.weatherCity));

    // Geocoding: ottieni coordinate dalla città usando Open-Meteo
    float lat = 0.0, lon = 0.0;

    server.sendContent("<p>Connessione al server geocoding...</p>");
    yield();

    bool geocodeSuccess = getCoordinatesFromCity(city, lat, lon);

    if (geocodeSuccess) {
      config.weatherLatitude = lat;
      config.weatherLongitude = lon;

      EEPROM.put(0, config);
      if (EEPROM.commit()) {
        Serial.println("Weather config saved with coordinates");
        Serial.println("City: " + city);
        Serial.println("Lat: " + String(lat, 4) + ", Lon: " + String(lon, 4));

        server.sendContent("<p style='color:#90EE90;'>✅ Città trovata! Coordinate: " + String(lat, 4) + ", " + String(lon, 4) + "</p>");
        server.sendContent("<p>Download dati meteo in corso...</p>");
        yield();

        // Scarica immediatamente i dati meteo
        bool updateSuccess = false;
        if (updateWeatherData()) {
          updateForecastData();
          weatherDataAvailable = true;
          lastWeatherUpdate = millis();
          changeState(STATE_GAME_WEATHER);
          updateSuccess = true;
          Serial.println("Weather data downloaded successfully after config save");
        }

        // Cambia colore sfondo e mostra risultato finale
        server.sendContent("<script>document.body.style.background='#4CAF50';</script>");
        server.sendContent("<h1>✅ Configurazione Salvata!</h1>");
        server.sendContent("<p>Città: " + city + "</p>");
        server.sendContent("<p>Coordinate: " + String(lat, 4) + ", " + String(lon, 4) + "</p>");
        if (updateSuccess) {
          server.sendContent("<p>✅ Dati meteo scaricati con successo!</p>");
          server.sendContent("<p>📺 Visualizzazione sulla matrice attivata!</p>");
        } else {
          server.sendContent("<p>⚠️ Errore nel download dei dati meteo. Riprova più tardi.</p>");
        }
        server.sendContent("<p>Reindirizzamento alla pagina meteo in 3 secondi...</p>");
        server.sendContent("<script>setTimeout(function(){window.location.href='/weather';},3000);</script>");
      } else {
        server.sendContent("<script>document.body.style.background='#f44336';</script>");
        server.sendContent("<h1>❌ Errore</h1>");
        server.sendContent("<p>Errore nel salvataggio EEPROM</p>");
        server.sendContent("<p><a href='/weatherconfig' style='color:white;'>Torna alla configurazione</a></p>");
      }
    } else {
      // Geocoding fallito
      server.sendContent("<script>document.body.style.background='#f44336';</script>");
      server.sendContent("<h1>❌ Città non trovata</h1>");
      server.sendContent("<p>Non è stato possibile trovare le coordinate per: <strong>" + city + "</strong></p>");
      server.sendContent("<p>Verifica il nome della città e riprova.</p>");
      server.sendContent("<p>Esempi validi: Roma, Milano, Napoli, Torino...</p>");
      server.sendContent("<p>Reindirizzamento alla configurazione in 5 secondi...</p>");
      server.sendContent("<script>setTimeout(function(){window.location.href='/weatherconfig';},5000);</script>");
    }

    server.sendContent("</body></html>");
    server.sendContent(""); // Chiude chunked transfer
  } else {
    server.send(400, "text/html", "Parametri mancanti");
  }
}

void handleWeatherUpdate() {
  if (server.hasArg("display")) {
    if (currentWeather.isValid) {
      changeState(STATE_GAME_WEATHER);
      server.send(200, "text/plain", "✅ Visualizzazione meteo sulla matrice attivata!");
    } else {
      server.send(400, "text/plain", "❌ Nessun dato meteo disponibile. Aggiorna prima i dati.");
    }
    return;
  }

  Serial.println("Starting weather data update...");
  bool weatherOk = updateWeatherData();
  bool forecastOk = false;

  if (weatherOk) {
    forecastOk = updateForecastData();
    weatherDataAvailable = true;
    lastWeatherUpdate = millis();
    changeState(STATE_GAME_WEATHER);

    String response = "✅ Dati meteo aggiornati con successo!\n";
    response += "Città: " + currentWeather.city + "\n";
    response += "Temperatura: " + String(currentWeather.temperature) + "°C\n";
    response += "Condizioni: " + currentWeather.description + "\n";
    if (forecastOk) {
      response += "Previsioni 5 giorni scaricate.\n";
    }
    response += "📺 Visualizzazione sulla matrice attivata!";

    server.send(200, "text/plain", response);
  } else {
    String errorMsg = "❌ Errore nell'aggiornamento dei dati meteo.\n\n";
    errorMsg += "Possibili cause:\n";
    errorMsg += "• Coordinate non configurate\n";
    errorMsg += "• Nessuna connessione internet\n";
    errorMsg += "• Server Open-Meteo non raggiungibile\n\n";
    errorMsg += "Verifica i log seriali per dettagli.";
    server.send(500, "text/plain", errorMsg);
  }
}

// Test di connessione HTTPS per diagnostica
String testHttpsConnection(const char* host, String path) {
  HTTPClient http;
  String url = "https://" + String(host) + path;

  Serial.println("Test URL: " + url);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    return response;
  }

  http.end();
  if (httpCode < 0) {
    return "CONNECTION_FAILED";
  }
  return "HTTP_ERROR_" + String(httpCode);
}

void handleWeatherTest() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Test Connessione Meteo</title>";
  html += "<style>body{font-family:monospace;background:#1a1a2e;color:#fff;padding:20px;}";
  html += ".ok{color:#4CAF50;}.err{color:#f44336;}.warn{color:#ff9800;}</style></head><body>";
  html += "<h1>🔧 Test Connessione Meteo</h1><pre>";

  // Test 1: WiFi
  html += "\n[1] Verifica WiFi... ";
  if (WiFi.status() == WL_CONNECTED) {
    html += "<span class='ok'>OK</span>\n";
    html += "    IP: " + WiFi.localIP().toString() + "\n";
    html += "    RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    html += "    Free heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  } else {
    html += "<span class='err'>ERRORE - WiFi non connesso!</span>\n";
    html += "</pre><p><a href='/'>Torna alla Home</a></p></body></html>";
    server.send(200, "text/html", html);
    return;
  }

  // Test 2: Configurazione
  WiFiConfig config;
  EEPROM.get(0, config);
  html += "\n[2] Verifica configurazione... ";
  html += "<span class='ok'>OK</span>\n";
  html += "    Città: " + String(config.weatherCity) + "\n";
  html += "    Lat: " + String(config.weatherLatitude, 4) + "\n";
  html += "    Lon: " + String(config.weatherLongitude, 4) + "\n";

  bool hasCoords = (config.weatherLatitude != 0.0 || config.weatherLongitude != 0.0);
  if (!hasCoords) {
    html += "    <span class='warn'>⚠ Coordinate non impostate - serve geocoding</span>\n";
  }

  // Test 3: Connessione HTTP a Open-Meteo (Geocoding)
  html += "\n[3] Test connessione HTTP (geocoding)... ";

  String response = testHttpsConnection("geocoding-api.open-meteo.com", "/v1/search?name=Roma&count=1&language=it&format=json");

  if (response == "CONNECTION_FAILED") {
    html += "<span class='err'>ERRORE - Connessione fallita!</span>\n";
    html += "    Impossibile connettersi al server HTTP\n";
  } else if (response == "TIMEOUT") {
    html += "<span class='err'>ERRORE - Timeout!</span>\n";
    html += "    Il server non ha risposto in tempo\n";
  } else if (response.length() > 0) {
    html += "<span class='ok'>OK - Connesso!</span>\n";
    html += "    Risposta ricevuta: " + String(response.length()) + " bytes\n";

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (!error && doc.containsKey("results")) {
      html += "    <span class='ok'>JSON valido - Geocoding funziona!</span>\n";
      if (doc["results"].size() > 0) {
        float lat = doc["results"][0]["latitude"].as<float>();
        float lon = doc["results"][0]["longitude"].as<float>();
        html += "    Roma: Lat=" + String(lat, 4) + ", Lon=" + String(lon, 4) + "\n";
      }
    } else {
      html += "    <span class='err'>Errore parsing JSON: " + String(error.c_str()) + "</span>\n";
    }
  } else {
    html += "<span class='err'>ERRORE - Risposta vuota!</span>\n";
  }

  // Test 4: Connessione API meteo (se abbiamo coordinate)
  if (hasCoords) {
    html += "\n[4] Test API meteo... ";

    String path = "/v1/forecast?latitude=" + String(config.weatherLatitude, 4) +
                  "&longitude=" + String(config.weatherLongitude, 4) +
                  "&current=temperature_2m&timezone=Europe/Rome";

    String response2 = testHttpsConnection("api.open-meteo.com", path);

    if (response2 == "CONNECTION_FAILED") {
      html += "<span class='err'>ERRORE - Connessione fallita!</span>\n";
    } else if (response2 == "TIMEOUT") {
      html += "<span class='err'>ERRORE - Timeout!</span>\n";
    } else if (response2.length() > 0) {
      html += "<span class='ok'>OK - Connesso!</span>\n";

      StaticJsonDocument<1024> doc2;
      DeserializationError error2 = deserializeJson(doc2, response2);
      if (!error2 && doc2.containsKey("current")) {
        float temp = doc2["current"]["temperature_2m"].as<float>();
        html += "    <span class='ok'>Temperatura attuale: " + String(temp, 1) + "°C</span>\n";
      } else {
        html += "    <span class='err'>Errore parsing: " + String(error2.c_str()) + "</span>\n";
      }
    } else {
      html += "<span class='err'>ERRORE - Risposta vuota!</span>\n";
    }
  } else {
    html += "\n[4] Test API meteo... <span class='warn'>SALTATO (configura prima la città)</span>\n";
  }

  html += "\n</pre>";
  html += "<p><a href='/weatherconfig'><button style='padding:10px 20px;font-size:16px;'>⚙️ Configura Meteo</button></a> ";
  html += "<a href='/weather'><button style='padding:10px 20px;font-size:16px;'>🌤️ Vai al Meteo</button></a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================
void displayWiFiSetupMode() {
  clearMatrix();

  CRGB wifiColor = CRGB(0, 255, 255); // Ciano

  // Punto centrale (trasmettitore) - riga 15
  setPixel(6, 15, wifiColor);
  setPixel(7, 15, wifiColor);
  setPixel(8, 15, wifiColor);
  setPixel(9, 15, wifiColor);

  // Onda 1 (piccola) - righe 12-13
  setPixel(6, 12, wifiColor);
  setPixel(7, 12, wifiColor);
  setPixel(8, 12, wifiColor);
  setPixel(9, 12, wifiColor);
  setPixel(5, 13, wifiColor);
  setPixel(6, 13, wifiColor);
  setPixel(9, 13, wifiColor);
  setPixel(10, 13, wifiColor);

  // Onda 2 (media) - righe 9-10
  setPixel(5, 9, wifiColor);
  setPixel(6, 9, wifiColor);
  setPixel(7, 9, wifiColor);
  setPixel(8, 9, wifiColor);
  setPixel(9, 9, wifiColor);
  setPixel(10, 9, wifiColor);
  setPixel(4, 10, wifiColor);
  setPixel(5, 10, wifiColor);
  setPixel(10, 10, wifiColor);
  setPixel(11, 10, wifiColor);

  // Onda 3 (grande) - righe 5-7
  setPixel(4, 5, wifiColor);
  setPixel(5, 5, wifiColor);
  setPixel(6, 5, wifiColor);
  setPixel(7, 5, wifiColor);
  setPixel(8, 5, wifiColor);
  setPixel(9, 5, wifiColor);
  setPixel(10, 5, wifiColor);
  setPixel(11, 5, wifiColor);
  setPixel(3, 6, wifiColor);
  setPixel(4, 6, wifiColor);
  setPixel(11, 6, wifiColor);
  setPixel(12, 6, wifiColor);
  setPixel(2, 7, wifiColor);
  setPixel(3, 7, wifiColor);
  setPixel(12, 7, wifiColor);
  setPixel(13, 7, wifiColor);

  // Onda 4 (molto grande) - righe 0-3
  setPixel(4, 0, wifiColor);
  setPixel(5, 0, wifiColor);
  setPixel(6, 0, wifiColor);
  setPixel(7, 0, wifiColor);
  setPixel(8, 0, wifiColor);
  setPixel(9, 0, wifiColor);
  setPixel(10, 0, wifiColor);
  setPixel(11, 0, wifiColor);
  setPixel(3, 1, wifiColor);
  setPixel(4, 1, wifiColor);
  setPixel(11, 1, wifiColor);
  setPixel(12, 1, wifiColor);
  setPixel(2, 2, wifiColor);
  setPixel(3, 2, wifiColor);
  setPixel(12, 2, wifiColor);
  setPixel(13, 2, wifiColor);
  setPixel(1, 3, wifiColor);
  setPixel(2, 3, wifiColor);
  setPixel(13, 3, wifiColor);
  setPixel(14, 3, wifiColor);

  FastLED.show();
}

void displayIP() {
  if (wifiConnected) {
    String ipStr = WiFi.localIP().toString();
    scrollText = "IP: " + ipStr;
    changeState(STATE_GAME_TEXT_SCROLL);
    lastScrollUpdate = millis();
  }
}

void displayStaticIP() {
  // Mostra l'IP in modo statico, un ottetto per riga
  clearMatrix();

  if (wifiConnected) {
    String ipStr = WiFi.localIP().toString();

    // Separa gli ottetti
    int dotPos[3];
    int dotCount = 0;
    for (int i = 0; i < ipStr.length() && dotCount < 3; i++) {
      if (ipStr.charAt(i) == '.') {
        dotPos[dotCount++] = i;
      }
    }

    if (dotCount == 3) {
      // Estrai i 4 ottetti
      String octet1 = ipStr.substring(0, dotPos[0]);
      String octet2 = ipStr.substring(dotPos[0] + 1, dotPos[1]);
      String octet3 = ipStr.substring(dotPos[1] + 1, dotPos[2]);
      String octet4 = ipStr.substring(dotPos[2] + 1);

      CRGB color = CRGB(255, 255, 255); // Bianco

      // Mostra ogni ottetto centrato su una riga (usa font 3x5)
      // Riga 1 (y=0-4): primo ottetto
      int x1 = (16 - octet1.length() * 4) / 2;
      for (unsigned int i = 0; i < octet1.length(); i++) {
        drawCharacter(octet1.charAt(i), x1 + i * 4, 0, color);
      }

      // Riga 2 (y=4-8): secondo ottetto
      int x2 = (16 - octet2.length() * 4) / 2;
      for (unsigned int i = 0; i < octet2.length(); i++) {
        drawCharacter(octet2.charAt(i), x2 + i * 4, 4, color);
      }

      // Riga 3 (y=8-12): terzo ottetto
      int x3 = (16 - octet3.length() * 4) / 2;
      for (unsigned int i = 0; i < octet3.length(); i++) {
        drawCharacter(octet3.charAt(i), x3 + i * 4, 8, color);
      }

      // Riga 4 (y=12-15): quarto ottetto
      int x4 = (16 - octet4.length() * 4) / 2;
      for (unsigned int i = 0; i < octet4.length(); i++) {
        drawCharacter(octet4.charAt(i), x4 + i * 4, 12, color);
      }
    }
  }

  FastLED.show();
}

void displayBootText() {
  // Mostra "CONSOLE QUADRA" scorrevole con effetto rainbow durante il boot
  String bootText = "CONSOLE QUADRA";
  int textLen = bootText.length();
  int totalWidth = textLen * 4; // 3 pixel carattere + 1 spazio
  int scrollPos = 16; // Inizia da destra

  // Melodia LOFTHER Davide Gatti - Survival Hacking "Console Quadra" 
  
  /*
  const int melodyNotes[] = {
    // DISCESA (18 note) OTTAVA PIU' BASSA
    523,  // C5
    466,  // A#4 (Bb4)
    392,  // G4
    330,  // E4
    294,  // D4
    262,  // C4
    233,  // A#3 (Bb3)
    196,  // G3
    175,  // F3
    165,  // E3
    147,  // D3
    131,  // C3
    117,  // A#2 (Bb2)
    98,   // G2
    87,   // F2
    82,   // E2
    73,   // D2
    65,   // C2 (punto più basso)
    
    // SALITA (15 note)
    73,   // D2
    82,   // E2
    98,   // G2
    110,  // A2
    131,  // C3
    147,  // D3
    165,  // E3
    196,  // G3
    233,  // A#3 (Bb3)
    262,  // C4
    294,  // D4
    330,  // E4
    392,  // G4
    440,  // A4
    523   // C5 (fine)
  };
  */

  const int melodyNotes[] = {
    // DISCESA (18 note) - UN'OTTAVA PIÙ ALTA
    1047, // C6 (era C5)
    932,  // A#5 (Bb5)
    784,  // G5
    659,  // E5
    587,  // D5
    523,  // C5
    466,  // A#4 (Bb4)
    392,  // G4
    349,  // F4
    330,  // E4
    294,  // D4
    262,  // C4
    233,  // A#3 (Bb3)
    196,  // G3
    175,  // F3
    165,  // E3
    147,  // D3
    131,  // C3 (punto più basso)
    
    // SALITA (15 note) - UN'OTTAVA PIÙ ALTA
    147,  // D3
    165,  // E3
    196,  // G3
    220,  // A3
    262,  // C4
    294,  // D4
    330,  // E4
    392,  // G4
    466,  // A#4 (Bb4)
    523,  // C5
    587,  // D5
    659,  // E5
    784,  // G5
    880,  // A5
    1047  // C6 (fine)
  };

  const int noteDurations[] = {
    120, 120, 120, 120, 120, 120, 120, 120, 120, 120,
    120, 120, 120, 120, 120, 120, 120, 120,
    120, 120, 120, 120, 120, 120, 120, 120, 120, 120,
    120, 120, 120, 120, 120
  };

  const int melodyLength = 33;


  int noteIndex = 0;
  int stepCounter = 0;
  int stepsPerNote = 2;

  // Scorri il testo da destra a sinistra (una sola volta)
  while (scrollPos > -totalWidth) {
    clearMatrix();

    // Disegna ogni carattere con colore rainbow
    for (int i = 0; i < textLen; i++) {
      int charX = scrollPos + i * 4;
      if (charX >= -3 && charX < 16) {
        // Calcola colore rainbow basato sulla posizione del carattere
        int hue = ((i * 20 + millis() / 30) % 256);
        CRGB rainbowColor = gamma32(ColorHSV_NeoPixel(hue * 256));
        drawCharacter(bootText.charAt(i), charX, 5, rainbowColor);
      }
    }

    // Suona la melodia sincronizzata con lo scorrimento
    if (stepCounter % stepsPerNote == 0 && noteIndex < melodyLength) {
      if (melodyNotes[noteIndex] > 0) {
        tone(BUZZER_PIN, melodyNotes[noteIndex], noteDurations[noteIndex]);
      } else {
        noTone(BUZZER_PIN); // Pausa
      }
      noteIndex++;
    }
    stepCounter++;

    FastLED.show();
    delay(60); // Velocità scorrimento leggermente più lenta
    scrollPos--;
  }

  noTone(BUZZER_PIN); // Assicura che il buzzer sia spento
  clearMatrix();
}

// Funzione helper per ottenere il colore del testo scorrevole
CRGB getScrollTextColor(int charIndex) {
  // IP scroll ha sempre colore bianco
  if (ipScrollActive) {
    return CRGB(255, 255, 255);
  }

  // Modalità rainbow: colore animato per ogni carattere
  if (scrollTextColor == 8) {
    uint8_t hue = (rainbowOffset + charIndex * 20) % 256;
    CRGB color = gamma32(ColorHSV_NeoPixel(hue * 256));
    return color;
  }

  // Altri colori fissi
  switch (scrollTextColor) {
    case 0: return CRGB(255, 0, 0);     // Rosso
    case 1: return CRGB(0, 255, 0);     // Verde
    case 2: return CRGB(0, 0, 255);     // Blu
    case 3: return CRGB(255, 255, 0);   // Giallo
    case 4: return CRGB(0, 255, 255);   // Ciano
    case 5: return CRGB(255, 0, 255);   // Magenta
    case 6: return CRGB(255, 255, 255); // Bianco
    case 7: return CRGB(255, 128, 0);   // Arancione
    default: return CRGB(0, 255, 0);    // Default verde
  }
}

// ============================================
// FONT 5x7 - Font medio più bello e dettagliato
// ============================================
void drawChar5x7(char ch, int x, int y, CRGB color) {
  switch (ch) {
    case 'A': case 'a':
      // _XXX_
      // X___X
      // X___X
      // XXXXX
      // X___X
      // X___X
      // X___X
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'B': case 'b':
      // XXXX_
      // X___X
      // X___X
      // XXXX_
      // X___X
      // X___X
      // XXXX_
      for (int i = 0; i < 4; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+6, color); }
      break;
    case 'C': case 'c':
      // _XXX_
      // X___X
      // X____
      // X____
      // X____
      // X___X
      // _XXX_
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color);
      setPixel(x, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case 'D': case 'd':
      // XXXX_
      // X___X
      // X___X
      // X___X
      // X___X
      // X___X
      // XXXX_
      for (int i = 0; i < 4; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+6, color); }
      break;
    case 'E': case 'e':
      // XXXXX
      // X____
      // X____
      // XXXX_
      // X____
      // X____
      // XXXXX
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color);
      setPixel(x, y+5, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+6, color); }
      break;
    case 'F': case 'f':
      // XXXXX
      // X____
      // X____
      // XXXX_
      // X____
      // X____
      // X____
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color);
      setPixel(x, y+5, color);
      setPixel(x, y+6, color);
      break;
    case 'G': case 'g':
      // _XXX_
      // X___X
      // X____
      // X__XX
      // X___X
      // X___X
      // _XXX_
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case 'H': case 'h':
      // X___X
      // X___X
      // X___X
      // XXXXX
      // X___X
      // X___X
      // X___X
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'I': case 'i':
      // XXXXX
      // __X__
      // __X__
      // __X__
      // __X__
      // __X__
      // XXXXX
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x+2, y+1, color);
      setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+6, color); }
      break;
    case 'J': case 'j':
      // __XXX
      // ____X
      // ____X
      // ____X
      // X___X
      // X___X
      // _XXX_
      setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case 'K': case 'k':
      // X___X
      // X__X_
      // X_X__
      // XX___
      // X_X__
      // X__X_
      // X___X
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+3, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      setPixel(x, y+5, color); setPixel(x+3, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'L': case 'l':
      // X____
      // X____
      // X____
      // X____
      // X____
      // X____
      // XXXXX
      setPixel(x, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color);
      setPixel(x, y+4, color);
      setPixel(x, y+5, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+6, color); }
      break;
    case 'M': case 'm':
      // X___X
      // XX_XX
      // X_X_X
      // X___X
      // X___X
      // X___X
      // X___X
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color); setPixel(x+3, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'N': case 'n':
      // X___X
      // XX__X
      // X_X_X
      // X__XX
      // X___X
      // X___X
      // X___X
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'O': case 'o':
      // _XXX_
      // X___X
      // X___X
      // X___X
      // X___X
      // X___X
      // _XXX_
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case 'P': case 'p':
      // XXXX_
      // X___X
      // X___X
      // XXXX_
      // X____
      // X____
      // X____
      for (int i = 0; i < 4; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color);
      setPixel(x, y+5, color);
      setPixel(x, y+6, color);
      break;
    case 'Q': case 'q':
      // _XXX_
      // X___X
      // X___X
      // X___X
      // X_X_X
      // X__X_
      // _XX_X
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+3, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'R': case 'r':
      // XXXX_
      // X___X
      // X___X
      // XXXX_
      // X_X__
      // X__X_
      // X___X
      for (int i = 0; i < 4; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      setPixel(x, y+5, color); setPixel(x+3, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'S': case 's':
      // _XXXX
      // X____
      // X____
      // _XXX_
      // ____X
      // ____X
      // XXXX_
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+6, color); }
      break;
    case 'T': case 't':
      // XXXXX
      // __X__
      // __X__
      // __X__
      // __X__
      // __X__
      // __X__
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x+2, y+1, color);
      setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      setPixel(x+2, y+6, color);
      break;
    case 'U': case 'u':
      // X___X
      // X___X
      // X___X
      // X___X
      // X___X
      // X___X
      // _XXX_
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case 'V': case 'v':
      // X___X
      // X___X
      // X___X
      // X___X
      // _X_X_
      // _X_X_
      // __X__
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+1, y+4, color); setPixel(x+3, y+4, color);
      setPixel(x+1, y+5, color); setPixel(x+3, y+5, color);
      setPixel(x+2, y+6, color);
      break;
    case 'W': case 'w':
      // X___X
      // X___X
      // X___X
      // X_X_X
      // X_X_X
      // XX_XX
      // X___X
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+1, y+5, color); setPixel(x+3, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'X': case 'x':
      // X___X
      // X___X
      // _X_X_
      // __X__
      // _X_X_
      // X___X
      // X___X
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x+1, y+2, color); setPixel(x+3, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+1, y+4, color); setPixel(x+3, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+4, y+6, color);
      break;
    case 'Y': case 'y':
      // X___X
      // X___X
      // _X_X_
      // __X__
      // __X__
      // __X__
      // __X__
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x+1, y+2, color); setPixel(x+3, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      setPixel(x+2, y+6, color);
      break;
    case 'Z': case 'z':
      // XXXXX
      // ____X
      // ___X_
      // __X__
      // _X___
      // X____
      // XXXXX
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x+4, y+1, color);
      setPixel(x+3, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+1, y+4, color);
      setPixel(x, y+5, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+6, color); }
      break;
    // Numeri 5x7
    case '0':
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+3, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+1, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case '1':
      setPixel(x+2, y, color);
      setPixel(x+1, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+6, color); }
      break;
    case '2':
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+3, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+1, y+5, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+6, color); }
      break;
    case '3':
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x+3, y+1, color);
      setPixel(x+2, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case '4':
      setPixel(x+3, y, color);
      setPixel(x+2, y+1, color); setPixel(x+3, y+1, color);
      setPixel(x+1, y+2, color); setPixel(x+3, y+2, color);
      setPixel(x, y+3, color); setPixel(x+3, y+3, color);
      for (int i = 0; i < 5; i++) { setPixel(x+i, y+4, color); }
      setPixel(x+3, y+5, color);
      setPixel(x+3, y+6, color);
      break;
    case '5':
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case '6':
      setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x+1, y+1, color);
      setPixel(x, y+2, color);
      for (int i = 0; i < 4; i++) { setPixel(x+i, y+3, color); }
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case '7':
      for (int i = 0; i < 5; i++) { setPixel(x+i, y, color); }
      setPixel(x+4, y+1, color);
      setPixel(x+3, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      setPixel(x+2, y+6, color);
      break;
    case '8':
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
    case '9':
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+3, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color);
      break;
    case ' ':
      break;
    case '.':
      setPixel(x+2, y+6, color);
      break;
    case ':':
      setPixel(x+2, y+2, color);
      setPixel(x+2, y+5, color);
      break;
    case '!':
      setPixel(x+2, y, color);
      setPixel(x+2, y+1, color);
      setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+6, color);
      break;
    case '?':
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x+3, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+6, color);
      break;
    default:
      // Carattere sconosciuto - box
      for (int dx = 0; dx < 5; dx++) {
        setPixel(x+dx, y, color);
        setPixel(x+dx, y+6, color);
      }
      for (int dy = 1; dy < 6; dy++) {
        setPixel(x, y+dy, color);
        setPixel(x+4, y+dy, color);
      }
      break;
  }
}

// Funzione helper per disegnare un pixel scalato (blocco scale x scale)
void setScaledPixel(int x, int y, CRGB color, int scale) {
  for (int sx = 0; sx < scale; sx++) {
    for (int sy = 0; sy < scale; sy++) {
      setPixel(x + sx, y + sy, color);
    }
  }
}

// Funzione per disegnare font 5x7 scalato 2x (diventa 10x14)
void drawChar5x7Scaled2x(char ch, int x, int y, CRGB color) {
  // Ogni pixel del font 5x7 diventa un blocco 2x2
  int scale = 2;

  switch (ch) {
    case 'A': case 'a':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'B': case 'b':
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case 'C': case 'c':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case 'D': case 'd':
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case 'E': case 'e':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case 'F': case 'f':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale);
      break;
    case 'G': case 'g':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case 'H': case 'h':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'I': case 'i':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+2*scale, y+1*scale, color, scale);
      setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x+2*scale, y+5*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case 'J': case 'j':
      setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case 'K': case 'k':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+3*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+1*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+3*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'L': case 'l':
      setScaledPixel(x, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case 'M': case 'm':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+1*scale, y+1*scale, color, scale); setScaledPixel(x+3*scale, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'N': case 'n':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+1*scale, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'O': case 'o':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case 'P': case 'p':
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale);
      break;
    case 'Q': case 'q':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+3*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'R': case 'r':
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+3*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'S': case 's':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x+1*scale, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale);
      setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x+4*scale, y+5*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case 'T': case 't':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+2*scale, y+1*scale, color, scale);
      setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x+2*scale, y+5*scale, color, scale);
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case 'U': case 'u':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case 'V': case 'v':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x+1*scale, y+4*scale, color, scale); setScaledPixel(x+3*scale, y+4*scale, color, scale);
      setScaledPixel(x+1*scale, y+5*scale, color, scale); setScaledPixel(x+3*scale, y+5*scale, color, scale);
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case 'W': case 'w':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+1*scale, y+5*scale, color, scale); setScaledPixel(x+3*scale, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'X': case 'x':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+1*scale, y+2*scale, color, scale); setScaledPixel(x+3*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+1*scale, y+4*scale, color, scale); setScaledPixel(x+3*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x, y+6*scale, color, scale); setScaledPixel(x+4*scale, y+6*scale, color, scale);
      break;
    case 'Y': case 'y':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+4*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+1*scale, y+2*scale, color, scale); setScaledPixel(x+3*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x+2*scale, y+5*scale, color, scale);
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case 'Z': case 'z':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+3*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+1*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    // Numeri
    case '0':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+3*scale, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+1*scale, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case '1':
      setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x+1*scale, y+1*scale, color, scale); setScaledPixel(x+2*scale, y+1*scale, color, scale);
      setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x+2*scale, y+5*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case '2':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x+3*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x+1*scale, y+5*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+6*scale, color, scale); }
      break;
    case '3':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+3*scale, y+1*scale, color, scale);
      setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x+1*scale, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale);
      setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case '4':
      setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x+2*scale, y+1*scale, color, scale); setScaledPixel(x+3*scale, y+1*scale, color, scale);
      setScaledPixel(x+1*scale, y+2*scale, color, scale); setScaledPixel(x+3*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale);
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      setScaledPixel(x+3*scale, y+5*scale, color, scale);
      setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case '5':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case '6':
      setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x+1*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      for (int i = 0; i < 4; i++) { setScaledPixel(x+i*scale, y+3*scale, color, scale); }
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case '7':
      for (int i = 0; i < 5; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+3*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      setScaledPixel(x+2*scale, y+5*scale, color, scale);
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case '8':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x+1*scale, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x, y+5*scale, color, scale); setScaledPixel(x+4*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale); setScaledPixel(x+3*scale, y+6*scale, color, scale);
      break;
    case '9':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+4*scale, y+2*scale, color, scale);
      setScaledPixel(x+1*scale, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); setScaledPixel(x+3*scale, y+3*scale, color, scale); setScaledPixel(x+4*scale, y+3*scale, color, scale);
      setScaledPixel(x+4*scale, y+4*scale, color, scale);
      setScaledPixel(x+3*scale, y+5*scale, color, scale);
      setScaledPixel(x+1*scale, y+6*scale, color, scale); setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case ' ':
      break;
    case '.':
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case ':':
      setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+5*scale, color, scale);
      break;
    case '!':
      setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x+2*scale, y+1*scale, color, scale);
      setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    case '?':
      setScaledPixel(x+1*scale, y, color, scale); setScaledPixel(x+2*scale, y, color, scale); setScaledPixel(x+3*scale, y, color, scale);
      setScaledPixel(x, y+1*scale, color, scale); setScaledPixel(x+4*scale, y+1*scale, color, scale);
      setScaledPixel(x+3*scale, y+2*scale, color, scale);
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+6*scale, color, scale);
      break;
    default:
      // Carattere sconosciuto - piccolo box
      for (int dx = 0; dx < 3; dx++) {
        setScaledPixel(x+dx*scale, y, color, scale);
        setScaledPixel(x+dx*scale, y+4*scale, color, scale);
      }
      for (int dy = 1; dy < 4; dy++) {
        setScaledPixel(x, y+dy*scale, color, scale);
        setScaledPixel(x+2*scale, y+dy*scale, color, scale);
      }
      break;
  }
}

// Funzione helper per disegnare caratteri scalati
void drawScaledCharacter(char ch, int x, int y, CRGB color, int scale) {
  // Se scale è 1, usa la funzione normale per efficienza
  if (scale == 1) {
    drawCharacter(ch, x, y, color);
    return;
  }

  // Per scale > 1, disegna ogni pixel come un blocco
  // Ogni pixel del carattere originale (3x5) diventa un blocco scale x scale
  switch (ch) {
    case 'A': case 'a':
      setScaledPixel(x+scale, y, color, scale); // Picco triangolare
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); } // Barra centrale
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'B': case 'b':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+scale, y, color, scale); // Solo 2 pixel in alto
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+scale, y+2*scale, color, scale); // Solo 2 pixel al centro
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+scale, y+4*scale, color, scale); // Solo 2 pixel in basso
      break;
    case 'C': case 'c':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'D': case 'd':
      for (int i = 0; i < 2; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 2; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'E': case 'e':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'F': case 'f':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale);
      break;
    case 'G': case 'g':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'H': case 'h':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'I': case 'i':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x+scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'K': case 'k':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'L': case 'l':
      setScaledPixel(x, y, color, scale);
      setScaledPixel(x, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'M': case 'm':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+scale, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'N': case 'n':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+scale, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+scale, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+scale, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'O': case 'o':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'P': case 'p':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale);
      break;
    case 'Q': case 'q':
      // ███
      // █ █
      // █ █
      // ███
      //  ██
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+scale, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+scale, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'R': case 'r':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+scale, y+2*scale, color, scale); // Solo 2 pixel (gamba)
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+scale, y+3*scale, color, scale); // Diagonale
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale); // Gamba separata
      break;
    case 'S': case 's':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale); // Curva superiore
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale); // Curva inferiore
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'T': case 't':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x+scale, y+3*scale, color, scale);
      setScaledPixel(x+scale, y+4*scale, color, scale);
      break;
    case 'U': case 'u':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'V': case 'v':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+scale, y+4*scale, color, scale);
      break;
    case 'W': case 'w':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case 'X': case 'x':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x, y+4*scale, color, scale); setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case 'Y': case 'y':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x+scale, y+3*scale, color, scale);
      setScaledPixel(x+scale, y+4*scale, color, scale);
      break;
    case 'Z': case 'z':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    // Numeri
    case '0':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x, y+2*scale, color, scale); setScaledPixel(x+2*scale, y+2*scale, color, scale);
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '1':
      setScaledPixel(x+scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x+scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '2':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '3':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '4':
      setScaledPixel(x, y, color, scale); setScaledPixel(x+2*scale, y, color, scale);
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      setScaledPixel(x+2*scale, y+4*scale, color, scale);
      break;
    case '5':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '6':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '7':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x+2*scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+2*scale, color, scale);
      setScaledPixel(x+scale, y+3*scale, color, scale);
      setScaledPixel(x+scale, y+4*scale, color, scale);
      break;
    case '8':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x, y+3*scale, color, scale); setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    case '9':
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y, color, scale); }
      setScaledPixel(x, y+scale, color, scale); setScaledPixel(x+2*scale, y+scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+2*scale, color, scale); }
      setScaledPixel(x+2*scale, y+3*scale, color, scale);
      for (int i = 0; i < 3; i++) { setScaledPixel(x+i*scale, y+4*scale, color, scale); }
      break;
    // Punteggiatura
    case '.':
      setScaledPixel(x+scale, y+4*scale, color, scale);
      break;
    case ':':
      setScaledPixel(x+scale, y+scale, color, scale);
      setScaledPixel(x+scale, y+3*scale, color, scale);
      break;
    case ' ':
      break;
    default:
      // Per caratteri non implementati, usa il carattere normale (fallback)
      drawCharacter(ch, x, y, color);
      break;
  }
}

void scrollTextOnMatrix() {
  unsigned long currentTime = millis();

  if (currentTime - lastScrollUpdate > scrollSpeed) {
    clearMatrixNoShow();

    int charWidth, charHeight, charY;

    // Dimensioni basate sulla scelta utente
    if (scrollTextSize == 0) {
      // Piccolo: 3x5 (font esistente)
      charWidth = 4; // 3 pixel + 1 spazio
      charHeight = 5;
      charY = 5; // Centra verticalmente
    } else if (scrollTextSize == 1) {
      // Medio: 5x7 (font nuovo)
      charWidth = 6; // 5 pixel + 1 spazio
      charHeight = 7;
      charY = 4; // Centra verticalmente (16-7)/2 = 4
    } else {
      // Grande: 10x14 (font 5x7 scalato 2x)
      charWidth = 12; // (5*2) pixel + 2 spazio
      charHeight = 14;
      charY = 1; // Centra verticalmente (16-14)/2 = 1
    }

    int textLength = scrollText.length();

    for (int i = 0; i < textLength; i++) {
      int charX = scrollPosition + i * charWidth;

      // Disegna solo se il carattere è visibile
      if (charX > -(charWidth) && charX < MATRIX_WIDTH) {
        CRGB textColor = getScrollTextColor(i);

        if (scrollTextSize == 0) {
          // Font piccolo 3x5
          drawCharacter(scrollText.charAt(i), charX, charY, textColor);
        } else if (scrollTextSize == 1) {
          // Font medio 5x7
          drawChar5x7(scrollText.charAt(i), charX, charY, textColor);
        } else {
          // Font grande 10x14 (scaling 2x reale del 5x7)
          drawChar5x7Scaled2x(scrollText.charAt(i), charX, charY, textColor);
        }
      }
    }

    FastLED.show();

    scrollPosition--;

    // Aggiorna offset rainbow per animazione
    if (scrollTextColor == 8) {
      rainbowOffset = (rainbowOffset + 2) % 256;
    }

    // Se siamo in modalità IP scroll, ferma dopo un giro completo
    if (ipScrollActive && scrollPosition < -textLength * charWidth) {
      // Non resettare la posizione, il loop principale passerà all'orologio
    } else if (!ipScrollActive && scrollPosition < -textLength * charWidth) {
      // Per altri testi, ricomincia da capo
      scrollPosition = MATRIX_WIDTH;
    }

    lastScrollUpdate = currentTime;
  }
}

void drawClockOnMatrix() {
  static int lastMinute = -1;
  static int lastSecond = -1;
  static int lastHour = -1;
  static int lastColorMode = -1;
  static int lastDisplayType = -1;

  // Resetta le variabili statiche quando si entra per la prima volta nello stato CLOCK
  // o quando c'è un force redraw (dopo alternanza)
  if (previousState != STATE_GAME_CLOCK || forceRedraw) {
    lastMinute = -1;
    lastSecond = -1;
    lastHour = -1;
    lastColorMode = -1;
    lastDisplayType = -1;
  }

  int h = myTZ.hour();
  int m = myTZ.minute();
  int s = myTZ.second();

  CRGB digitColor = getClockColor();

  // *** LAMPEGGIO ROSSO QUANDO LA SVEGLIA SUONA ***
  if (alarmRinging) {
    // Segnala subito che il display è attivo (per avviare la melodia)
    if (!alarmDisplayStarted) {
      alarmDisplayStarted = true;
    }

    // Lampeggio fisso 500ms ON / 500ms OFF sincronizzato con inizio sveglia
    // Usa alarmRingingStartTime come riferimento per garantire che tutte le suonerie
    // partano in fase 0 (display ON) insieme alla melodia
    unsigned long elapsed = millis() - alarmRingingStartTime;
    unsigned long blinkPhase = (elapsed / 500) % 2;

    if (blinkPhase == 0) {
      // Fase ON: mostra orologio rosso
      digitColor = CRGB(255, 0, 0);
    } else {
      // Fase OFF: spegni il display
      clearMatrixNoShow();
      FastLED.show();
      return;
    }
  }

  // Ridisegna se cambia ora, minuto, colore, tipo di display o force redraw
  bool needsRedraw = (h != lastHour || m != lastMinute || clockColorMode != lastColorMode || clockDisplayType != lastDisplayType || forceRedraw);

  // Per tipo binario, analogico e scorrevole, ridisegna continuamente
  if (clockDisplayType == 3 || clockDisplayType == 4) {
    needsRedraw = needsRedraw || (s != lastSecond);
  }

  // Per tipo scorrevole, forza ridisegno continuo per animazione scroll
  if (clockDisplayType == 6) {
    needsRedraw = true; // Sempre ridisegna per animazione fluida
  }

  // Per modalità rainbow, forza ridisegno continuo per animazione fluida
  if (clockColorMode == 8) {
    needsRedraw = true; // Sempre ridisegna per animazione rainbow fluida
  }

  // Quando la sveglia suona, forza ridisegno continuo per il lampeggio
  if (alarmRinging) {
    needsRedraw = true;
  }

  // OTTIMIZZAZIONE ANTI-FLICKERING: FastLED.show() chiamato UNA SOLA VOLTA alla fine
  bool needsShow = false; // Flag per tracciare se serve aggiornare il display

  if (needsRedraw) {
    // NON fare clearMatrix per l'orologio analogico (gestisce internamente l'aggiornamento)
    if (clockDisplayType != 4) {
      clearMatrixNoShow(); // Usa versione senza FastLED.show()
    }

    switch (clockDisplayType) {
      case 0: // Classico (ore/minuti separati)
        {
          int h1 = h / 10;
          int h2 = h % 10;
          int m1 = m / 10;
          int m2 = m % 10;

          // ORE in alto (distanza 2 LED)
          drawBigDigit(h1, 2, 1, digitColor);
          drawBigDigit(h2, 9, 1, digitColor);

          // MINUTI in basso (distanza 2 LED)
          drawBigDigit(m1, 2, 9, digitColor);
          drawBigDigit(m2, 9, 9, digitColor);

          // LED dei secondi
          if (s % 2 == 0) {
            setPixel(7, 8, getSecondsLedColor());
          }
        }
        break;

      case 1: // Compatto HH:MM
        drawClockCompact(h, m, s, digitColor);
        break;

      case 2: // Grande
        drawClockLarge(h, m, s, digitColor);
        break;

      case 3: // Binario
        drawClockBinary(h, m, s, digitColor);
        break;

      case 4: // Analogico
        drawClockAnalog(h, m, s, digitColor);
        break;

      case 5: // Verticale (numeri impilati)
        drawClockVertical(h, m, s, digitColor);
        break;

      case 6: // Scorrevole (font 5x7)
        drawClockScrolling(h, m, s, digitColor);
        break;

      case 7: // Compatto + Giorno
        drawClockCompactDay(h, m, s, digitColor);
        break;

      default: // Fallback al classico
        {
          int h1 = h / 10;
          int h2 = h % 10;
          int m1 = m / 10;
          int m2 = m % 10;

          drawBigDigit(h1, 2, 1, digitColor);
          drawBigDigit(h2, 9, 1, digitColor);
          drawBigDigit(m1, 2, 9, digitColor);
          drawBigDigit(m2, 9, 9, digitColor);

          if (s % 2 == 0) {
            setPixel(7, 8, getSecondsLedColor());
          }
        }
        break;
    }

    lastHour = h;
    lastMinute = m;
    lastColorMode = clockColorMode;
    lastDisplayType = clockDisplayType;
    needsShow = true; // Segna che serve aggiornare
  }

  // Gestione lampeggio secondi per tipi classico, compatto, grande, verticale, scorrevole e compatto+giorno
  if (s != lastSecond && (clockDisplayType == 0 || clockDisplayType == 1 || clockDisplayType == 2 || clockDisplayType == 5 || clockDisplayType == 6 || clockDisplayType == 7)) {
    if (clockDisplayType == 0) {
      // Classico: 1 LED al centro
      if (s % 2 == 0) {
        setPixel(7, 8, getSecondsLedColor());
      } else {
        setPixel(7, 8, CRGB(0, 0, 0));
      }
    } else if (clockDisplayType == 1) {
      // Compatto: 2 LED verticali al centro
      if (s % 2 == 0) {
        setPixel(7, 6, getSecondsLedColor());
        setPixel(7, 8, getSecondsLedColor());
      } else {
        setPixel(7, 6, CRGB(0, 0, 0));
        setPixel(7, 8, CRGB(0, 0, 0));
      }
    } else if (clockDisplayType == 2) {
      // Grande: 2 LED orizzontali al centro
      if (s % 2 == 0) {
        setPixel(7, 8, getSecondsLedColor());
        setPixel(8, 8, getSecondsLedColor());
      } else {
        setPixel(7, 8, CRGB(0, 0, 0));
        setPixel(8, 8, CRGB(0, 0, 0));
      }
    } else if (clockDisplayType == 5) {
      // Verticale: 2 LED orizzontali (senza centrale)
      if (s % 2 == 0) {
        setPixel(6, 7, getSecondsLedColor());
        setPixel(8, 7, getSecondsLedColor());
      } else {
        setPixel(6, 7, CRGB(0, 0, 0));
        setPixel(8, 7, CRGB(0, 0, 0));
      }
    } else if (clockDisplayType == 7) {
      // Compatto + Giorno: 2 LED verticali in alto
      if (s % 2 == 0) {
        setPixel(7, 2, getSecondsLedColor());
        setPixel(7, 4, getSecondsLedColor());
      } else {
        setPixel(7, 2, CRGB(0, 0, 0));
        setPixel(7, 4, CRGB(0, 0, 0));
      }
    }
    needsShow = true; // Segna che serve aggiornare
    lastSecond = s;
  }

  // *** CHIAMATA UNICA A FastLED.show() - ELIMINA FLICKERING ***
  if (needsShow) {
    FastLED.show();
  }
}

void drawDateOnMatrix() {
  static int lastDay = -1;
  static int lastMonth = -1;
  static int lastYear = -1;
  static int lastSize = -1;
  static int lastColorMode = -1;

  // Resetta le variabili statiche quando si entra per la prima volta o dopo force redraw
  if (forceRedraw) {
    lastDay = -1;
    lastMonth = -1;
    lastYear = -1;
    lastSize = -1;
    lastColorMode = -1;
  }

  int d = myTZ.day();
  int mo = myTZ.month();
  int y = myTZ.year();

  CRGB digitColor = getDateColor();

  // Ridisegna solo se cambiano giorno, mese, anno, dimensione, colore o force redraw
  bool needsRedraw = (d != lastDay || mo != lastMonth || y != lastYear || dateDisplaySize != lastSize || dateColorMode != lastColorMode || forceRedraw);

  // Per modalità rainbow, forza ridisegno continuo per animazione fluida
  if (dateColorMode == 8) {
    needsRedraw = true; // Sempre ridisegna per animazione rainbow fluida
  }

  if (needsRedraw) {
    clearMatrix();

    int d1 = d / 10;
    int d2 = d % 10;
    int mo1 = mo / 10;
    int mo2 = mo % 10;

    if (dateDisplaySize == 0) {
      // Piccolo: DD a sinistra, MM a destra con font 3x5, 2 LED bianchi orizzontali centrati
      // Giorno a sinistra (font 3x5) - alzato a Y=1
      drawSmallDigit(d1, 0, 1, digitColor);
      drawSmallDigit(d2, 4, 1, digitColor);

      // 2 LED bianchi orizzontali centrati tra giorno e mese (Y=3)
      setPixel(7, 3, CRGB(255, 255, 255));
      setPixel(8, 3, CRGB(255, 255, 255));

      // Mese a destra (font 3x5) - alzato a Y=1
      drawSmallDigit(mo1, 9, 1, digitColor);
      drawSmallDigit(mo2, 13, 1, digitColor);

      // Anno centrato in basso (4 cifre con font 3x5) - Y=10
      int y1 = (y / 1000) % 10;
      int y2 = (y / 100) % 10;
      int y3 = (y / 10) % 10;
      int y4 = y % 10;

      // Centrato: 4 cifre * 3 pixel + 3 spazi = 15 pixel, inizio a x=0
      drawSmallDigit(y1, 0, 10, digitColor);
      drawSmallDigit(y2, 4, 10, digitColor);
      drawSmallDigit(y3, 8, 10, digitColor);
      drawSmallDigit(y4, 12, 10, digitColor);
    } else {
      // Grande: DD in alto, MM in basso (come l'orologio grande)
      drawBigDigit(d1, 3, 1, digitColor);
      drawBigDigit(d2, 9, 1, digitColor);

      // Linea divisoria orizzontale bianca tra giorno e mese
      for (int x = 2; x < 14; x++) {
        setPixel(x, 8, CRGB(255, 255, 255));
      }

      drawBigDigit(mo1, 3, 9, digitColor);
      drawBigDigit(mo2, 9, 9, digitColor);
    }

    lastDay = d;
    lastMonth = mo;
    lastYear = y;
    lastSize = dateDisplaySize;
    lastColorMode = dateColorMode;
    FastLED.show();
  }
}

CRGB getClockColor() {
  if (clockColorMode == 8) {
    // Rainbow animato fluido: usa millis() invece dei secondi per animazione continua
    unsigned long currentMillis = millis();
    int hue = (currentMillis / 20) % 256; // Ciclo completo ogni ~5 secondi
    return gamma32(ColorHSV_NeoPixel(hue * 256));
  }

  switch (clockColorMode) {
    case 0: return CRGB(255, 0, 0);
    case 1: return CRGB(0, 255, 0);
    case 2: return CRGB(0, 0, 255);
    case 3: return CRGB(255, 255, 0);
    case 4: return CRGB(0, 255, 255);
    case 5: return CRGB(255, 0, 255);
    case 6: return CRGB(255, 255, 255);
    case 7: return CRGB(255, 128, 0);
    default: return CRGB(0, 255, 0);
  }
}

CRGB getSecondsLedColor() {
  switch (secondsLedColorMode) {
    case 0: return CRGB(255, 0, 0);
    case 1: return CRGB(0, 255, 0);
    case 2: return CRGB(0, 0, 255);
    case 3: return CRGB(255, 255, 0);
    case 4: return CRGB(0, 255, 255);
    case 5: return CRGB(255, 0, 255);
    case 6: return CRGB(255, 255, 255);
    case 7: return CRGB(255, 128, 0);
    default: return CRGB(255, 0, 0);
  }
}

CRGB getDateColor() {
  if (dateColorMode == 8) {
    // Rainbow animato fluido: usa millis() invece dei secondi per animazione continua
    unsigned long currentMillis = millis();
    int hue = (currentMillis / 20) % 256; // Ciclo completo ogni ~5 secondi
    return gamma32(ColorHSV_NeoPixel(hue * 256));
  }

  switch (dateColorMode) {
    case 0: return CRGB(255, 0, 0);
    case 1: return CRGB(0, 255, 0);
    case 2: return CRGB(0, 0, 255);
    case 3: return CRGB(255, 255, 0);
    case 4: return CRGB(0, 255, 255);
    case 5: return CRGB(255, 0, 255);
    case 6: return CRGB(255, 255, 255);
    case 7: return CRGB(255, 128, 0);
    default: return CRGB(0, 255, 0);
  }
}

void drawLocalSensorOnMatrix() {
  static float lastTemp = -999.0;
  static float lastHumidity = -999.0;

  // Resetta le variabili statiche quando si entra per la prima volta o dopo force redraw
  if (forceRedraw) {
    lastTemp = -999.0;
    lastHumidity = -999.0;
  }

  // Verifica se il sensore è disponibile
  if (!sensorAvailable) {
    clearMatrix();
    // Mostra messaggio "NO SENSOR" con font 3x5
    // N O   S E N S O R (centrato)
    drawCharacter('N', 1, 6, CRGB(255, 0, 0));
    drawCharacter('O', 5, 6, CRGB(255, 0, 0));
    FastLED.show();
    return;
  }

  // COLORI FISSI: Bianco per temperatura, Ciano per umidità
  CRGB tempColor = CRGB(255, 255, 255);  // BIANCO per temperatura
  CRGB humidityColor = CRGB(0, 255, 255); // CIANO per umidità
  CRGB whiteColor = CRGB(255, 255, 255);  // BIANCO per simboli

  // Tronca temperatura e umidità (es: 18.8 → 18, non arrotonda)
  int tempInt = (int)currentTemperature;  // Tronca i decimali (18.8 → 18)
  int humInt = (int)currentHumidity;      // Tronca i decimali

  // Ridisegna solo se cambiano temperatura, umidità o force redraw
  bool needsRedraw = (abs(currentTemperature - lastTemp) >= 0.5 ||
                      abs(currentHumidity - lastHumidity) >= 0.5 ||
                      forceRedraw);

  if (needsRedraw) {
    clearMatrix();

    // *** LAYOUT: Temperatura in alto, Umidità in basso con icone ***

    // === TEMPERATURA IN ALTO ===
    // Icona termometro a sinistra (2x5 pixel) - ABBASSATA DI 1 LED (da Y=0 a Y=1)
    // Termometro stilizzato
    setPixel(0, 1, CRGB(255, 0, 0));   // Bulbo rosso
    setPixel(0, 2, CRGB(200, 50, 50)); // Liquido rosso sfumato
    setPixel(0, 3, CRGB(150, 100, 100)); // Liquido rosso sfumato
    setPixel(0, 4, CRGB(150, 150, 150)); // Tubo grigio
    setPixel(0, 5, CRGB(150, 150, 150)); // Tubo grigio
    setPixel(1, 1, CRGB(255, 0, 0));   // Bulbo rosso

    // Temperatura (font 3x5) - 2 o 3 cifre + "°C" - ABBASSATA DI 1 LED (da Y=0 a Y=1)
    // SPOSTATA 1 LED A DESTRA, ° = 1 LED, C = ROSSA
    int t1 = abs(tempInt) / 10;
    int t2 = abs(tempInt) % 10;
    bool negative = (tempInt < 0);
    CRGB redColor = CRGB(255, 0, 0); // ROSSO per la C

    if (negative || tempInt >= 10) {
      // 3 caratteri: segno/prima cifra, seconda cifra, simbolo gradi
      if (negative) {
        // Segno meno (-) BIANCO - spostato +1 a destra, abbassato +1
        setPixel(4, 3, tempColor);
        setPixel(5, 3, tempColor);
        setPixel(6, 3, tempColor);
      } else {
        // Prima cifra BIANCA - spostata +1 a destra, abbassata +1
        drawSmallDigit(t1, 4, 1, tempColor);
      }
      drawSmallDigit(t2, 8, 1, tempColor); // Seconda cifra BIANCA - spostata +1 a destra, abbassata +1
      // Simbolo gradi (°) - 1 SOLO LED BIANCO - abbassato +1
      setPixel(12, 1, whiteColor);
      // "C" per Celsius ROSSA - abbassata +1
      drawCharacter('C', 13, 1, redColor);
    } else {
      // 2 caratteri: cifra + simbolo gradi (per temperature 0-9°C)
      drawSmallDigit(t2, 5, 1, tempColor); // Cifra BIANCA - spostata +1 a destra, abbassata +1
      // Simbolo gradi (°) - 1 SOLO LED BIANCO - abbassato +1
      setPixel(9, 1, whiteColor);
      // "C" ROSSA - abbassata +1
      drawCharacter('C', 10, 1, redColor);
    }

    // === UMIDITÀ IN BASSO ===
    // Icona goccia d'acqua a sinistra (2x4 pixel) - ABBASSATA DI 1 LED
    // Goccia stilizzata BLU
    setPixel(0, 10, CRGB(0, 150, 255));  // Punta goccia
    setPixel(0, 11, CRGB(0, 180, 255)); // Centro goccia
    setPixel(1, 11, CRGB(0, 180, 255)); // Centro goccia
    setPixel(0, 12, CRGB(0, 180, 255)); // Centro goccia
    setPixel(1, 12, CRGB(0, 180, 255)); // Centro goccia
    setPixel(0, 13, CRGB(0, 150, 255)); // Base goccia
    setPixel(1, 13, CRGB(0, 150, 255)); // Base goccia

    // Umidità (font 3x5) - 2 o 3 cifre CIANO + "%" BIANCO SPOSTATO A DESTRA DI 1 LED
    int h1 = humInt / 10;
    int h2 = humInt % 10;

    if (humInt >= 100) {
      // 3 cifre (100%) CIANO - cifre spostate a sinistra per fare spazio al %
      drawSmallDigit(1, 2, 10, humidityColor);
      drawSmallDigit(0, 6, 10, humidityColor);
      drawSmallDigit(0, 10, 10, humidityColor);
      // Simbolo % BIANCO spostato a destra (era 15, ma esce, quindi rimane 15)
      drawCharacter('%', 14, 10, whiteColor);
    } else if (humInt >= 10) {
      // 2 cifre CIANO
      drawSmallDigit(h1, 4, 10, humidityColor);
      drawSmallDigit(h2, 8, 10, humidityColor);
      // Simbolo % BIANCO spostato a destra di 1 LED
      drawCharacter('%', 13, 10, whiteColor);
    } else {
      // 1 cifra CIANO
      drawSmallDigit(h2, 5, 10, humidityColor);
      // Simbolo % BIANCO spostato a destra di 1 LED
      drawCharacter('%', 10, 10, whiteColor);
    }

    lastTemp = currentTemperature;
    lastHumidity = currentHumidity;
    FastLED.show();
  }
}

void drawBigDigit(int digit, int x, int y, CRGB color) {
  // Font digitale 7-segmenti (stile display LED/calcolatrice) 5x7
  switch (digit) {
    case 0:
      // █████
      // █   █
      // █   █
      // █   █
      // █   █
      // █   █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;

    case 1:
      //     █
      //     █
      //     █
      //     █
      //     █
      //     █
      //     █
      setPixel(x+4, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+4, y+6, color);
      break;

    case 2:
      // █████
      //     █
      //     █
      // █████
      // █
      // █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color);
      setPixel(x, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;

    case 3:
      // █████
      //     █
      //     █
      // █████
      //     █
      //     █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;

    case 4:
      // █   █
      // █   █
      // █   █
      // █████
      //     █
      //     █
      //     █
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+4, y+6, color);
      break;

    case 5:
      // █████
      // █
      // █
      // █████
      //     █
      //     █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;

    case 6:
      // █████
      // █
      // █
      // █████
      // █   █
      // █   █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;

    case 7:
      // █████
      //     █
      //     █
      //     █
      //     █
      //     █
      //     █
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+4, y+6, color);
      break;

    case 8:
      // █████
      // █   █
      // █   █
      // █████
      // █   █
      // █   █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;

    case 9:
      // █████
      // █   █
      // █   █
      // █████
      //     █
      //     █
      // █████
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x, y+6, color); setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color); setPixel(x+4, y+6, color);
      break;
  }
}

void drawBigDigitOldStyle(int digit, int x, int y, CRGB color) {
  // Font classico monospazio digitale 5x7 (vecchio stile)
  switch (digit) {
    case 0:
      //  ███
      // █   █
      // █   █
      // █   █
      // █   █
      // █   █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 1:
      //   █
      //  ██
      //   █
      //   █
      //   █
      //   █
      //  ███
      setPixel(x+2, y, color);
      setPixel(x+1, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 2:
      //  ███
      //     █
      //     █
      //  ███
      // █
      // █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x, y+4, color);
      setPixel(x, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 3:
      //  ███
      //     █
      //     █
      //  ███
      //     █
      //     █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 4:
      // █   █
      // █   █
      // █   █
      //  ████
      //     █
      //     █
      //     █
      setPixel(x, y, color); setPixel(x+4, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+4, y+6, color);
      break;

    case 5:
      //  ███
      // █
      // █
      //  ███
      //     █
      //     █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 6:
      //  ███
      // █
      // █
      // ████
      // █   █
      // █   █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 7:
      // █████
      //     █
      //     █
      //    █
      //   █
      //   █
      //   █
      setPixel(x, y, color); setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color); setPixel(x+4, y, color);
      setPixel(x+4, y+1, color);
      setPixel(x+4, y+2, color);
      setPixel(x+3, y+3, color);
      setPixel(x+2, y+4, color);
      setPixel(x+2, y+5, color);
      setPixel(x+2, y+6, color);
      break;

    case 8:
      //  ███
      // █   █
      // █   █
      //  ███
      // █   █
      // █   █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color);
      setPixel(x, y+4, color); setPixel(x+4, y+4, color);
      setPixel(x, y+5, color); setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;

    case 9:
      //  ███
      // █   █
      // █   █
      //  ████
      //     █
      //     █
      //  ███
      setPixel(x+1, y, color); setPixel(x+2, y, color); setPixel(x+3, y, color);
      setPixel(x, y+1, color); setPixel(x+4, y+1, color);
      setPixel(x, y+2, color); setPixel(x+4, y+2, color);
      setPixel(x+1, y+3, color); setPixel(x+2, y+3, color); setPixel(x+3, y+3, color); setPixel(x+4, y+3, color);
      setPixel(x+4, y+4, color);
      setPixel(x+4, y+5, color);
      setPixel(x+1, y+6, color); setPixel(x+2, y+6, color); setPixel(x+3, y+6, color);
      break;
  }
}

// Funzioni per diversi tipi di display orologio
void drawClockCompact(int h, int m, int s, CRGB digitColor) {
  // Display compatto: HH:MM su una riga con cifre piccole (3x5)
  // HH a sinistra, MM a destra, : : lampeggiante al centro
  // Cifre separate da 1 LED di spazio

  int h1 = h / 10;
  int h2 = h % 10;
  int m1 = m / 10;
  int m2 = m % 10;

  int y = 5;  // Centrato verticalmente

  // Layout con 1 LED di separazione tra le cifre:
  // h1: x=0 (0,1,2) | gap x=3 | h2: x=4 (4,5,6) | dots: x=7 | gap x=8 | m1: x=9 (9,10,11) | gap x=12 | m2: x=13 (13,14,15)

  // Disegna HH a sinistra con 1 LED di separazione
  drawCharacter('0' + h1, 0, y, digitColor);   // x=0,1,2
  drawCharacter('0' + h2, 4, y, digitColor);   // x=4,5,6 (gap a x=3)

  // Due punti lampeggianti al centro (x=7)
  if (s % 2 == 0) {
    setPixel(7, 6, getSecondsLedColor());
    setPixel(7, 8, getSecondsLedColor());
  }

  // Disegna MM a destra con 1 LED di separazione
  drawCharacter('0' + m1, 9, y, digitColor);   // x=9,10,11 (gap a x=8)
  drawCharacter('0' + m2, 13, y, digitColor);  // x=13,14,15 (gap a x=12)
}

void drawClockCompactDay(int h, int m, int s, CRGB digitColor) {
  // Display compatto + giorno: HH:MM in alto, giorno settimana in basso
  // Entrambi con font 3x5

  int h1 = h / 10;
  int h2 = h % 10;
  int m1 = m / 10;
  int m2 = m % 10;

  // === ORARIO IN ALTO (y=1) ===
  // Layout con 1 LED di separazione tra le cifre (come compatto)
  drawCharacter('0' + h1, 0, 1, digitColor);   // x=0,1,2
  drawCharacter('0' + h2, 4, 1, digitColor);   // x=4,5,6

  // Due punti lampeggianti
  if (s % 2 == 0) {
    setPixel(7, 2, getSecondsLedColor());
    setPixel(7, 4, getSecondsLedColor());
  }

  drawCharacter('0' + m1, 9, 1, digitColor);   // x=9,10,11
  drawCharacter('0' + m2, 13, 1, digitColor);  // x=13,14,15

  // === GIORNO DELLA SETTIMANA IN BASSO (y=10) ===
  // Ottieni il giorno della settimana (1=Domenica in ezTime, ma usiamo 0=Lunedì)
  int dayOfWeek = myTZ.weekday(); // 1=Dom, 2=Lun, 3=Mar, 4=Mer, 5=Gio, 6=Ven, 7=Sab

  // Converti in indice 0-6 dove 0=LUN, 1=MAR, ..., 6=DOM
  int dayIndex = (dayOfWeek == 1) ? 6 : (dayOfWeek - 2);

  // Abbreviazioni giorni italiani
  const char* giorni[] = {"LUN", "MAR", "MER", "GIO", "VEN", "SAB", "DOM"};
  const char* giorno = giorni[dayIndex];

  // Colori fissi per ogni giorno della settimana
  // LUN=giallo, MAR=verde, MER=blu, GIO=bianco, VEN=arancione, SAB=ciano, DOM=rosso
  const CRGB dayColors[] = {
    CRGB(255, 255, 0),   // LUN - Giallo
    CRGB(0, 255, 0),     // MAR - Verde
    CRGB(0, 0, 255),     // MER - Blu
    CRGB(255, 255, 255), // GIO - Bianco
    CRGB(255, 128, 0),   // VEN - Arancione
    CRGB(0, 255, 255),   // SAB - Ciano
    CRGB(255, 0, 0)      // DOM - Rosso
  };
  CRGB dayColor = dayColors[dayIndex];

  // Centra il giorno (3 caratteri * 3 pixel + 2 gap = 11 pixel)
  // Per centrare: (16 - 11) / 2 = 2.5 -> x=2
  int dayY = 10;
  drawCharacter(giorno[0], 2, dayY, dayColor);   // Prima lettera x=2,3,4
  drawCharacter(giorno[1], 6, dayY, dayColor);   // Seconda lettera x=6,7,8
  drawCharacter(giorno[2], 10, dayY, dayColor);  // Terza lettera x=10,11,12
}

void drawClockBinary(int h, int m, int s, CRGB digitColor) {
  // Display binario per matrice 16x16 con ORE, MINUTI e SECONDI
  // ORE (5 bit: 16,8,4,2,1) in alto - ROSSO
  // MINUTI (6 bit: 32,16,8,4,2,1) al centro - VERDE
  // SECONDI (6 bit: 32,16,8,4,2,1) in basso - CIANO

  clearMatrixNoShow();

  CRGB hourColor = CRGB(255, 0, 0);     // Rosso
  CRGB minColor = CRGB(0, 255, 0);      // Verde
  CRGB secColor = CRGB(0, 200, 255);    // Ciano

  // === ORE (righe 0-4) ===
  // Etichetta "H" piccola
  setPixel(0, 0, hourColor);
  setPixel(0, 1, hourColor);
  setPixel(0, 2, hourColor);
  setPixel(1, 1, hourColor);
  setPixel(2, 0, hourColor);
  setPixel(2, 1, hourColor);
  setPixel(2, 2, hourColor);

  // ORE in binario 5 bit - barre 4x2 con SFUMATURA
  for (int bit = 0; bit < 5; bit++) {
    if (h & (1 << (4 - bit))) {
      int xPos = 4 + bit * 2;
      
      // Colore sfumato (50% intensità)
      CRGB fadedColor = CRGB(hourColor.r / 3, hourColor.g / 3, hourColor.b / 3);
      
      for (int y = 0; y < 4; y++) {
        setPixel(xPos, y, hourColor);      // Sinistra pieno
        setPixel(xPos + 1, y, fadedColor); // Destra sfumato
      }
    }
  }

  // Separatore 1 (riga 5)
  for (int x = 0; x < 16; x++) {
    setPixel(x, 5, CRGB(50, 50, 50));
  }

  // === MINUTI (righe 6-10) ===
  // Etichetta "M" piccola
  setPixel(0, 6, minColor);
  setPixel(0, 7, minColor);
  setPixel(0, 8, minColor);
  setPixel(0, 9, minColor);
  setPixel(1, 7, minColor);
  setPixel(2, 6, minColor);
  setPixel(2, 7, minColor);
  setPixel(2, 8, minColor);
  setPixel(2, 9, minColor);

  // MINUTI in binario 6 bit - barre 4x2 con SFUMATURA
  for (int bit = 0; bit < 6; bit++) {
    if (m & (1 << (5 - bit))) {
      int xPos = 4 + bit * 2;
      
      CRGB fadedColor = CRGB(minColor.r / 3, minColor.g / 3, minColor.b / 3);
      
      for (int y = 6; y < 10; y++) {
        setPixel(xPos, y, minColor);
        setPixel(xPos + 1, y, fadedColor);
      }
    }
  }


  // Separatore 2 (riga 11)
  for (int x = 0; x < 16; x++) {
    setPixel(x, 11, CRGB(50, 50, 50));
  }

  // === SECONDI (righe 12-15) ===
  // Etichetta "S" piccola
  setPixel(0, 12, secColor);
  setPixel(1, 12, secColor);
  setPixel(2, 12, secColor);
  setPixel(0, 13, secColor);
  setPixel(0, 14, secColor);
  setPixel(1, 14, secColor);
  setPixel(2, 14, secColor);
  setPixel(2, 15, secColor);
  setPixel(1, 15, secColor);
  setPixel(0, 15, secColor);

  // SECONDI in binario 6 bit - barre 4x2 con SFUMATURA
  for (int bit = 0; bit < 6; bit++) {
    if (s & (1 << (5 - bit))) {
      int xPos = 4 + bit * 2;
      
      CRGB fadedColor = CRGB(secColor.r / 3, secColor.g / 3, secColor.b / 3);
      
      for (int y = 12; y < 16; y++) {
        setPixel(xPos, y, secColor);
        setPixel(xPos + 1, y, fadedColor);
      }
    }
  }
}  
void drawClockAnalog(int h, int m, int s, CRGB digitColor) {
  // Orologio analogico con cerchio rotondo perfetto e lancette
  // Ridisegna tutto ogni secondo ma in modo ottimizzato per ridurre sfarfallio

  int centerX = 7;
  int centerY = 7;
  float radius = 6.5;

  // Cancella SOLO l'interno del cerchio (non tutto lo schermo)
  // Questo riduce lo sfarfallio rispetto a clearMatrix() completo
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 16; y++) {
      float dx = x - centerX;
      float dy = y - centerY;
      float distance = sqrt(dx * dx + dy * dy);

      // Cancella solo i pixel DENTRO il cerchio
      if (distance < radius - 0.5) {
        setPixel(x, y, CRGB(0, 0, 0));
      }
      // Disegna il bordo del cerchio
      else if (distance >= radius - 0.5 && distance <= radius + 0.5) {
        setPixel(x, y, CRGB(150, 150, 150));
      }
      // Lascia nero tutto il resto (fuori dal cerchio)
      else {
        setPixel(x, y, CRGB(0, 0, 0));
      }
    }
  }

  // Marcatori ore principali (12, 3, 6, 9) più luminosi
  // 12 ore (top) - Nord
  setPixel(centerX, centerY - 6, CRGB(255, 255, 255));
  setPixel(centerX, centerY - 5, CRGB(200, 200, 200));

  // 3 ore (right) - Est
  setPixel(centerX + 6, centerY, CRGB(255, 255, 255));
  setPixel(centerX + 5, centerY, CRGB(200, 200, 200));

  // 6 ore (bottom) - Sud
  setPixel(centerX, centerY + 6, CRGB(255, 255, 255));
  setPixel(centerX, centerY + 5, CRGB(200, 200, 200));

  // 9 ore (left) - Ovest
  setPixel(centerX - 6, centerY, CRGB(255, 255, 255));
  setPixel(centerX - 5, centerY, CRGB(200, 200, 200));

  // Disegna lancette (ordine: minuti, ore, secondi - così le ore rosse sono sempre in primo piano)
  // Lancetta minuti (più lunga) - usa digitColor con intensità ridotta
  float minAngle = (m * 6 - 90) * 3.14159 / 180.0;
  CRGB minuteColor = CRGB(digitColor.r * 0.7, digitColor.g * 0.7, digitColor.b * 0.7);

  for (int len = 0; len <= 5; len++) {
    int x = centerX + (int)(len * cos(minAngle) + 0.5);
    int y = centerY + (int)(len * sin(minAngle) + 0.5);
    if (x >= 0 && x < 16 && y >= 0 && y < 16) {
      setPixel(x, y, minuteColor);
    }
  }

  // Lancetta ore (più corta) - ROSSO FISSO (disegnata dopo i minuti per rimanere sempre visibile)
  float hourAngle = ((h % 12) * 30 + m * 0.5 - 90) * 3.14159 / 180.0;
  CRGB hourColor = CRGB(255, 0, 0); // ROSSO per lancetta ore
  for (int len = 0; len <= 3; len++) {
    int x = centerX + (int)(len * cos(hourAngle) + 0.5);
    int y = centerY + (int)(len * sin(hourAngle) + 0.5);
    if (x >= 0 && x < 16 && y >= 0 && y < 16) {
      setPixel(x, y, hourColor);
    }
  }

  // Lancetta secondi (sempre visibile) - colore secondi
  float secAngle = (s * 6 - 90) * 3.14159 / 180.0;
  for (int len = 0; len <= 6; len++) {
    int x = centerX + (int)(len * cos(secAngle) + 0.5);
    int y = centerY + (int)(len * sin(secAngle) + 0.5);
    if (x >= 0 && x < 16 && y >= 0 && y < 16) {
      setPixel(x, y, getSecondsLedColor());
    }
  }

  // Centro (singolo pixel) - sempre bianco brillante
  setPixel(centerX, centerY, CRGB(255, 255, 255));
}

void drawClockLarge(int h, int m, int s, CRGB digitColor) {
  // Display grande: HH in alto, MM in basso con cifre grandi (5x7) - Vecchio stile
  // LED lampeggiante al centro

  int h1 = h / 10;
  int h2 = h % 10;
  int m1 = m / 10;
  int m2 = m % 10;

  // ORE in alto (y=1), centrate
  // 2 cifre (5 LED) + 1 spazio = 11 LED totali, offset (16-11)/2 = 2
  drawBigDigitOldStyle(h1, 3, 1, digitColor);
  drawBigDigitOldStyle(h2, 9, 1, digitColor);

  // LED lampeggiante al centro (tra ore e minuti)
  if (s % 2 == 0) {
    setPixel(7, 8, getSecondsLedColor());
    setPixel(8, 8, getSecondsLedColor());
  }

  // MINUTI in basso (y=9), centrate
  drawBigDigitOldStyle(m1, 3, 9, digitColor);
  drawBigDigitOldStyle(m2, 9, 9, digitColor);
}

// ============================================
// TIPO DI OROLOGIO VERTICALE
// ============================================

void drawClockVertical(int h, int m, int s, CRGB digitColor) {
  // Display verticale: cifre impilate dall'alto al basso
  // H H (riga 1-2)
  // : (riga 3)
  // M M (riga 4-5)

  int h1 = h / 10;
  int h2 = h % 10;
  int m1 = m / 10;
  int m2 = m % 10;

  // ORE in alto - piccole cifre affiancate (usando drawSmallDigit) - spostate 1 LED a destra
  drawSmallDigit(h1, 4, 1, digitColor);
  drawSmallDigit(h2, 8, 1, digitColor);

  // Linea separatrice / due punti (senza LED centrale)
  if (s % 2 == 0) {
    setPixel(6, 7, getSecondsLedColor());
    setPixel(8, 7, getSecondsLedColor());
  }

  // MINUTI in basso - piccole cifre affiancate - spostate 1 LED a destra
  drawSmallDigit(m1, 4, 10, digitColor);
  drawSmallDigit(m2, 8, 10, digitColor);
}

void drawClockScrolling(int h, int m, int s, CRGB digitColor) {
  // Orologio digitale scorrevole con font 5x7
  // Formato: HH:MM:SS che scorre orizzontalmente in loop continuo

  static int scrollPos = 16; // Posizione iniziale (parte da destra)
  static unsigned long lastScrollTime = 0;

  unsigned long currentMillis = millis();

  // Aggiorna posizione scroll (velocità normale: ogni 100ms)
  if (currentMillis - lastScrollTime >= 100) {
    scrollPos--;

    // Quando il testo esce completamente a sinistra, riportalo a destra
    // Lunghezza testo "HH:MM:SS" = 8 caratteri * 6 pixel (5+1 spazio) = 48 pixel
    if (scrollPos < -48) {
      scrollPos = 16; // Ricomincia da destra (loop continuo)
    }

    lastScrollTime = currentMillis;
  }

  // Crea stringa orario HH:MM:SS (aggiornata automaticamente ogni secondo)
  String timeStr = "";
  if (h < 10) timeStr += "0";
  timeStr += String(h);
  timeStr += ":";
  if (m < 10) timeStr += "0";
  timeStr += String(m);
  timeStr += ":";
  if (s < 10) timeStr += "0";
  timeStr += String(s);

  // Disegna il testo scorrevole con font 5x7
  int xPos = scrollPos;
  int yPos = 4; // Centrato verticalmente (16-7)/2 = 4-5, uso 4

  for (unsigned int i = 0; i < timeStr.length(); i++) {
    // Disegna anche caratteri parzialmente visibili (da -5 a 16)
    if (xPos > -6 && xPos < 16) {
      drawChar5x7(timeStr[i], xPos, yPos, digitColor);
    }
    xPos += 6; // 5 pixel carattere + 1 spazio
  }
}

void drawCharacter(char ch, int x, int y, CRGB color) {
  switch (ch) {
    case 'A': case 'a':
      setPixel(x+1, y, color); // Picco triangolare
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); } // Barra centrale
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'B': case 'b':
      setPixel(x, y, color); setPixel(x+1, y, color); // Solo 2 pixel in alto
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); // Solo 2 pixel al centro
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); // Solo 2 pixel in basso
      break;
    case 'C': case 'c':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'D': case 'd':
      for (int i = 0; i < 2; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 2; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'E': case 'e':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'F': case 'f':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color);
      setPixel(x, y+4, color);
      break;
    case 'G': case 'g':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'H': case 'h':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'I': case 'i':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x+1, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'K': case 'k':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'L': case 'l':
      setPixel(x, y, color);
      setPixel(x, y+1, color);
      setPixel(x, y+2, color);
      setPixel(x, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'M': case 'm':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'N': case 'n':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'O': case 'o':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'Q': case 'q':
      // ███
      // █ █
      // █ █
      // ███
      //  ██
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'P': case 'p':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color);
      setPixel(x, y+4, color);
      break;
    case 'R': case 'r':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+1, y+2, color); // Solo 2 pixel (gamba)
      setPixel(x, y+3, color); setPixel(x+1, y+3, color); // Diagonale
      setPixel(x, y+4, color); setPixel(x+2, y+4, color); // Gamba separata
      break;
    case 'S': case 's':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); // Solo sinistra
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x+2, y+3, color); // Solo destra
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'T': case 't':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x+1, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      setPixel(x+1, y+4, color);
      break;
    case 'U': case 'u':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case 'V': case 'v':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x+1, y+4, color);
      break;
    case 'W': case 'w':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'X': case 'x':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      setPixel(x, y+4, color); setPixel(x+2, y+4, color);
      break;
    case 'Y': case 'y':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      setPixel(x+1, y+4, color);
      break;
    case 'Z': case 'z':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;

    // Numeri
    case '0':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      setPixel(x, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case '1':
      setPixel(x+1, y, color);
      setPixel(x, y+1, color); setPixel(x+1, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      setPixel(x, y+4, color); setPixel(x+1, y+4, color); setPixel(x+2, y+4, color);
      break;
    case '2':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case '3':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color); setPixel(x+2, y+2, color);
      setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case '4':
      setPixel(x, y, color); setPixel(x+2, y, color);
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x+2, y+3, color);
      setPixel(x+2, y+4, color);
      break;
    case '5':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case '6':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case '7':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x+2, y+1, color);
      setPixel(x+1, y+2, color);
      setPixel(x+1, y+3, color);
      setPixel(x+1, y+4, color);
      break;
    case '8':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x, y+3, color); setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;
    case '9':
      for (int i = 0; i < 3; i++) { setPixel(x+i, y, color); }
      setPixel(x, y+1, color); setPixel(x+2, y+1, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+2, color); }
      setPixel(x+2, y+3, color);
      for (int i = 0; i < 3; i++) { setPixel(x+i, y+4, color); }
      break;

    // Punteggiatura
    case '.':
      setPixel(x+1, y+4, color);
      break;
    case ':':
      setPixel(x+1, y+1, color);
      setPixel(x+1, y+3, color);
      break;
    case '%':
      // Simbolo percentuale 3x5 - più chiaro e riconoscibile
      // █ █  <- cerchietti agli angoli
      //   █  <- diagonale
      //  █   <- centro
      // █    <- diagonale
      // █ █  <- cerchietti agli angoli
      setPixel(x, y, color);        // Punto alto sinistra
      setPixel(x+2, y, color);      // Punto alto destra
      setPixel(x+2, y+1, color);    // Diagonale discendente
      setPixel(x+1, y+2, color);    // Centro (incrocio)
      setPixel(x, y+3, color);      // Diagonale discendente
      setPixel(x, y+4, color);      // Punto basso sinistra
      setPixel(x+2, y+4, color);    // Punto basso destra
      break;
    case ' ':
      break;
    default:
      for (int dx = 0; dx < 3; dx++) {
        for (int dy = 0; dy < 5; dy++) {
          if (dx == 0 || dx == 2 || dy == 0 || dy == 4) {
            setPixel(x + dx, y + dy, color);
          }
        }
      }
      break;
  }
}

// ============================================
// LOOP PRINCIPALE
// ============================================
void loop() {
  if (apMode) {
    dnsServer.processNextRequest();
  }

  server.handleClient();
  events(); // ezTime update

  // === WiFi Watchdog: controlla connessione ogni 30 secondi ===
  if (wifiConnected && !apMode && !bluetoothMode) {
    static unsigned long lastWifiCheck = 0;
    static uint8_t wifiFailCount = 0;
    if (millis() - lastWifiCheck > 30000) {
      lastWifiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        wifiFailCount++;
        Serial.printf("[WiFi Watchdog] Disconnesso! Tentativo riconnessione %d/5\n", wifiFailCount);
        WiFi.disconnect(true);
        delay(100);
        WiFiConfig wdConfig;
        EEPROM.get(0, wdConfig);
        WiFi.begin(wdConfig.ssid, wdConfig.password);
        // Attendi riconnessione (max 10 secondi)
        unsigned long waitStart = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - waitStart < 10000) {
          delay(250);
          yield();
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("[WiFi Watchdog] Riconnesso! IP: %s\n", WiFi.localIP().toString().c_str());
          wifiFailCount = 0;
        } else if (wifiFailCount >= 5) {
          Serial.println("[WiFi Watchdog] 5 tentativi falliti, riavvio ESP32...");
          ESP.restart();
        }
      } else {
        wifiFailCount = 0; // Reset contatore se connesso
      }
    }
  }

  // Aggiorna dati sensore HTU21
  readSensorData();

  // Gestione Sveglia (GLOBALE - funziona in tutti gli stati)
  checkAlarm();
  if (alarmRinging) {
    handleAlarmRinging();
  }
  // Aggiorna riproduzione melodia (sistema non bloccante)
  updateMelody();

  // Controlla eventi del calendario
  checkCalendarEvents();

  // Controlla cronotermostato
  checkThermostat();

  // Processa input gamepad Bluetooth (solo se in modalità Bluetooth)
  #ifdef ENABLE_BLUEPAD32
  if (bluetoothMode) {
    // Controlla timeout pairing (1 minuto)
    if (btWaitingForController && (millis() - btPairingStartTime > BT_PAIRING_TIMEOUT)) {
      Serial.println("Bluetooth pairing timeout! Returning to WiFi mode...");
      disableBluetoothMode();
    } else {
      processGamepadInput();
    }
  }

  // Gestione modalità Pairing Bluetooth (dalla pagina web /btpairing)
  if (btPairingModeActive) {
    // Aggiorna stato Bluepad32 per ricevere eventi di connessione
    BP32.update();

    // Controlla se il controller TARGET si è connesso
    bool targetConnected = false;
    if (btPairingTargetSlot >= 0) {
      // Pairing di uno slot specifico: controlla solo quello slot
      int slot = btPairingTargetSlot;
      if (gamepadConnected[slot] || (myControllers[slot] && myControllers[slot]->isConnected())) {
        targetConnected = true;
      }
    } else {
      // Pairing generico: qualsiasi controller connesso
      for (int i = 0; i < 2; i++) {
        if (gamepadConnected[i] || (myControllers[i] && myControllers[i]->isConnected())) {
          targetConnected = true;
          break;
        }
      }
    }

    // Se il controller target è connesso, completa il pairing automaticamente
    if (targetConnected) {
      Serial.println("=== PAIRING SUCCESSFUL - AUTO-COMPLETING ===");
      if (btPairingTargetSlot >= 0) {
        Serial.printf("Target slot %d successfully paired!\n", btPairingTargetSlot + 1);
      }

      // Aspetta un momento per confermare la connessione
      for (int i = 0; i < 10; i++) { delay(100); yield(); }

      // Mostra conferma verde sulla matrice
      fillMatrix(CRGB::Green);
      for (int i = 0; i < 15; i++) { delay(100); yield(); } // 1.5 sec per vedere il verde

      // Reset target slot
      btPairingTargetSlot = -1;

      // Usa lo stesso metodo del tasto SELECT: quick restart per tornare a WiFi
      // Il pairing è già salvato nella NVS di Bluepad32 e in EEPROM
      Serial.println("Pairing saved! Restarting to WiFi mode...");
      quickRestartToWiFi();
    }

    // Controlla timeout pairing (3 minuti)
    if (millis() - btPairingModeStart > BT_PAIRING_MODE_TIMEOUT) {
      Serial.println("Bluetooth pairing timeout (3 min)! Returning to WiFi...");

      // Disabilita nuove connessioni Bluetooth
      BP32.enableNewBluetoothConnections(false);
      btPairingModeActive = false;
      btPairingTargetSlot = -1;
      delay(200);
      yield();

      // Usa transizione robusta a WiFi
      showWiFiText();
      bool wifiOk = transitionToWiFi(3);

      if (wifiOk) {
        startWebServer();
        changeState(STATE_GAME_CLOCK);
        forceRedraw = true;
      } else {
        Serial.println("WiFi recovery failed after timeout - starting AP mode");
        startWiFiManager();
      }
    }

    // Debug: stampa stato ogni 5 secondi
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 5000) {
      lastDebugTime = millis();
      unsigned long elapsed = (millis() - btPairingModeStart) / 1000;
      Serial.printf("BT Pairing: searching... (%lu sec elapsed, timeout %lu sec)\n",
                    elapsed, BT_PAIRING_MODE_TIMEOUT / 1000);
      Serial.printf("  gamepadConnected: [%d, %d]\n", gamepadConnected[0], gamepadConnected[1]);
      Serial.printf("  myControllers: [%s, %s]\n",
                    myControllers[0] ? "set" : "null",
                    myControllers[1] ? "set" : "null");
    }

    // Lampeggia icona Bluetooth ogni secondo per indicare ricerca attiva
    static unsigned long lastBlinkTime = 0;
    static bool blinkState = true;
    if (millis() - lastBlinkTime > 1000) {
      lastBlinkTime = millis();
      blinkState = !blinkState;
      if (blinkState) {
        showBluetoothPairingScreen();
      } else {
        // Schermo leggermente più scuro per effetto lampeggio
        clearMatrix();
        CRGB dimBlue = CRGB(0, 0, 80);
        // Simbolo Bluetooth più scuro
        for (int y = 3; y <= 12; y++) setPixel(7, y, dimBlue);
        for (int y = 3; y <= 12; y++) setPixel(8, y, dimBlue);
        setPixel(9, 4, dimBlue); setPixel(10, 5, dimBlue); setPixel(11, 6, dimBlue);
        setPixel(6, 4, dimBlue); setPixel(5, 5, dimBlue); setPixel(4, 6, dimBlue);
        setPixel(9, 11, dimBlue); setPixel(10, 10, dimBlue); setPixel(11, 9, dimBlue);
        setPixel(6, 11, dimBlue); setPixel(5, 10, dimBlue); setPixel(4, 9, dimBlue);
        FastLED.show();
      }
    }

    // IMPORTANTE: Salta il resto del loop durante il pairing per evitare artefatti
    return;
  }
  #endif

  // Controlla pulsanti fisici PRIMA di disegnare
  checkButtons();

  // Applica aggiornamento luminosità pendente dal web
  // Fatto qui nel loop principale per evitare artefatti da interrupt WiFi
  if (pendingBrightnessUpdate) {
    pendingBrightnessUpdate = false;
    FastLED.setBrightness(currentBrightness);
    FastLED.show();
    delayMicroseconds(300);
    FastLED.show();
  }

  // Gestione automatica passaggio da scroll IP a orologio
  if (ipScrollActive && wifiConnected && currentState == STATE_GAME_TEXT_SCROLL) {
    int textLength = scrollText.length();

    // Calcola charWidth in base alla dimensione del font
    int charWidth;
    if (scrollTextSize == 0) {
      charWidth = 4; // Piccolo
    } else if (scrollTextSize == 1) {
      charWidth = 6; // Medio
    } else {
      charWidth = 12; // Grande
    }

    // Passa all'orologio quando il testo è completamente uscito dallo schermo
    if (scrollPosition < -textLength * charWidth) {
      ipScrollActive = false;
      changeState(STATE_GAME_CLOCK);
    }
  }

  // Gestione spegnimento programmato e manuale (funziona in tutti gli stati)
  static bool wasShutdown = false;
  static SystemState stateBeforeShutdown = STATE_GAME_CLOCK;
  static bool wasInScheduledShutdown = false;
  static bool matrixLedEnabledBeforeScheduledShutdown = true;

  // Controlla se siamo in periodo di spegnimento programmato
  bool inScheduledShutdown = isInShutdownPeriod();

  // Sincronizza matrixLedEnabled con lo spegnimento programmato
  if (inScheduledShutdown && !wasInScheduledShutdown) {
    // Entriamo in un periodo di spegnimento programmato
    matrixLedEnabledBeforeScheduledShutdown = matrixLedEnabled;
    matrixLedEnabled = false;
    saveConfig(); // Salva il nuovo stato in EEPROM
    wasInScheduledShutdown = true;
  } else if (!inScheduledShutdown && wasInScheduledShutdown) {
    // Usciamo da un periodo di spegnimento programmato
    matrixLedEnabled = matrixLedEnabledBeforeScheduledShutdown;
    saveConfig(); // Ripristina lo stato precedente in EEPROM
    wasInScheduledShutdown = false;
  }

  // Controlla se dobbiamo spegnere i LED (spegnimento programmato O spegnimento manuale)
  bool shouldShutdown = inScheduledShutdown || !matrixLedEnabled;

  if (shouldShutdown) {
    // Spegne il display
    if (!wasShutdown) {
      // Salva lo stato corrente prima di spegnere
      stateBeforeShutdown = currentState;
      clearMatrix();
      wasShutdown = true;
    }
    // Quando spento, non eseguiamo il resto del loop
    return;
  } else {
    // Forza ridisegno e ripristina stato quando si esce dallo spegnimento
    if (wasShutdown) {
      // Se eravamo in stato orologio, forza il ridisegno
      if (stateBeforeShutdown == STATE_GAME_CLOCK || stateBeforeShutdown == STATE_GAME_WEATHER) {
        changeState(STATE_GAME_CLOCK);
        forceRedraw = true;
      } else {
        // Per gli altri stati, ripristina lo stato precedente
        changeState(stateBeforeShutdown);
      }
      wasShutdown = false;
    }
  }

  // Se in modalità Bluetooth in attesa controller o START, non disegnare altri stati
  #ifdef ENABLE_BLUEPAD32
  if (bluetoothMode && (btWaitingForController || btWaitingForStart)) {
    return;
  }
  #endif

  // Gestione stato corrente
  switch (currentState) {
    case STATE_GAME_MENU:
      drawMenu();
      break;

    case STATE_GAME_TRIS:
      // TRIS visualizzato sulla matrice
      if (previousState != STATE_GAME_TRIS) {
        drawTrisOnMatrix();
      }
      break;

    case STATE_GAME_TEXT_SCROLL:
      scrollTextOnMatrix();
      break;

    case STATE_GAME_CLOCK:
      {
        // *** QUANDO LA SVEGLIA SUONA: MOSTRA SOLO OROLOGIO (lampeggio rosso) ***
        if (alarmRinging) {
          currentDisplayMode = 0;
          showingClock = true;
          drawClockOnMatrix();
          break;
        }

        // Gestione alternanza automatica Orologio/Meteo/Data/Sensore Locale
        if (clockWeatherAutoSwitch) {
          static int lastDisplayMode = 0;
          // sequenceIndex ora è globale (dichiarato sopra)

          // Costruisci sequenza dinamica basata su cosa è abilitato
          // 0=Orologio, 1=Meteo, 2=Data, 3=Sensore Locale
          static int dynamicSequence[4];
          static int cachedSequenceLength = 0;
          static bool lastWeatherAvailable = false;
          static bool lastDateEnabled = false;
          static bool lastSensorEnabled = false;
          static int lastDisplaySequence = -1;

          // Ricostruisci sequenza solo se cambiano le condizioni
          bool needsRebuild = (weatherDataAvailable != lastWeatherAvailable) ||
                              (dateDisplayEnabled != lastDateEnabled) ||
                              (localSensorDisplayEnabled != lastSensorEnabled) ||
                              (displaySequence != lastDisplaySequence);

          if (needsRebuild || cachedSequenceLength == 0) {
            cachedSequenceLength = 0;

            // Costruisce la sequenza base in base a displaySequence
            // 0: Orologio → Meteo → Data
            // 1: Orologio → Data → Meteo
            // 2: Data → Orologio → Meteo
            // 4: Meteo → Orologio → Data
            // 5: Meteo → Data → Orologio

            if (displaySequence == 0) {
              // Orologio → Meteo → Data
              dynamicSequence[cachedSequenceLength++] = 0;
              if (weatherDataAvailable) dynamicSequence[cachedSequenceLength++] = 1;
              if (dateDisplayEnabled) dynamicSequence[cachedSequenceLength++] = 2;
            } else if (displaySequence == 1) {
              // Orologio → Data → Meteo
              dynamicSequence[cachedSequenceLength++] = 0;
              if (dateDisplayEnabled) dynamicSequence[cachedSequenceLength++] = 2;
              if (weatherDataAvailable) dynamicSequence[cachedSequenceLength++] = 1;
            } else if (displaySequence == 2) {
              // Data → Orologio → Meteo
              if (dateDisplayEnabled) dynamicSequence[cachedSequenceLength++] = 2;
              dynamicSequence[cachedSequenceLength++] = 0;
              if (weatherDataAvailable) dynamicSequence[cachedSequenceLength++] = 1;
            } else if (displaySequence == 4) {
              // Meteo → Orologio → Data
              if (weatherDataAvailable) dynamicSequence[cachedSequenceLength++] = 1;
              dynamicSequence[cachedSequenceLength++] = 0;
              if (dateDisplayEnabled) dynamicSequence[cachedSequenceLength++] = 2;
            } else if (displaySequence == 5) {
              // Meteo → Data → Orologio
              if (weatherDataAvailable) dynamicSequence[cachedSequenceLength++] = 1;
              if (dateDisplayEnabled) dynamicSequence[cachedSequenceLength++] = 2;
              dynamicSequence[cachedSequenceLength++] = 0;
            } else {
              // Fallback: Orologio sempre presente
              dynamicSequence[cachedSequenceLength++] = 0;
              if (weatherDataAvailable) dynamicSequence[cachedSequenceLength++] = 1;
              if (dateDisplayEnabled) dynamicSequence[cachedSequenceLength++] = 2;
            }

            // Sensore locale sempre alla fine se abilitato e disponibile
            if (localSensorDisplayEnabled && sensorAvailable) {
              dynamicSequence[cachedSequenceLength++] = 3;
            }

            // Aggiorna cache stati
            lastWeatherAvailable = weatherDataAvailable;
            lastDateEnabled = dateDisplayEnabled;
            lastSensorEnabled = localSensorDisplayEnabled;
            lastDisplaySequence = displaySequence;

            // Reset sequenceIndex quando ricostruiamo
            sequenceIndex = 0;
          }

          int sequenceLength = cachedSequenceLength;

          // Se sequenceIndex fuori range, resettalo
          if (sequenceIndex >= sequenceLength) {
            sequenceIndex = 0;
          }

          // Ottieni la modalità corrente dalla sequenza
          currentDisplayMode = dynamicSequence[sequenceIndex];

          // Determina l'intervallo corrente in base a cosa stiamo mostrando
          unsigned long currentInterval;
          if (currentDisplayMode == 0) {
            currentInterval = (unsigned long)clockDisplayInterval * 1000;
          } else if (currentDisplayMode == 1) {
            currentInterval = (unsigned long)weatherDisplayInterval * 1000;
          } else if (currentDisplayMode == 2) {
            currentInterval = (unsigned long)dateDisplayInterval * 1000;
          } else {
            currentInterval = (unsigned long)localSensorDisplayInterval * 1000;
          }

          // Controlla se è tempo di cambiare visualizzazione
          if (millis() - lastClockWeatherSwitch >= currentInterval) {
            // Avanza nella sequenza
            sequenceIndex++;
            if (sequenceIndex >= sequenceLength) {
              sequenceIndex = 0;
            }
            currentDisplayMode = dynamicSequence[sequenceIndex];

            lastClockWeatherSwitch = millis();

            // Setta flag per forzare ridisegno completo
            forceRedraw = true;

            // Cancella la matrice quando si cambia modalità per evitare interferenze
            clearMatrix();
          }

          // Mostra orologio, meteo, data o sensore locale in base allo stato
          if (currentDisplayMode == 0) {
            drawClockOnMatrix();
            showingClock = true; // Mantieni compatibilità
          } else if (currentDisplayMode == 1) {
            drawWeatherOnMatrix();
            showingClock = false; // Mantieni compatibilità
          } else if (currentDisplayMode == 2) {
            drawDateOnMatrix();
            showingClock = false; // Mantieni compatibilità
          } else if (currentDisplayMode == 3) {
            drawLocalSensorOnMatrix();
            showingClock = false; // Mantieni compatibilità
          }

          // Reset del flag dopo il disegno
          forceRedraw = false;

          lastDisplayMode = currentDisplayMode;
        } else {
          // Comportamento normale: solo orologio
          currentDisplayMode = 0;
          showingClock = true;
          drawClockOnMatrix();
        }
      }
      break;

    case STATE_GAME_WEATHER:
      drawWeatherOnMatrix();
      break;

    case STATE_GAME_SPACE_INVADERS:
      // GAME OVER ha priorità sull'HUD
      if (siGameOver) {
        hudOverlayActive = false;  // Disattiva HUD durante GAME OVER
        updateSpaceInvaders();
      } else if (hudOverlayActive) {
        updateGameHUD();
      } else {
        updateSpaceInvaders();
      }
      break;

    case STATE_GAME_PONG:
      if (hudOverlayActive) {
        updateGameHUD();
      } else {
        updatePong();
      }
      break;

    case STATE_GAME_SNAKE:
      if (hudOverlayActive) {
        updateGameHUD();
      } else {
        updateSnake();
      }
      break;

    case STATE_GAME_BREAKOUT:
      // GAME OVER ha priorità sull'HUD
      if (breakoutGameOver) {
        hudOverlayActive = false;  // Disattiva HUD durante GAME OVER
        updateBreakout();
      } else if (hudOverlayActive) {
        updateGameHUD();
      } else {
        updateBreakout();
      }
      break;

    case STATE_GAME_SCOREBOARD:
      drawScoreboard();
      break;

    case STATE_GAME_TETRIS:
      if (hudOverlayActive) {
        updateGameHUD();
      } else {
        updateTetris();
      }
      break;

    case STATE_GAME_PACMAN:
      // GAME OVER ha priorità sull'HUD
      if (pacmanGameOver) {
        hudOverlayActive = false;  // Disattiva HUD durante GAME OVER
        updatePacman();
      } else if (hudOverlayActive) {
        updateGameHUD();
      } else {
        updatePacman();
      }
      break;

    case STATE_GAME_SIMON:
      updateSimon();
      break;

    case STATE_GAME_ZUMA:
      updateZuma();
      break;

    case STATE_STOPWATCH:
      updateStopwatch();
      drawStopwatch();
      break;

    case STATE_TIMER:
      updateTimer();
      drawTimer();
      break;

    case STATE_CALENDAR_EVENT:
      drawCalendarEvent();
      break;

    case STATE_THERMOSTAT:
      drawThermostat();
      break;

    case STATE_WIFI_SETUP:
      {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;

        if (millis() - lastBlink > 1000) {
          blinkState = !blinkState;
          if (blinkState) {
            displayWiFiSetupMode();
          } else {
            clearMatrix();
          }
          lastBlink = millis();
        }
      }
      break;

    default:
      break;
  }

  previousState = currentState;

  // Aggiornamento automatico meteo ogni 10 minuti
  // FIX: Aggiorna anche se weatherDataAvailable è false ma le coordinate sono configurate
  if (wifiConnected) {
    // Verifica se le coordinate meteo sono configurate
    WiFiConfig configCheck;
    EEPROM.get(0, configCheck);
    bool hasWeatherConfig = (configCheck.weatherLatitude != 0.0 || configCheck.weatherLongitude != 0.0) && strlen(configCheck.weatherCity) > 0;

    if (hasWeatherConfig && (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL)) {
      Serial.println("Auto-updating weather data...");
      if (updateWeatherData()) {
        updateForecastData();
        weatherDataAvailable = true; // FIX: Imposta il flag quando l'aggiornamento ha successo
        lastWeatherUpdate = millis();
      } else {
        // Se fallisce, riprova dopo 2 minuti invece di 10
        lastWeatherUpdate = millis() - WEATHER_UPDATE_INTERVAL + 120000;
        Serial.println("Weather update failed, retry in 2 minutes");
      }
    }
  }

  // Debug periodico (ogni 30 secondi)
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 30000) {
    debugWiFiStatus();
    lastDebug = millis();
  }
}

void drawMenu() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 2000 || lastUpdate == 0) {
    clearMatrix();

    CRGB color1 = CRGB(255, 0, 0);
    CRGB color2 = CRGB(0, 255, 0);
    CRGB color3 = CRGB(0, 0, 255);
    CRGB color4 = CRGB(255, 255, 0);

    // Quadrato 1: Tris (rosso)
    for (int x = 2; x < 6; x++) {
      for (int y = 2; y < 6; y++) {
        setPixel(x, y, color1);
      }
    }
    for (int i = -1; i <= 1; i++) {
      setPixel(3 + i, 3 + i, CRGB(255, 255, 255));
      setPixel(3 + i, 5 - i, CRGB(255, 255, 255));
    }

    // Quadrato 2: Testo scorrevole (verde)
    for (int x = 10; x < 14; x++) {
      for (int y = 2; y < 6; y++) {
        setPixel(x, y, color2);
      }
    }
    for (int i = 0; i < 3; i++) {
      setPixel(11 + i, 3, CRGB(255, 255, 255));
    }
    setPixel(12, 4, CRGB(255, 255, 255));
    setPixel(12, 5, CRGB(255, 255, 255));

    // Quadrato 3: Menu principale (blu)
    for (int x = 2; x < 6; x++) {
      for (int y = 10; y < 14; y++) {
        setPixel(x, y, color3);
      }
    }
    setPixel(2, 11, CRGB(255, 255, 255));
    setPixel(2, 12, CRGB(255, 255, 255));
    setPixel(3, 11, CRGB(255, 255, 255));
    setPixel(4, 12, CRGB(255, 255, 255));
    setPixel(5, 11, CRGB(255, 255, 255));
    setPixel(5, 12, CRGB(255, 255, 255));

    // Quadrato 4: Orologio (giallo)
    for (int x = 10; x < 14; x++) {
      for (int y = 10; y < 14; y++) {
        setPixel(x, y, color4);
      }
    }
    setPixel(11, 11, CRGB(255, 255, 255));
    setPixel(12, 11, CRGB(255, 255, 255));
    setPixel(13, 11, CRGB(255, 255, 255));
    setPixel(11, 12, CRGB(255, 255, 255));
    setPixel(13, 12, CRGB(255, 255, 255));
    setPixel(11, 13, CRGB(255, 255, 255));
    setPixel(12, 13, CRGB(255, 255, 255));
    setPixel(13, 13, CRGB(255, 255, 255));
    setPixel(12, 12, CRGB(0, 0, 0));

    FastLED.show();
    lastUpdate = millis();
  }
}

void changeState(SystemState newState) {
  if (currentState != newState) {
    previousState = currentState;
    currentState = newState;

    // Reset variabili specifiche dello stato quando si cambia
    if (newState != STATE_GAME_TEXT_SCROLL) {
      scrollPosition = MATRIX_WIDTH;
      lastScrollUpdate = 0;
    }

    // Reset timer alternanza quando si entra in modalità orologio
    if (newState == STATE_GAME_CLOCK) {
      lastClockWeatherSwitch = millis();
      showingClock = true;
      currentDisplayMode = 0; // Reset a orologio
    }

    clearMatrix();
  }
}

bool isInShutdownPeriod() {
  if (!wifiConnected) return false; // Se non connesso a WiFi, non spegnere

  int h = myTZ.hour();
  int m = myTZ.minute();
  int currentMinutes = h * 60 + m;

  // Controlla spegnimento notturno
  if (nightShutdownEnabled) {
    int startMinutes = nightShutdownStartHour * 60 + nightShutdownStartMinute;
    int endMinutes = nightShutdownEndHour * 60 + nightShutdownEndMinute;

    // Gestisce il caso in cui l'intervallo attraversa la mezzanotte
    if (startMinutes > endMinutes) {
      // Es: 23:00 - 07:00
      if (currentMinutes >= startMinutes || currentMinutes < endMinutes) {
        return true;
      }
    } else {
      // Es: 01:00 - 05:00
      if (currentMinutes >= startMinutes && currentMinutes < endMinutes) {
        return true;
      }
    }
  }

  // Controlla spegnimento diurno
  if (dayShutdownEnabled) {
    // Controlla se oggi è un giorno abilitato per lo spegnimento diurno
    int wd = myTZ.weekday(); // ezTime: 0=Dom, 1=Lun...6=Sab
    uint8_t dayBit = (wd == 0) ? 6 : (wd - 1); // Converti a bit0=Lun...bit6=Dom
    if (dayShutdownDays & (1 << dayBit)) {
      int startMinutes = dayShutdownStartHour * 60 + dayShutdownStartMinute;
      int endMinutes = dayShutdownEndHour * 60 + dayShutdownEndMinute;

      // Gestisce il caso in cui l'intervallo attraversa la mezzanotte (improbabile ma possibile)
      if (startMinutes > endMinutes) {
        if (currentMinutes >= startMinutes || currentMinutes < endMinutes) {
          return true;
        }
      } else {
        if (currentMinutes >= startMinutes && currentMinutes < endMinutes) {
          return true;
        }
      }
    }
  }

  return false;
}

// ============================================
// FUNZIONI AUDIO/BUZZER
// ============================================

// Definizioni frequenze note musicali per le suonerie
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_D6  1175
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_G6  1568

void playTone(int frequency, int duration) {
  if (!soundEnabled) return;
  // RIMOSSO delay - completamente non bloccante
  tone(BUZZER_PIN, frequency, duration);
}

void playBeep() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 1000, 50);
}

void playSuccess() {
  if (!soundEnabled) return;
  // Suono singolo non bloccante
  tone(BUZZER_PIN, 1000, 100);  // Beep alto breve
}

void playError() {
  if (!soundEnabled) return;
  // Suono singolo non bloccante
  tone(BUZZER_PIN, 200, 150);  // Beep basso
}

void playGameOver() {
  if (!soundEnabled) return;
  // Suono singolo non bloccante
  tone(BUZZER_PIN, 150, 300);  // Beep basso lungo
}

void playLevelUp() {
  if (!soundEnabled) return;
  // Suono singolo non bloccante
  tone(BUZZER_PIN, 1500, 150);  // Beep molto alto
}

void playEat() {
  if (!soundEnabled) return;
  // Suono singolo non bloccante
  tone(BUZZER_PIN, 800, 30);
}

void playShoot() {
  if (!soundEnabled) return;
  // Suono laser arcade - salita rapida
  tone(BUZZER_PIN, 800, 20);
  delay(20);
  tone(BUZZER_PIN, 1000, 20);
  delay(20);
  tone(BUZZER_PIN, 1200, 20);
}

// ============================================
// BLUETOOTH GAMEPAD - IMPLEMENTAZIONE
// ============================================
#ifdef ENABLE_BLUEPAD32

// Callback quando un controller si connette
void onConnectedController(ControllerPtr ctl) {
  bool foundSlot = false;
  int assignedSlot = -1;

  // Se siamo in modalità pairing con slot specifico
  if (btPairingModeActive && btPairingTargetSlot >= 0) {
    int targetSlot = btPairingTargetSlot;

    // Se lo slot target è già occupato da un altro controller, lo liberiamo
    if (myControllers[targetSlot] != nullptr && myControllers[targetSlot] != ctl) {
      Serial.printf("CONTROLLER: Slot %d occupied, disconnecting old controller\n", targetSlot);
      myControllers[targetSlot]->disconnect();
      myControllers[targetSlot] = nullptr;
      gamepadConnected[targetSlot] = false;
      delay(100);
    }

    // Assegna il nuovo controller allo slot target
    Serial.printf("CONTROLLER: Connected to TARGET slot %d\n", targetSlot);
    myControllers[targetSlot] = ctl;
    gamepadConnected[targetSlot] = true;
    foundSlot = true;
    assignedSlot = targetSlot;

    // Feedback sonoro connessione
    playSuccess();

    Serial.println("=== CONTROLLER PAIRED SUCCESSFULLY ===");
    Serial.printf("Controller %d connected during pairing mode!\n", targetSlot + 1);

    // Salva in EEPROM che questo controller è stato associato
    if (targetSlot == 0) {
      btController1Paired = true;
    } else {
      btController2Paired = true;
    }
    saveConfig();
    Serial.printf("Controller %d paired flag saved to EEPROM\n", targetSlot + 1);
  }
  else {
    // Comportamento normale: assegna al primo slot libero
    for (int i = 0; i < 2; i++) {
      if (myControllers[i] == nullptr) {
        Serial.printf("CONTROLLER: Connected, index=%d\n", i);
        myControllers[i] = ctl;
        gamepadConnected[i] = true;
        foundSlot = true;
        assignedSlot = i;

        // Feedback sonoro connessione
        playSuccess();

        // Se siamo in modalità Pairing generica (dalla pagina web /btpairing)
        if (btPairingModeActive) {
          Serial.println("=== CONTROLLER PAIRED SUCCESSFULLY ===");
          Serial.printf("Controller %d connected during pairing mode!\n", i + 1);
          // Salva in EEPROM che questo controller è stato associato
          if (i == 0) {
            btController1Paired = true;
          } else {
            btController2Paired = true;
          }
          saveConfig();
          Serial.printf("Controller %d paired flag saved to EEPROM\n", i + 1);
        }
        // Se stavamo aspettando un controller per il gioco Bluetooth
        else if (btWaitingForController && i == 0) {
          btWaitingForController = false;
          btWaitingForStart = true;
          Serial.println("Controller connected! Showing 'Press START' screen");
          showBluetoothWaitingStart();
        }
        break;
      }
    }
  }

  // Se è il secondo controller e siamo in Pong, disabilita AI
  if (assignedSlot == 1 && savedGameState == STATE_GAME_PONG) {
    pongVsAI = false;
    Serial.println("PONG: Player 2 connected, AI disabled");
  }

  if (!foundSlot) {
    Serial.println("CONTROLLER: No empty slot found");
  }
}

// Callback quando un controller si disconnette
void onDisconnectedController(ControllerPtr ctl) {
  for (int i = 0; i < 2; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("CONTROLLER: Disconnected, index=%d\n", i);
      myControllers[i] = nullptr;
      gamepadConnected[i] = false;

      // Reset stato gamepad
      gamepadStates[i] = {false, false, false, false, false, false, false, false, false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

      // Feedback sonoro disconnessione
      playError();

      // Se era il secondo controller in Pong, riabilita AI
      if (i == 1 && currentState == STATE_GAME_PONG) {
        pongVsAI = true;
        Serial.println("PONG: Player 2 disconnected, AI enabled");
      }
      break;
    }
  }
}

// Inizializza Bluepad32
void setupGamepads() {
  Serial.println("Bluepad32: Initializing...");

  // Setup Bluepad32 callbacks
  BP32.setup(&onConnectedController, &onDisconnectedController);

  // Opzionale: dimentica dispositivi precedenti per nuovo pairing
  // BP32.forgetBluetoothKeys();

  // Abilita nuovi pairing (necessario per trovare nuovi controller)
  BP32.enableNewBluetoothConnections(true);

  Serial.println("Bluepad32: Ready! Put your controller in pairing mode.");
  Serial.println("8BitDo: Hold Start+B for 3 seconds");
  Serial.println("PS4/PS5: Hold Share+PS for 3 seconds");
}
#endif

// ============================================
// ROBUST RADIO MANAGEMENT FUNCTIONS
// ============================================

// Shutdown semplice del WiFi (solo API Arduino) - velocizzato
void robustWiFiShutdown() {
  Serial.println("[RADIO] WiFi shutdown...");

  // Ferma web server
  server.stop();
  delay(50);
  yield();

  // Disconnetti WiFi
  WiFi.disconnect(true);
  delay(100);
  yield();

  // Disabilita WiFi
  WiFi.mode(WIFI_OFF);
  delay(100);
  yield();

  wifiConnected = false;
  Serial.println("[RADIO] WiFi shutdown complete");
}

// Avvio semplice WiFi (solo API Arduino) - velocizzato
bool robustWiFiStartup() {
  Serial.println("[RADIO] WiFi startup...");

  // Imposta modalità STA
  WiFi.mode(WIFI_STA);
  delay(100);
  yield();

  Serial.println("[RADIO] WiFi startup complete");
  return true;
}

// Prepara la radio per Bluetooth (velocizzato)
void prepareRadioForBluetooth() {
  Serial.println("[RADIO] Preparing for Bluetooth...");

  // Ferma web server
  server.stop();
  delay(50);
  yield();

  // Disconnetti e spegni WiFi
  WiFi.disconnect(true);
  delay(100);
  yield();

  WiFi.mode(WIFI_OFF);
  delay(100);
  yield();

  wifiConnected = false;
  Serial.println("[RADIO] Ready for Bluetooth");
}

// Prepara la radio per WiFi - velocizzato
bool prepareRadioForWiFi() {
  Serial.println("[RADIO] Preparing for WiFi...");

  #ifdef ENABLE_BLUEPAD32
  // Disabilita nuove connessioni BT
  BP32.enableNewBluetoothConnections(false);
  delay(100);
  yield();
  #endif

  // Pausa per stabilizzazione
  delay(100);
  yield();

  // Avvia WiFi
  return robustWiFiStartup();
}

// Connessione WiFi semplice
bool robustWiFiConnect(bool showBootText) {
  WiFiConfig config;
  EEPROM.get(0, config);

  if (config.magicNumber != 0xCAFE || strlen(config.ssid) == 0) {
    Serial.println("[RADIO] Invalid WiFi config");
    return false;
  }

  Serial.printf("[RADIO] Connecting to: %s\n", config.ssid);

  if (showBootText) {
    displayBootText();
  }

  // Inizia connessione
  WiFi.begin(config.ssid, config.password);

  // Attendi connessione (max 15 secondi)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    yield();
    Serial.print(".");
    attempts++;
  }

  if (showBootText) {
    clearMatrix();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[RADIO] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    wifiConnected = true;
    return true;
  } else {
    Serial.println("\n[RADIO] Connection failed");
    wifiConnected = false;
    return false;
  }
}

// Transizione da Bluetooth a WiFi
bool transitionToWiFi(int maxRetries) {
  Serial.println("[RADIO] === TRANSITION TO WIFI ===");

  for (int retry = 1; retry <= maxRetries; retry++) {
    Serial.printf("[RADIO] Attempt %d/%d\n", retry, maxRetries);

    // Prepara radio per WiFi
    prepareRadioForWiFi();

    // Tenta connessione
    if (robustWiFiConnect(false)) {
      Serial.println("[RADIO] === SUCCESS ===");
      return true;
    }

    // Se fallito e ci sono altri tentativi
    if (retry < maxRetries) {
      Serial.println("[RADIO] Retrying...");
      WiFi.disconnect(true);
      delay(1000);
      yield();
    }
  }

  Serial.println("[RADIO] === FAILED ===");
  return false;
}

// Reset di emergenza della radio (da usare come ultima risorsa)
void emergencyRadioReset() {
  Serial.println("[RADIO] !!! EMERGENCY RADIO RESET !!!");

  // Salva che dobbiamo riconnetterci dopo restart
  // (potremmo usare EEPROM o RTC memory)

  // Restart ESP32
  Serial.println("[RADIO] Restarting ESP32...");
  delay(1000);
  ESP.restart();
}

// ============================================
// BLUETOOTH/WIFI MODE SWITCHING
// ============================================

// Abilita modalità Bluetooth (disabilita WiFi)
void enableBluetoothMode() {
  enableBluetoothModeWithGame(currentState);
}

// Abilita modalità Bluetooth con gioco specifico
void enableBluetoothModeWithGame(SystemState gameState) {
  #ifdef ENABLE_BLUEPAD32
  if (bluetoothMode) {
    Serial.println("Bluetooth mode already active");
    return;
  }

  Serial.println("=== ENABLING BLUETOOTH MODE ===");

  // 1. Salva lo stato del gioco corrente
  savedGameState = gameState;
  Serial.print("Game state saved: ");
  Serial.println(savedGameState);

  // 2. Usa funzione robusta per preparare radio per Bluetooth
  prepareRadioForBluetooth();

  // 3. Abilita nuove connessioni Bluetooth (BP32 già inizializzato in setup())
  BP32.enableNewBluetoothConnections(true);
  delay(50);
  yield();
  Serial.println("Bluetooth new connections ENABLED");

  // 4. Imposta flag
  bluetoothMode = true;
  btWaitingForController = true;
  btWaitingForStart = false;
  btPairingStartTime = millis();

  // 5. Reset dei timestamp gamepad per evitare input fantasma
  for (int i = 0; i < 2; i++) {
    gamepadStates[i].lastButtonStart = 0;
    gamepadStates[i].lastButtonSelect = 0;
    gamepadStates[i].lastButtonA = 0;
    gamepadStates[i].lastButtonB = 0;
    gamepadStates[i].lastDpadUp = 0;
    gamepadStates[i].lastDpadDown = 0;
    gamepadStates[i].lastDpadLeft = 0;
    gamepadStates[i].lastDpadRight = 0;
    gamepadStates[i].buttonSelect = false;
  }

  // 6. Feedback visivo: mostra animazione pairing sulla matrice
  showBluetoothPairingScreen();

  Serial.println("Bluetooth mode ENABLED - Waiting for controller...");
  Serial.println("Put your 8BitDo controller in pairing mode (Start+B for 3 sec)");
  #else
  Serial.println("Bluepad32 not enabled in this build");
  #endif
}

// Mostra schermata di pairing Bluetooth
void showBluetoothPairingScreen() {
  clearMatrix();

  // Disegna icona Bluetooth stilizzata al centro
  CRGB btColor = CRGB::Blue;

  // Simbolo Bluetooth (rombo con frecce)
  // Linea verticale centrale
  for (int y = 3; y <= 12; y++) setPixel(7, y, btColor);
  for (int y = 3; y <= 12; y++) setPixel(8, y, btColor);

  // Frecce superiori
  setPixel(9, 4, btColor); setPixel(10, 5, btColor); setPixel(11, 6, btColor);
  setPixel(6, 4, btColor); setPixel(5, 5, btColor); setPixel(4, 6, btColor);

  // Frecce inferiori (invertite)
  setPixel(9, 11, btColor); setPixel(10, 10, btColor); setPixel(11, 9, btColor);
  setPixel(6, 11, btColor); setPixel(5, 10, btColor); setPixel(4, 9, btColor);

  // Connessioni diagonali
  setPixel(4, 6, btColor); setPixel(11, 9, btColor);
  setPixel(4, 9, btColor); setPixel(11, 6, btColor);

  FastLED.show();
}

// Mostra scritta "WIFI" sulla matrice
// Riavvio rapido da Bluetooth a WiFi (SELECT sul gamepad)
void quickRestartToWiFi() {
  Serial.println("=== QUICK RESTART TO WIFI ===");

  #ifdef ENABLE_BLUEPAD32
  // Disconnetti controller velocemente
  for (int i = 0; i < 2; i++) {
    if (myControllers[i] && myControllers[i]->isConnected()) {
      myControllers[i]->disconnect();
    }
    myControllers[i] = nullptr;
  }
  BP32.enableNewBluetoothConnections(false);
  #endif

  // Mostra "WIFI" sulla matrice
  showWiFiText();
  delay(300);  // Breve pausa per vedere "WIFI"

  // Imposta flag per quick restart
  quickRestartFlag = QUICK_RESTART_MAGIC;

  // Riavvio immediato
  ESP.restart();
}

void showWiFiText() {
  clearMatrix();
  CRGB wifiColor = CRGB(0, 255, 255); // Ciano
  // W (x=2-6)
  setPixel(2, 5, wifiColor); setPixel(2, 6, wifiColor); setPixel(2, 7, wifiColor); setPixel(2, 8, wifiColor); setPixel(2, 9, wifiColor); setPixel(2, 10, wifiColor);
  setPixel(3, 10, wifiColor); setPixel(4, 9, wifiColor); setPixel(4, 8, wifiColor);
  setPixel(5, 10, wifiColor);
  setPixel(6, 5, wifiColor); setPixel(6, 6, wifiColor); setPixel(6, 7, wifiColor); setPixel(6, 8, wifiColor); setPixel(6, 9, wifiColor); setPixel(6, 10, wifiColor);
  // I (x=8)
  setPixel(8, 5, wifiColor); setPixel(8, 6, wifiColor); setPixel(8, 7, wifiColor); setPixel(8, 8, wifiColor); setPixel(8, 9, wifiColor); setPixel(8, 10, wifiColor);
  // F (x=10-12)
  setPixel(10, 5, wifiColor); setPixel(10, 6, wifiColor); setPixel(10, 7, wifiColor); setPixel(10, 8, wifiColor); setPixel(10, 9, wifiColor); setPixel(10, 10, wifiColor);
  setPixel(11, 5, wifiColor); setPixel(12, 5, wifiColor);
  setPixel(11, 7, wifiColor); setPixel(12, 7, wifiColor);
  // I (x=14)
  setPixel(14, 5, wifiColor); setPixel(14, 6, wifiColor); setPixel(14, 7, wifiColor); setPixel(14, 8, wifiColor); setPixel(14, 9, wifiColor); setPixel(14, 10, wifiColor);
  FastLED.show();
}

// Disabilita modalità Bluetooth (riabilita WiFi)
void disableBluetoothMode() {
  if (!bluetoothMode) {
    Serial.println("Bluetooth mode not active");
    return;
  }

  Serial.println("=== DISABLING BLUETOOTH MODE ===");

  #ifdef ENABLE_BLUEPAD32
  // 1. Disconnetti tutti i controller in modo pulito
  for (int i = 0; i < 2; i++) {
    if (myControllers[i] && myControllers[i]->isConnected()) {
      Serial.printf("Disconnecting controller %d\n", i);
      myControllers[i]->disconnect();
      delay(100);
      yield();
    }
    myControllers[i] = nullptr;
    gamepadConnected[i] = false;
  }
  delay(200);
  yield();

  // 2. Disabilita nuove connessioni Bluetooth
  BP32.enableNewBluetoothConnections(false);
  Serial.println("Bluetooth new connections disabled");
  delay(200);
  yield();
  #endif

  // 3. Imposta flag PRIMA di riconnettere WiFi
  bluetoothMode = false;
  btWaitingForController = false;
  btWaitingForStart = false;

  // 4. Mostra "WIFI" sulla matrice
  showWiFiText();

  // 5. Usa transizione robusta a WiFi (con retry integrato)
  bool wifiOk = transitionToWiFi(3);  // 3 tentativi

  if (wifiOk) {
    // 6. Riavvia web server
    startWebServer();
    Serial.println("Web server restarted");
    Serial.print("Web server available at: http://");
    Serial.println(WiFi.localIP());
  } else {
    // 7. Se fallisce, vai in AP mode (NON riavviare)
    Serial.println("=== WIFI RECOVERY FAILED - Starting AP Mode ===");
    startWiFiManager();
  }

  // 8. Mantieni "WIFI" visibile per 1 secondo
  for (int i = 0; i < 10; i++) {
    delay(100);
    yield();
  }

  // 9. Vai all'orologio
  changeState(STATE_GAME_CLOCK);
  Serial.println("Switching to CLOCK");
  Serial.println("=== BLUETOOTH MODE DISABLED ===");
}

// Mostra schermata "Premi START" dopo connessione controller
void showBluetoothWaitingStart() {
  clearMatrix();

  // Mostra "GO" grande in verde
  CRGB goColor = CRGB::Green;

  // G (semplificata)
  for (int y = 4; y <= 11; y++) setPixel(2, y, goColor);
  for (int x = 2; x <= 5; x++) setPixel(x, 4, goColor);
  for (int x = 2; x <= 5; x++) setPixel(x, 11, goColor);
  setPixel(5, 10, goColor);
  setPixel(5, 9, goColor);
  setPixel(4, 8, goColor);
  setPixel(5, 8, goColor);

  // O
  for (int y = 4; y <= 11; y++) setPixel(8, y, goColor);
  for (int y = 4; y <= 11; y++) setPixel(13, y, goColor);
  for (int x = 8; x <= 13; x++) setPixel(x, 4, goColor);
  for (int x = 8; x <= 13; x++) setPixel(x, 11, goColor);

  // Lampeggia punto esclamativo sotto
  setPixel(7, 14, CRGB::Yellow);
  setPixel(8, 14, CRGB::Yellow);
  setPixel(7, 13, CRGB::Yellow);
  setPixel(8, 13, CRGB::Yellow);

  FastLED.show();

  Serial.println("Showing 'GO!' - Press START on controller to begin game");
}

// Handler web per attivare Bluetooth
void handleEnableBluetooth() {
  // Determina quale gioco attivare dal parametro
  SystemState gameToStart = STATE_GAME_MENU;

  if (server.hasArg("game")) {
    String game = server.arg("game");
    if (game == "tris") gameToStart = STATE_GAME_TRIS;
    else if (game == "pong") gameToStart = STATE_GAME_PONG;
    else if (game == "snake") gameToStart = STATE_GAME_SNAKE;
    else if (game == "tetris") gameToStart = STATE_GAME_TETRIS;
    else if (game == "pacman") gameToStart = STATE_GAME_PACMAN;
    else if (game == "spaceinvaders") gameToStart = STATE_GAME_SPACE_INVADERS;
    else if (game == "breakout") gameToStart = STATE_GAME_BREAKOUT;
    else if (game == "simon") gameToStart = STATE_GAME_SIMON;
    else if (game == "zuma") gameToStart = STATE_GAME_ZUMA;

    Serial.print("Bluetooth mode requested for game: ");
    Serial.println(game);
  }

  // Invia risposta prima di disabilitare WiFi
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Bluetooth Mode</title>";
  html += "<style>body{background:#000;color:#fff;font-family:Arial;text-align:center;padding:50px;}";
  html += ".bt-icon{font-size:100px;margin:30px;}</style>";
  html += "</head><body>";
  html += "<div class='bt-icon'>🎮</div>";
  html += "<h1>Modalità Bluetooth Attivata!</h1>";
  html += "<p>Il WiFi verrà disabilitato.</p>";
  html += "<p>Metti il controller 8BitDo in pairing mode (Start+B per 3 sec)</p>";
  html += "<p>Quando connesso, premi <strong>START</strong> sul controller per iniziare</p>";
  html += "<p><strong>Premi SELECT per 2 secondi sul gamepad per tornare al WiFi</strong></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);

  // Aspetta che la risposta sia inviata (velocizzato)
  delay(200);

  // Ora attiva Bluetooth con il gioco specificato
  enableBluetoothModeWithGame(gameToStart);
}

// ============================================
// BLUETOOTH PAIRING PAGE - Pagina per associare controller
// ============================================

void handleBluetoothPairing() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>CONSOLE QUADRA - Bluetooth Pairing</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#000;color:#fff;margin:0;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;text-align:center;}";
  html += "h1{color:#2196F3;margin-bottom:10px;}";
  html += ".subtitle{color:#888;margin-bottom:30px;}";
  html += ".status-box{background:#111;border:2px solid #333;border-radius:15px;padding:20px;margin:20px 0;}";
  html += ".status-box.active{border-color:#4CAF50;background:#0a1a0a;}";
  html += ".status-box.pairing{border-color:#FF9800;background:#1a1500;animation:pulse 2s infinite;}";
  html += "@keyframes pulse{0%,100%{opacity:1;}50%{opacity:0.7;}}";
  html += ".controller-slot{background:#1a1a1a;border:2px solid #333;border-radius:10px;padding:15px;margin:10px 0;display:flex;align-items:center;justify-content:space-between;}";
  html += ".controller-slot.connected{border-color:#4CAF50;background:#0a1a0a;}";
  html += ".controller-icon{font-size:2em;margin-right:15px;}";
  html += ".controller-info{text-align:left;flex-grow:1;}";
  html += ".controller-name{font-weight:bold;font-size:1.1em;}";
  html += ".controller-status{color:#888;font-size:0.9em;}";
  html += ".controller-status.online{color:#4CAF50;}";
  html += "button{padding:15px 30px;font-size:1.1em;border:none;border-radius:10px;cursor:pointer;margin:10px;transition:all 0.3s;}";
  html += ".btn-primary{background:#2196F3;color:#fff;}";
  html += ".btn-primary:hover{background:#1976D2;}";
  html += ".btn-danger{background:#f44336;color:#fff;}";
  html += ".btn-danger:hover{background:#d32f2f;}";
  html += ".btn-secondary{background:#666;color:#fff;}";
  html += ".btn-secondary:hover{background:#555;}";
  html += ".btn-success{background:#4CAF50;color:#fff;}";
  html += ".btn-success:hover{background:#388E3C;}";
  html += ".btn-slot{background:#2196F3;color:#fff;padding:10px 15px;font-size:0.9em;margin:0;}";
  html += ".btn-slot:hover{background:#1976D2;}";
  html += ".btn-slot:disabled{background:#555;cursor:not-allowed;}";
  html += ".instructions{background:#1a1a2e;border-radius:10px;padding:20px;margin:20px 0;text-align:left;}";
  html += ".instructions h3{color:#FF9800;margin-top:0;}";
  html += ".instructions ol{margin:0;padding-left:20px;}";
  html += ".instructions li{margin:10px 0;color:#ccc;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".warning{background:#1a1500;border:1px solid #FF9800;border-radius:10px;padding:15px;margin:20px 0;color:#FF9800;}";
  html += "#statusMsg{padding:10px;border-radius:5px;margin:10px 0;display:none;}";
  html += "#statusMsg.success{display:block;background:#0a1a0a;border:1px solid #4CAF50;color:#4CAF50;}";
  html += "#statusMsg.error{display:block;background:#1a0a0a;border:1px solid #f44336;color:#f44336;}";
  html += "#statusMsg.info{display:block;background:#0a0a1a;border:1px solid #2196F3;color:#2196F3;}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<h1>🎮 Bluetooth Pairing</h1>";
  html += "<p class='subtitle'>Associa fino a 2 controller Bluetooth</p>";

  html += "<div class='warning'>";
  html += "⚠️ <strong>Nota:</strong> Durante il pairing il WiFi verrà temporaneamente disabilitato. ";
  html += "Al termine, il WiFi si riconnetterà automaticamente.";
  html += "</div>";

  html += "<div id='statusMsg'></div>";

  html += "<div class='status-box' id='pairingStatus'>";
  html += "<h3 id='pairingTitle'>Stato Pairing</h3>";
  html += "<p id='pairingText'>Seleziona quale controller associare</p>";
  html += "</div>";

  html += "<div class='controller-slot" + String(btController1Paired ? " connected" : "") + "' id='controller1'>";
  html += "<span class='controller-icon'>🎮</span>";
  html += "<div class='controller-info'>";
  html += "<div class='controller-name'>Controller 1</div>";
  html += "<div class='controller-status" + String(btController1Paired ? " online" : "") + "' id='ctrl1Status'>" + String(btController1Paired ? "Associato" : "Non connesso") + "</div>";
  html += "</div>";
  html += "<button class='btn-slot' id='btnSlot0' onclick='startPairing(0)'>🔗 Associa</button>";
  html += "</div>";

  html += "<div class='controller-slot" + String(btController2Paired ? " connected" : "") + "' id='controller2'>";
  html += "<span class='controller-icon'>🎮</span>";
  html += "<div class='controller-info'>";
  html += "<div class='controller-name'>Controller 2</div>";
  html += "<div class='controller-status" + String(btController2Paired ? " online" : "") + "' id='ctrl2Status'>" + String(btController2Paired ? "Associato" : "Non connesso") + "</div>";
  html += "</div>";
  html += "<button class='btn-slot' id='btnSlot1' onclick='startPairing(1)'>🔗 Associa</button>";
  html += "</div>";

  html += "<div style='margin:30px 0;'>";
  html += "<button class='btn-danger' id='btnStop' onclick='stopPairing()' style='display:none;'>⏹ Ferma Pairing</button>";
  html += "</div>";

  html += "<div style='margin:20px 0;'>";
  html += "<button class='btn-secondary' onclick='forgetDevices()'>🗑 Dimentica Dispositivi</button>";
  html += "</div>";

  html += "<div class='instructions'>";
  html += "<h3>📋 Istruzioni per il Pairing</h3>";
  html += "<ol>";
  html += "<li><strong>8BitDo Controller:</strong> Tieni premuto <strong>Start + B</strong> per 3 secondi</li>";
  html += "<li><strong>PS4/PS5 Controller:</strong> Tieni premuto <strong>Share + PS</strong> per 3 secondi</li>";
  html += "<li><strong>Xbox Controller:</strong> Tieni premuto il pulsante di <strong>pairing</strong> sul retro</li>";
  html += "<li><strong>Nintendo Switch Pro:</strong> Tieni premuto il pulsante <strong>Sync</strong> in alto</li>";
  html += "</ol>";
  html += "<p style='margin-top:15px;color:#888;'>Una volta associato, il controller verrà ricordato per le sessioni future.</p>";
  html += "<p style='margin-top:10px;color:#FF9800;'><strong>Nota:</strong> Per associare il Controller 2 quando il Controller 1 è già associato, usa prima 'Dimentica Dispositivi', poi associa prima il Controller 1 e poi il Controller 2.</p>";
  html += "</div>";

  html += "</div>";

  html += "<script>";
  html += "let pollInterval=null;";
  html += "let currentPairingSlot=-1;";
  html += "function showMsg(msg,type){let el=document.getElementById('statusMsg');el.textContent=msg;el.className=type;setTimeout(()=>{el.className='';},5000);}";
  html += "function startPairing(slot){";
  html += "  currentPairingSlot=slot;";
  html += "  let ctrlName=slot===0?'Controller 1':'Controller 2';";
  html += "  showMsg('Avvio pairing '+ctrlName+' in corso...','info');";
  html += "  fetch('/btpairingstart?slot='+slot).then(r=>r.json()).then(d=>{";
  html += "    if(d.success){";
  html += "      showMsg('Metti '+ctrlName+' in pairing mode!','success');";
  html += "      document.getElementById('btnSlot0').disabled=true;";
  html += "      document.getElementById('btnSlot1').disabled=true;";
  html += "      document.getElementById('btnStop').style.display='inline-block';";
  html += "      document.getElementById('pairingStatus').classList.add('pairing');";
  html += "      document.getElementById('pairingTitle').textContent='Pairing '+ctrlName+'...';";
  html += "      document.getElementById('pairingText').textContent='In attesa di '+ctrlName+' (timeout 3 minuti)';";
  html += "      pollInterval=setInterval(pollStatus,2000);";
  html += "    }else{showMsg('Errore: '+d.error,'error');}";
  html += "  }).catch(e=>showMsg('Errore connessione','error'));";
  html += "}";
  html += "function stopPairing(){";
  html += "  fetch('/btpairingstop').then(r=>r.json()).then(d=>{";
  html += "    showMsg('Pairing fermato. Riconnessione WiFi...','info');";
  html += "    if(pollInterval)clearInterval(pollInterval);";
  html += "    currentPairingSlot=-1;";
  html += "    setTimeout(()=>location.reload(),5000);";
  html += "  }).catch(e=>showMsg('Errore','error'));";
  html += "}";
  html += "function forgetDevices(){";
  html += "  fetch('/btforget').then(r=>r.json()).then(d=>{";
  html += "    if(d.success){";
  html += "      document.getElementById('ctrl1Status').textContent='Non connesso';";
  html += "      document.getElementById('ctrl1Status').classList.remove('online');";
  html += "      document.getElementById('controller1').classList.remove('connected');";
  html += "      document.getElementById('ctrl2Status').textContent='Non connesso';";
  html += "      document.getElementById('ctrl2Status').classList.remove('online');";
  html += "      document.getElementById('controller2').classList.remove('connected');";
  html += "    }";
  html += "  });";
  html += "}";
  html += "function pollStatus(){";
  html += "  fetch('/btpairingstatus').then(r=>r.json()).then(d=>{";
  html += "    if(d.ctrl1){document.getElementById('ctrl1Status').textContent=d.ctrl1;document.getElementById('ctrl1Status').classList.toggle('online',d.ctrl1!='Non connesso');document.getElementById('controller1').classList.toggle('connected',d.ctrl1!='Non connesso');}";
  html += "    if(d.ctrl2){document.getElementById('ctrl2Status').textContent=d.ctrl2;document.getElementById('ctrl2Status').classList.toggle('online',d.ctrl2!='Non connesso');document.getElementById('controller2').classList.toggle('connected',d.ctrl2!='Non connesso');}";
  html += "    if(!d.pairing){";
  html += "      clearInterval(pollInterval);";
  html += "      document.getElementById('pairingStatus').classList.remove('pairing');";
  html += "      document.getElementById('btnSlot0').disabled=false;";
  html += "      document.getElementById('btnSlot1').disabled=false;";
  html += "      document.getElementById('btnStop').style.display='none';";
  html += "      document.getElementById('pairingTitle').textContent='Pairing completato';";
  html += "      document.getElementById('pairingText').textContent='WiFi riconnesso';";
  html += "      currentPairingSlot=-1;";
  html += "    }";
  html += "  }).catch(e=>{});";
  html += "}";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Avvia modalità pairing Bluetooth
void handleBluetoothPairingStart() {
  #ifdef ENABLE_BLUEPAD32
  if (bluetoothMode || btPairingModeActive) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Bluetooth già attivo\"}");
    return;
  }

  // Leggi parametro slot dalla URL (0 = Controller 1, 1 = Controller 2, -1 o assente = qualsiasi)
  btPairingTargetSlot = -1;
  if (server.hasArg("slot")) {
    int slot = server.arg("slot").toInt();
    if (slot >= 0 && slot <= 1) {
      btPairingTargetSlot = slot;
    }
  }

  Serial.println("=== STARTING BLUETOOTH PAIRING MODE ===");
  if (btPairingTargetSlot >= 0) {
    Serial.printf("Target: Controller %d only\n", btPairingTargetSlot + 1);
  } else {
    Serial.println("Target: Any available slot");
  }

  // Invia risposta PRIMA di disabilitare WiFi
  server.send(200, "application/json", "{\"success\":true}");
  delay(200);
  yield();

  // Imposta flag PRIMA di disabilitare WiFi
  btPairingModeActive = true;
  btPairingModeStart = millis();

  // Reset stato controller - se pairing specifico, resetta solo quello slot
  if (btPairingTargetSlot >= 0) {
    // Pairing specifico: resetta solo lo slot target
    myControllers[btPairingTargetSlot] = nullptr;
    gamepadConnected[btPairingTargetSlot] = false;
  } else {
    // Pairing generico: resetta entrambi
    for (int i = 0; i < 2; i++) {
      myControllers[i] = nullptr;
      gamepadConnected[i] = false;
    }
  }

  // Mostra icona Bluetooth sulla matrice
  showBluetoothPairingScreen();

  // Usa funzione robusta per preparare radio per Bluetooth
  prepareRadioForBluetooth();

  // Abilita nuove connessioni Bluetooth
  BP32.enableNewBluetoothConnections(true);
  delay(100);
  yield();
  Serial.println("Bluetooth new connections ENABLED - searching for controllers...");

  Serial.println("Bluetooth pairing mode started - waiting for controllers...");
  Serial.println("Pairing will auto-complete when a controller connects");
  #else
  server.send(200, "application/json", "{\"success\":false,\"error\":\"Bluepad32 non abilitato\"}");
  #endif
}

// Ferma modalità pairing e torna al WiFi
void handleBluetoothPairingStop() {
  #ifdef ENABLE_BLUEPAD32
  // Se pairing non è attivo, il WiFi potrebbe essere già connesso
  if (!btPairingModeActive) {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Pairing non attivo\"}");
    return;
  }

  Serial.println("=== STOPPING BLUETOOTH PAIRING MODE (MANUAL) ===");

  btPairingModeActive = false;
  btPairingTargetSlot = -1;

  // Disconnetti controller ma mantieni pairing salvato
  for (int i = 0; i < 2; i++) {
    if (myControllers[i] && myControllers[i]->isConnected()) {
      myControllers[i]->disconnect();
      delay(100);
      yield();
    }
    myControllers[i] = nullptr;
    gamepadConnected[i] = false;
  }
  delay(200);
  yield();

  // Disabilita nuove connessioni Bluetooth
  BP32.enableNewBluetoothConnections(false);
  Serial.println("Bluetooth new connections DISABLED");
  delay(200);
  yield();

  // Mostra scritta WIFI e usa transizione robusta
  showWiFiText();
  bool wifiOk = transitionToWiFi(3);

  if (wifiOk) {
    startWebServer();
    Serial.println("WiFi reconnected after manual pairing stop");
    changeState(STATE_GAME_CLOCK);
    forceRedraw = true;
  } else {
    Serial.println("WiFi recovery failed after manual stop - starting AP mode");
    startWiFiManager();
  }

  // Non possiamo inviare risposta perché abbiamo riavviato il server
  #else
  server.send(200, "application/json", "{\"success\":false}");
  #endif
}

// Stato del pairing (polling)
void handleBluetoothPairingStatus() {
  String json = "{";
  json += "\"pairing\":" + String(btPairingModeActive ? "true" : "false") + ",";

  #ifdef ENABLE_BLUEPAD32
  // Controller 1
  if (myControllers[0] && myControllers[0]->isConnected()) {
    json += "\"ctrl1\":\"Connesso ✓\",";
  } else if (btController1Paired) {
    json += "\"ctrl1\":\"Associato\",";
  } else {
    json += "\"ctrl1\":\"Non connesso\",";
  }

  // Controller 2
  if (myControllers[1] && myControllers[1]->isConnected()) {
    json += "\"ctrl2\":\"Connesso ✓\"";
  } else if (btController2Paired) {
    json += "\"ctrl2\":\"Associato\"";
  } else {
    json += "\"ctrl2\":\"Non connesso\"";
  }
  #else
  json += "\"ctrl1\":\"N/A\",\"ctrl2\":\"N/A\"";
  #endif

  json += "}";
  server.send(200, "application/json", json);
}

// Dimentica tutti i dispositivi Bluetooth associati
void handleBluetoothForget() {
  #ifdef ENABLE_BLUEPAD32
  BP32.forgetBluetoothKeys();
  // Resetta i flag dei controller associati
  btController1Paired = false;
  btController2Paired = false;
  saveConfig();
  Serial.println("All Bluetooth devices forgotten and flags reset");
  server.send(200, "application/json", "{\"success\":true}");
  #else
  server.send(200, "application/json", "{\"success\":false,\"error\":\"Bluepad32 non abilitato\"}");
  #endif
}

#ifdef ENABLE_BLUEPAD32
// Funzione principale per processare input gamepad
void processGamepadInput() {
  // Aggiorna stato Bluepad32
  BP32.update();

  // Controlla se SELECT è TENUTO PREMUTO per tornare a WiFi
  // IMPORTANTE: Richiede SELECT premuto per 500ms per evitare falsi positivi con START
  for (int i = 0; i < 2; i++) {
    ControllerPtr ctl = myControllers[i];
    if (ctl && ctl->isConnected() && ctl->isGamepad()) {
      uint16_t miscBtns = ctl->miscButtons();
      // Solo SELECT premuto (non START) per tornare a WiFi
      bool selectPressed = (miscBtns & MISC_BUTTON_BACK) != 0;
      bool startPressed = (miscBtns & MISC_BUTTON_START) != 0;

      if (selectPressed && !startPressed) {
        unsigned long now = millis();
        if (!gamepadStates[i].buttonSelect) {
          // SELECT appena premuto - salva timestamp
          gamepadStates[i].buttonSelect = true;
          gamepadStates[i].lastButtonSelect = now;
        } else {
          // SELECT già premuto - controlla se tenuto per 500ms
          if (now - gamepadStates[i].lastButtonSelect > 500) {
            Serial.println("SELECT held for 500ms - Quick restart to WiFi");
            gamepadStates[i].buttonSelect = false;
            quickRestartToWiFi();
            return;
          }
        }
      } else {
        // SELECT rilasciato - reset flag
        gamepadStates[i].buttonSelect = false;
      }
    }
  }

  // Se stiamo aspettando che l'utente prema START
  if (btWaitingForStart) {
    ControllerPtr ctl = myControllers[0];
    if (ctl && ctl->isConnected() && ctl->isGamepad()) {
      // Controlla se START è premuto
      if (ctl->miscButtons() & MISC_BUTTON_START) {
        Serial.println("START pressed! Initializing and starting game...");
        btWaitingForStart = false;

        // Inizializza e avvia il gioco in base al tipo
        switch (savedGameState) {
          case STATE_GAME_SPACE_INVADERS:
            resetSpaceInvaders(true);
            siGameActive = true;
            siGameOver = false;
            siLastUpdate = millis();
            break;
          case STATE_GAME_PONG:
            resetPong();
            pongGameActive = true;
            pongLastUpdate = millis();
            break;
          case STATE_GAME_SNAKE:
            resetSnake();
            snakeGameActive = true;
            snakeLastUpdate = millis();
            break;
          case STATE_GAME_TETRIS:
            resetTetris();
            tetrisGameActive = true;
            tetrisLastUpdate = millis();
            break;
          case STATE_GAME_PACMAN:
            resetPacman();
            playPacmanBeginningMelody();
            pacmanGameActive = true;
            pacmanLastUpdate = millis();
            break;
          case STATE_GAME_BREAKOUT:
            resetBreakout();
            breakoutGameActive = true;
            breakoutLastUpdate = millis();
            break;
          case STATE_GAME_SIMON:
            resetSimon();
            simonGameActive = true;
            break;
          case STATE_GAME_ZUMA:
            resetZuma();
            break;
          case STATE_GAME_TRIS:
            resetTrisGame();
            trisGameActive = true;
            break;
          default:
            break;
        }

        // Cambia stato
        changeState(savedGameState);
        forceRedraw = true;

        // Feedback sonoro
        playSuccess();

        // Piccola pausa per evitare input multipli
        delay(200);
        return;
      }
    }
    return; // Non processare altri input mentre aspettiamo START
  }

  // Processa ogni controller connesso
  for (int i = 0; i < 2; i++) {
    ControllerPtr ctl = myControllers[i];

    if (ctl && ctl->isConnected() && ctl->isGamepad()) {
      // Processa input per lo stato corrente
      processGamepadForCurrentState(i, ctl);
    }
  }
}

// Dispatcher per stato corrente
void processGamepadForCurrentState(int idx, ControllerPtr ctl) {
  switch (currentState) {
    case STATE_GAME_MENU:
      handleGamepadMenu(idx, ctl);
      break;
    case STATE_GAME_CLOCK:
    case STATE_GAME_WEATHER:
      handleGamepadClock(idx, ctl);
      break;
    case STATE_GAME_PONG:
      handleGamepadPong(idx, ctl);
      break;
    case STATE_GAME_SNAKE:
      handleGamepadSnake(idx, ctl);
      break;
    case STATE_GAME_TETRIS:
      handleGamepadTetris(idx, ctl);
      break;
    case STATE_GAME_PACMAN:
      handleGamepadPacman(idx, ctl);
      break;
    case STATE_GAME_SPACE_INVADERS:
      handleGamepadSpaceInvaders(idx, ctl);
      break;
    case STATE_GAME_BREAKOUT:
      handleGamepadBreakout(idx, ctl);
      break;
    case STATE_GAME_ZUMA:
      handleGamepadZuma(idx, ctl);
      break;
    default:
      // Altri stati: solo Start per tornare al menu
      if ((ctl->miscButtons() & MISC_BUTTON_MENU)) {
        unsigned long now = millis();
        if (now - gamepadStates[idx].lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
          gamepadStates[idx].lastButtonStart = now;
          changeState(STATE_GAME_MENU);
        }
      }
      break;
  }
}

// Handler navigazione menu
void handleGamepadMenu(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0 naviga il menu
  if (idx != 0) return;

  // D-Pad per navigazione cursore
  uint8_t dpad = ctl->dpad();
  int32_t axisY = ctl->axisY();  // Stick sinistro
  int32_t axisX = ctl->axisX();

  // Su
  if ((dpad & DPAD_UP_MASK) || axisY < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadUp > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadUp = now;
      menuCursorY = 0;
      playBeep();
    }
  }
  // Giù
  if ((dpad & DPAD_DOWN_MASK) || axisY > ANALOG_DEADZONE) {
    if (now - gs.lastDpadDown > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadDown = now;
      menuCursorY = 1;
      playBeep();
    }
  }
  // Sinistra
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadLeft > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadLeft = now;
      menuCursorX = 0;
      playBeep();
    }
  }
  // Destra
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    if (now - gs.lastDpadRight > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadRight = now;
      menuCursorX = 1;
      playBeep();
    }
  }

  // Pulsante A o Start per selezionare
  if (ctl->a() || (ctl->miscButtons() & MISC_BUTTON_MENU)) {
    if (now - gs.lastButtonA > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonA = now;

      // Mappa posizione cursore a stato
      int selection = menuCursorY * 2 + menuCursorX;
      playSuccess();

      switch (selection) {
        case 0: // Alto-sinistra: Tris
          changeState(STATE_GAME_TRIS);
          break;
        case 1: // Alto-destra: Testo scorrevole
          changeState(STATE_GAME_TEXT_SCROLL);
          break;
        case 2: // Basso-sinistra: Menu giochi (vai a Pong come primo gioco)
          changeState(STATE_GAME_PONG);
          break;
        case 3: // Basso-destra: Orologio
          changeState(STATE_GAME_CLOCK);
          break;
      }
    }
  }

  // Ridisegna menu con cursore
  drawMenuWithCursor();
}

// Handler orologio/meteo
void handleGamepadClock(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0
  if (idx != 0) return;

  uint8_t dpad = ctl->dpad();

  // Su/Giù per luminosità
  if (dpad & DPAD_UP_MASK) {
    if (now - gs.lastDpadUp > GAMEPAD_REPEAT_MS) {
      gs.lastDpadUp = now;
      currentBrightness += 5;
      if (currentBrightness > 128) currentBrightness = 128;
      FastLED.setBrightness(currentBrightness);
      FastLED.show();
    }
  }
  if (dpad & DPAD_DOWN_MASK) {
    if (now - gs.lastDpadDown > GAMEPAD_REPEAT_MS) {
      gs.lastDpadDown = now;
      currentBrightness -= 5;
      if (currentBrightness < 1) currentBrightness = 1;
      FastLED.setBrightness(currentBrightness);
      FastLED.show();
    }
  }

  // A per cambiare tipo orologio
  if (ctl->a()) {
    if (now - gs.lastButtonA > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonA = now;
      clockDisplayType++;
      if (clockDisplayType > 7) clockDisplayType = 0;
      forceRedraw = true;
      saveConfig();
      playBeep();
    }
  }

  // Start per tornare al menu
  if ((ctl->miscButtons() & MISC_BUTTON_MENU)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      changeState(STATE_GAME_MENU);
    }
  }
}

// Handler Pong (supporta 2 giocatori)
void handleGamepadPong(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // START per nuova partita (sempre attivo, riavvia dal livello 1)
  if ((ctl->miscButtons() & MISC_BUTTON_START)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      resetPong();
      pongGameActive = true;
      pongGameOver = false;
      pongLastUpdate = millis();
      playSuccess();
    }
  }

  // Durante HUD o se gioco non attivo, blocca movimenti
  if (hudOverlayActive || !pongGameActive) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisY = ctl->axisY();

  // ============================================
  // MODALITÀ 2 CONTROLLER (2 giocatori con 2 gamepad)
  // Controller 0 = Racchetta 1, Controller 1 = Racchetta 2
  // ============================================
  if (!pongVsAI && gamepadConnected[0] && gamepadConnected[1]) {
    // Ogni controller muove la propria racchetta con D-Pad e Stick
    int player = idx + 1;  // idx 0 = player 1, idx 1 = player 2

    // Su (D-Pad UP o Stick su)
    if ((dpad & DPAD_UP_MASK) || axisY < -ANALOG_DEADZONE) {
      if (now - gs.lastDpadUp > GAMEPAD_REPEAT_MS) {
        gs.lastDpadUp = now;
        pongMovePaddle(player, -1);
      }
    }
    // Giù (D-Pad DOWN o Stick giù)
    if ((dpad & DPAD_DOWN_MASK) || axisY > ANALOG_DEADZONE) {
      if (now - gs.lastDpadDown > GAMEPAD_REPEAT_MS) {
        gs.lastDpadDown = now;
        pongMovePaddle(player, 1);
      }
    }
  }
  // ============================================
  // MODALITÀ 1 CONTROLLER (vs IA o 2 giocatori con 1 gamepad)
  // D-Pad/Stick = Racchetta 1, Y/A = Racchetta 2
  // ============================================
  else {
    // Solo controller 0 in questa modalità
    if (idx != 0) return;

    // Giocatore 1 - Su (D-Pad UP o Stick su)
    if ((dpad & DPAD_UP_MASK) || axisY < -ANALOG_DEADZONE) {
      if (now - gs.lastDpadUp > GAMEPAD_REPEAT_MS) {
        gs.lastDpadUp = now;
        pongMovePaddle(1, -1);
      }
    }
    // Giocatore 1 - Giù (D-Pad DOWN o Stick giù)
    if ((dpad & DPAD_DOWN_MASK) || axisY > ANALOG_DEADZONE) {
      if (now - gs.lastDpadDown > GAMEPAD_REPEAT_MS) {
        gs.lastDpadDown = now;
        pongMovePaddle(1, 1);
      }
    }

    // Giocatore 2 con stesso controller (solo se non vs IA)
    if (!pongVsAI) {
      // Giocatore 2 - Su (tasto Y)
      if (ctl->y()) {
        if (now - gs.lastButtonY > GAMEPAD_REPEAT_MS) {
          gs.lastButtonY = now;
          pongMovePaddle(2, -1);
        }
      }
      // Giocatore 2 - Giù (tasto A)
      if (ctl->a()) {
        if (now - gs.lastButtonA > GAMEPAD_REPEAT_MS) {
          gs.lastButtonA = now;
          pongMovePaddle(2, 1);
        }
      }
    }
  }
}

// Handler Snake
void handleGamepadSnake(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0
  if (idx != 0) return;

  // START per nuova partita (sempre attivo, riavvia dal livello 1)
  if ((ctl->miscButtons() & MISC_BUTTON_START)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      resetSnake();
      playSuccess();
    }
  }

  // Durante HUD o se gioco non attivo, blocca movimenti
  if (hudOverlayActive || !snakeGameActive) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisX = ctl->axisX();
  int32_t axisY = ctl->axisY();

  // Direzione con D-pad o stick
  if ((dpad & DPAD_UP_MASK) || axisY < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadUp > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadUp = now;
      snakeChangeDir(0, -1);
    }
  }
  if ((dpad & DPAD_DOWN_MASK) || axisY > ANALOG_DEADZONE) {
    if (now - gs.lastDpadDown > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadDown = now;
      snakeChangeDir(0, 1);
    }
  }
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadLeft > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadLeft = now;
      snakeChangeDir(-1, 0);
    }
  }
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    if (now - gs.lastDpadRight > GAMEPAD_DEBOUNCE_MS) {
      gs.lastDpadRight = now;
      snakeChangeDir(1, 0);
    }
  }
}

// Handler Tetris
void handleGamepadTetris(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0
  if (idx != 0) return;

  // START per nuova partita (sempre attivo, riavvia dal livello 1)
  if ((ctl->miscButtons() & MISC_BUTTON_START)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      resetTetris();
      playSuccess();
    }
  }

  // Durante HUD o se gioco non attivo, blocca movimenti
  if (hudOverlayActive || !tetrisGameActive) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisX = ctl->axisX();
  int32_t axisY = ctl->axisY();

  // Sinistra
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadLeft > GAMEPAD_REPEAT_MS) {
      gs.lastDpadLeft = now;
      tetrisMovePiece(-1, 0);
    }
  }
  // Destra
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    if (now - gs.lastDpadRight > GAMEPAD_REPEAT_MS) {
      gs.lastDpadRight = now;
      tetrisMovePiece(1, 0);
    }
  }
  // Giù (soft drop)
  if ((dpad & DPAD_DOWN_MASK) || axisY > ANALOG_DEADZONE) {
    if (now - gs.lastDpadDown > GAMEPAD_REPEAT_MS / 2) {
      gs.lastDpadDown = now;
      tetrisMovePiece(0, 1);
    }
  }

  // A per ruotare - solo una volta per pressione (non mentre tieni premuto)
  bool aPressed = ctl->a();
  if (aPressed && !gs.buttonA) {
    // Pulsante appena premuto (transizione da non premuto a premuto)
    tetrisRotatePiece();
  }
  gs.buttonA = aPressed;  // Salva stato corrente per prossimo ciclo

  // B per hard drop - solo una volta per pressione
  bool bPressed = ctl->b();
  if (bPressed && !gs.buttonB) {
    // Hard drop: muovi giù finché possibile
    while (tetrisGameActive) {
      tetrisMovePiece(0, 1);
      if (!tetrisGameActive) break;
    }
  }
  gs.buttonB = bPressed;  // Salva stato corrente per prossimo ciclo
}

// Handler Pac-Man
void handleGamepadPacman(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0
  if (idx != 0) return;

  // START per nuova partita (sempre attivo, riavvia dal livello 1)
  if ((ctl->miscButtons() & MISC_BUTTON_START)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      resetPacman(true);  // true = full reset (livello 1)
      pacmanGameActive = true;
      playPacmanBeginningMelody();
    }
  }

  // Durante HUD o se gioco non attivo, blocca movimenti
  if (hudOverlayActive || !pacmanGameActive) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisX = ctl->axisX();
  int32_t axisY = ctl->axisY();

  // Direzioni
  if ((dpad & DPAD_UP_MASK) || axisY < -ANALOG_DEADZONE) {
    pacmanChangeDir(0, -1);
  }
  if ((dpad & DPAD_DOWN_MASK) || axisY > ANALOG_DEADZONE) {
    pacmanChangeDir(0, 1);
  }
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    pacmanChangeDir(-1, 0);
  }
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    pacmanChangeDir(1, 0);
  }
}

// Handler Space Invaders
void handleGamepadSpaceInvaders(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0 (single player)
  if (idx != 0) return;

  // START per nuova partita (sempre attivo, resetta al livello 1)
  if ((ctl->miscButtons() & MISC_BUTTON_START)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      // Resetta SEMPRE al livello 1, anche durante il gioco
      resetSpaceInvaders(true);
      siGameActive = true;
      siGameOver = false;
      siLastUpdate = millis();
      playSuccess();
    }
  }

  // Durante HUD o se gioco non attivo, blocca movimenti
  if (hudOverlayActive || !siGameActive) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisX = ctl->axisX();

  // Sinistra - movimento continuo
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadLeft > GAMEPAD_REPEAT_MS) {
      gs.lastDpadLeft = now;
      siMovePlayer(1, -1);
    }
  }

  // Destra - movimento continuo
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    if (now - gs.lastDpadRight > GAMEPAD_REPEAT_MS) {
      gs.lastDpadRight = now;
      siMovePlayer(1, 1);
    }
  }

  // A per sparare (come FIRE virtuale)
  if (ctl->a()) {
    if (now - gs.lastButtonA > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonA = now;
      siShoot(1);
    }
  }
}

// Handler Breakout
void handleGamepadBreakout(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0
  if (idx != 0) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisX = ctl->axisX();

  // START per nuova partita (sempre attivo, riavvia dal livello 1)
  if ((ctl->miscButtons() & MISC_BUTTON_START)) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      resetBreakout();  // Resetta al livello 1
      playSuccess();
    }
  }

  // Durante HUD o se gioco non attivo, blocca solo movimento e pulsante A
  if (hudOverlayActive || !breakoutGameActive) return;

  // Sinistra/Destra per muovere paddle
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadLeft > GAMEPAD_REPEAT_MS) {
      gs.lastDpadLeft = now;
      breakoutMovePaddle(-1);
    }
  }
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    if (now - gs.lastDpadRight > GAMEPAD_REPEAT_MS) {
      gs.lastDpadRight = now;
      breakoutMovePaddle(1);
    }
  }

  // A per rilasciare palla
  if (ctl->a()) {
    if (now - gs.lastButtonA > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonA = now;
      if (breakoutBallOnPaddle) {
        breakoutBallOnPaddle = false;
        breakoutBallVelX = 0.8;
        breakoutBallVelY = -1.0;
        breakoutBallDirX = 1;
        breakoutBallDirY = -1;
      }
    }
  }
}

#endif // ENABLE_BLUEPAD32

// Disegna menu con cursore visibile
void drawMenuWithCursor() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();

  // Aggiorna ogni 100ms per blink del cursore
  if (now - lastUpdate < 100) return;
  lastUpdate = now;

  clearMatrix();

  CRGB color1 = CRGB(255, 0, 0);
  CRGB color2 = CRGB(0, 255, 0);
  CRGB color3 = CRGB(0, 0, 255);
  CRGB color4 = CRGB(255, 255, 0);

  // Blink del cursore
  menuCursorBlink++;
  bool showCursor = (menuCursorBlink % 6) < 4;  // On per 400ms, off per 200ms

  // Quadrato 1: Tris (rosso) - posizione (0,0)
  CRGB c1 = (menuCursorX == 0 && menuCursorY == 0 && showCursor) ? CRGB(255, 128, 128) : color1;
  for (int x = 2; x < 6; x++) {
    for (int y = 2; y < 6; y++) {
      setPixel(x, y, c1);
    }
  }
  for (int i = -1; i <= 1; i++) {
    setPixel(3 + i, 3 + i, CRGB(255, 255, 255));
    setPixel(3 + i, 5 - i, CRGB(255, 255, 255));
  }

  // Quadrato 2: Testo (verde) - posizione (1,0)
  CRGB c2 = (menuCursorX == 1 && menuCursorY == 0 && showCursor) ? CRGB(128, 255, 128) : color2;
  for (int x = 10; x < 14; x++) {
    for (int y = 2; y < 6; y++) {
      setPixel(x, y, c2);
    }
  }
  for (int i = 0; i < 3; i++) {
    setPixel(11 + i, 3, CRGB(255, 255, 255));
  }
  setPixel(12, 4, CRGB(255, 255, 255));
  setPixel(12, 5, CRGB(255, 255, 255));

  // Quadrato 3: Giochi (blu) - posizione (0,1)
  CRGB c3 = (menuCursorX == 0 && menuCursorY == 1 && showCursor) ? CRGB(128, 128, 255) : color3;
  for (int x = 2; x < 6; x++) {
    for (int y = 10; y < 14; y++) {
      setPixel(x, y, c3);
    }
  }
  setPixel(2, 11, CRGB(255, 255, 255));
  setPixel(2, 12, CRGB(255, 255, 255));
  setPixel(3, 11, CRGB(255, 255, 255));
  setPixel(4, 12, CRGB(255, 255, 255));
  setPixel(5, 11, CRGB(255, 255, 255));
  setPixel(5, 12, CRGB(255, 255, 255));

  // Quadrato 4: Orologio (giallo) - posizione (1,1)
  CRGB c4 = (menuCursorX == 1 && menuCursorY == 1 && showCursor) ? CRGB(255, 255, 128) : color4;
  for (int x = 10; x < 14; x++) {
    for (int y = 10; y < 14; y++) {
      setPixel(x, y, c4);
    }
  }
  setPixel(11, 11, CRGB(255, 255, 255));
  setPixel(12, 11, CRGB(255, 255, 255));
  setPixel(13, 11, CRGB(255, 255, 255));
  setPixel(11, 12, CRGB(255, 255, 255));
  setPixel(13, 12, CRGB(255, 255, 255));
  setPixel(11, 13, CRGB(255, 255, 255));
  setPixel(12, 13, CRGB(255, 255, 255));
  setPixel(13, 13, CRGB(255, 255, 255));
  setPixel(12, 12, CRGB(0, 0, 0));

  // Indicatore connessione controller (pixel in basso a destra)
  if (gamepadConnected[0]) {
    setPixel(15, 15, CRGB(0, 255, 0));  // Verde se connesso
  }
  if (gamepadConnected[1]) {
    setPixel(14, 15, CRGB(0, 0, 255));  // Blu per secondo controller
  }

  FastLED.show();
}

// Disegna Tris con cursore per gamepad
void drawTrisWithCursor() {
  static unsigned long lastBlink = 0;
  static bool cursorVisible = true;

  unsigned long now = millis();
  if (now - lastBlink > 300) {
    lastBlink = now;
    cursorVisible = !cursorVisible;
  }

  // Usa drawTrisOnMatrix() come base, poi sovrapponi cursore
  drawTrisOnMatrix();

  // Se cursore abilitato, evidenzia la cella selezionata
  if (trisCursorEnabled && cursorVisible && trisGameActive) {
    int cursorX = trisCursorPos % 3;
    int cursorY = trisCursorPos / 3;

    // Calcola coordinate pixel della cella (griglia 3x3 centrata sulla matrice 16x16)
    // Ogni cella è circa 5x5 pixel
    int cellStartX = 1 + cursorX * 5;
    int cellStartY = 1 + cursorY * 5;

    // Disegna bordo cursore (colore ciano)
    CRGB cursorColor = CRGB(0, 255, 255);

    // Bordo superiore e inferiore
    for (int x = cellStartX; x < cellStartX + 5 && x < 16; x++) {
      setPixel(x, cellStartY, cursorColor);
      setPixel(x, cellStartY + 4, cursorColor);
    }
    // Bordo sinistro e destro
    for (int y = cellStartY; y < cellStartY + 5 && y < 16; y++) {
      setPixel(cellStartX, y, cursorColor);
      setPixel(cellStartX + 4, y, cursorColor);
    }

    FastLED.show();
  }
}

// ============================================
// SUONI SPACE INVADERS (ARCADE STYLE - NON BLOCCANTI)
// ============================================

// Funzioni non bloccanti per suoni Space Invaders
void playSIAlienExplosion() {
  if (!soundEnabled) return;
  // Suono esplosione breve non bloccante
  tone(BUZZER_PIN, 400, 80);
}

void playSIPlayerDeath() {
  if (!soundEnabled) return;
  // Suono morte giocatore (nota bassa lunga)
  tone(BUZZER_PIN, 200, 300);
}

void playSIGameOver() {
  if (!soundEnabled) return;
  // Suono game over (nota molto bassa)
  tone(BUZZER_PIN, 150, 400);
}

void playSILevelComplete() {
  if (!soundEnabled) return;
  // Suono vittoria (nota alta breve)
  tone(BUZZER_PIN, 1500, 200);
}

void playSIUFOHit() {
  if (!soundEnabled) return;
  // Suono UFO colpito (nota molto alta)
  tone(BUZZER_PIN, 2000, 150);
}

// ============================================
// MELODIE PER SVEGLIA (12 SUONERIE)
// ============================================

// 0 - Super Mario Bros Theme
const Note marioMelody[] = {
  {NOTE_E5, 150}, {NOTE_E5, 150}, {0, 150}, {NOTE_E5, 150},
  {0, 150}, {NOTE_C5, 150}, {NOTE_E5, 150}, {0, 150},
  {NOTE_G5, 150}, {0, 450}, {NOTE_G4, 150}, {0, 450}
};
const int marioMelodyLength = 12;

// 1 - The Legend of Zelda - Secret Sound
const Note zeldaMelody[] = {
  {NOTE_G5, 100}, {NOTE_FS5, 100}, {NOTE_DS5, 100}, {NOTE_A4, 100},
  {NOTE_GS4, 100}, {NOTE_E5, 100}, {NOTE_GS5, 100}, {NOTE_C6, 100}
};
const int zeldaMelodyLength = 8;

// 2 - Tetris Theme (Korobeiniki)
const Note tetrisMelody[] = {
  {NOTE_E5, 400}, {NOTE_B4, 200}, {NOTE_C5, 200}, {NOTE_D5, 400},
  {NOTE_C5, 200}, {NOTE_B4, 200}, {NOTE_A4, 400}, {NOTE_A4, 200},
  {NOTE_C5, 200}, {NOTE_E5, 400}, {NOTE_D5, 200}, {NOTE_C5, 200},
  {NOTE_B4, 600}, {NOTE_C5, 200}, {NOTE_D5, 400}, {NOTE_E5, 400}
};
const int tetrisMelodyLength = 16;

// 3 - Nokia Ringtone
const Note nokiaMelody[] = {
  {NOTE_E5, 125}, {NOTE_D5, 125}, {NOTE_FS4, 250}, {NOTE_GS4, 250},
  {NOTE_CS5, 125}, {NOTE_B4, 125}, {NOTE_D4, 250}, {NOTE_E4, 250},
  {NOTE_B4, 125}, {NOTE_A4, 125}, {NOTE_CS4, 250}, {NOTE_E4, 250},
  {NOTE_A4, 500}
};
const int nokiaMelodyLength = 13;

// 4 - Pokémon Theme
const Note pokemonMelody[] = {
  {NOTE_E5, 200}, {NOTE_G5, 200}, {NOTE_A5, 200}, {NOTE_G5, 100},
  {NOTE_A5, 100}, {NOTE_B5, 200}, {NOTE_A5, 200}, {NOTE_G5, 200},
  {NOTE_E5, 200}, {NOTE_G5, 200}, {NOTE_A5, 200}, {NOTE_G5, 100},
  {NOTE_A5, 100}, {NOTE_B5, 400}
};
const int pokemonMelodyLength = 14;

// 5 - Star Wars Imperial March
const Note starWarsMelody[] = {
  {NOTE_G4, 500}, {NOTE_G4, 500}, {NOTE_G4, 500}, {NOTE_DS4, 350},
  {NOTE_AS4, 150}, {NOTE_G4, 500}, {NOTE_DS4, 350}, {NOTE_AS4, 150},
  {NOTE_G4, 1000}
};
const int starWarsMelodyLength = 9;

// 6 - Harry Potter - Hedwig's Theme
const Note harryPotterMelody[] = {
  {NOTE_B4, 300}, {NOTE_E5, 600}, {NOTE_G5, 400}, {NOTE_FS5, 200},
  {NOTE_E5, 600}, {NOTE_B5, 300}, {NOTE_A5, 600}, {NOTE_FS5, 900}
};
const int harryPotterMelodyLength = 8;

// 7 - Classic Alarm Clock
const Note classicAlarmMelody[] = {
  {NOTE_C5, 100}, {NOTE_G5, 100}, {NOTE_C5, 100}, {NOTE_G5, 100},
  {NOTE_C5, 100}, {NOTE_G5, 100}, {NOTE_C5, 100}, {NOTE_G5, 100},
  {NOTE_C5, 100}, {NOTE_G5, 100}, {NOTE_C5, 100}, {NOTE_G5, 100}
};
const int classicAlarmMelodyLength = 12;

// 8 - Simple Beep Pattern
const Note beepMelody[] = {
  {1000, 200}, {0, 100}, {1000, 200}, {0, 100},
  {1000, 200}, {0, 100}, {1000, 200}
};
const int beepMelodyLength = 7;

// 9 - Star Trek (The Original Series Theme)
const Note startrekMelody[] = {
  {NOTE_G4, 400}, {NOTE_C5, 300}, {NOTE_F5, 600}, {NOTE_E5, 250},
  {NOTE_C5, 400}, {NOTE_A4, 400}, {NOTE_D5, 400}, {NOTE_G5, 500},
  {NOTE_G5, 250}, {NOTE_B5, 1000}
};
const int startrekMelodyLength = 10;

// 10 - Back to the Future Main Theme
const Note backToTheFutureMelody[] = {
  {NOTE_G4, 300}, {NOTE_C5, 300}, {NOTE_G5, 200}, {NOTE_F5, 500},
  {NOTE_E5, 200}, {NOTE_D5, 100}, {NOTE_E5, 400}, {NOTE_D5, 400},
  {NOTE_C5, 400}, {NOTE_D5, 600}
};
const int backToTheFutureMelodyLength = 10;

// 11 - Indiana Jones Raiders March (Adventure Theme)
const Note indianaJonesMelody[] = {
  {NOTE_E4, 150}, {NOTE_F4, 150}, {NOTE_G4, 150}, {0, 100},
  {NOTE_C5, 600}, {0, 150},
  {NOTE_D4, 150}, {NOTE_E4, 150}, {NOTE_F4, 600}, {0, 150},
  {NOTE_G4, 150}, {NOTE_A4, 150}, {NOTE_B4, 150}, {0, 100},
  {NOTE_F5, 600}, {0, 150},
  {NOTE_A4, 150}, {NOTE_B4, 150}, {NOTE_C5, 600}, {0, 150},
  {NOTE_D5, 300}, {NOTE_E5, 900}//, //{NOTE_F5, 900}
};
const int indianaJonesMelodyLength = 22;

// 12 - Pac-Man Beginning (Melodia di inizio gioco originale arcade)
// Fonte: https://github.com/robsoncouto/arduino-songs - Tempo 105 BPM
const Note pacmanBeginningMelody[] = {
  {NOTE_B4, 143}, {NOTE_B5, 143}, {NOTE_FS5, 143}, {NOTE_DS5, 143},
  {NOTE_B5, 71}, {NOTE_FS5, 214}, {NOTE_DS5, 286},
  {NOTE_C5, 143}, {NOTE_C6, 143}, {NOTE_G6, 143}, {NOTE_E6, 143},
  {NOTE_C6, 71}, {NOTE_G6, 214}, {NOTE_E6, 286},
  {NOTE_B4, 143}, {NOTE_B5, 143}, {NOTE_FS5, 143}, {NOTE_DS5, 143},
  {NOTE_B5, 71}, {NOTE_FS5, 214}, {NOTE_DS5, 286},
  {NOTE_DS5, 71}, {NOTE_E5, 71}, {NOTE_F5, 71},
  {NOTE_F5, 71}, {NOTE_FS5, 71}, {NOTE_G5, 71},
  {NOTE_G5, 71}, {NOTE_GS5, 71}, {NOTE_A5, 143}, {NOTE_B5, 286}
};
const int pacmanBeginningMelodyLength = 31;

// ============================================
// FUNZIONI SVEGLIA
// ============================================

// Funzione generica per suonare una melodia
void playMelody(const Note* melody, int length) {
  if (!soundEnabled) return;

  for (int i = 0; i < length; i++) {
    if (melody[i].frequency == 0) {
      // Pausa (silenzio)
      noTone(BUZZER_PIN);
      delay(melody[i].duration);
    } else {
      tone(BUZZER_PIN, melody[i].frequency, melody[i].duration);
      delay(melody[i].duration);
    }
    noTone(BUZZER_PIN);
    delay(20); // Piccola pausa tra le note per separazione
  }
}

// Funzione per suonare la sveglia in base alla suoneria selezionata
void playAlarmRingtone(uint8_t ringtoneIndex) {
  if (!soundEnabled) return;

  switch (ringtoneIndex) {
    case RINGTONE_MARIO:
      playMelody(marioMelody, marioMelodyLength);
      break;
    case RINGTONE_ZELDA:
      playMelody(zeldaMelody, zeldaMelodyLength);
      break;
    case RINGTONE_TETRIS:
      playMelody(tetrisMelody, tetrisMelodyLength);
      break;
    case RINGTONE_NOKIA:
      playMelody(nokiaMelody, nokiaMelodyLength);
      break;
    case RINGTONE_POKEMON:
      playMelody(pokemonMelody, pokemonMelodyLength);
      break;
    case RINGTONE_STARWARS:
      playMelody(starWarsMelody, starWarsMelodyLength);
      break;
    case RINGTONE_HARRYPOTTER:
      playMelody(harryPotterMelody, harryPotterMelodyLength);
      break;
    case RINGTONE_CLASSIC:
      playMelody(classicAlarmMelody, classicAlarmMelodyLength);
      break;
    case RINGTONE_BEEP:
      playMelody(beepMelody, beepMelodyLength);
      break;
    case RINGTONE_STARTREK:
      playMelody(startrekMelody, startrekMelodyLength);
      break;
    case RINGTONE_BACKTOTHEFUTURE:
      playMelody(backToTheFutureMelody, backToTheFutureMelodyLength);
      break;
    case RINGTONE_INDIANAJONES:
      playMelody(indianaJonesMelody, indianaJonesMelodyLength);
      break;
    default:
      playBeep(); // Fallback se indice non valido
      break;
  }
}

// ============================================
// SISTEMA RIPRODUZIONE NON BLOCCANTE (per allarme)
// ============================================

// Inizia la riproduzione di una melodia (non bloccante)
void startMelody(const Note* melody, int length) {
  if (!soundEnabled || melody == NULL || length == 0) return;

  currentMelody = melody;
  currentMelodyLength = length;
  currentNoteIndex = 0;
  noteStartTime = millis();
  isPlayingMelody = true;

  // Suona la prima nota e aggiorna stato per sincronizzazione lampeggio
  if (currentMelody[0].frequency == 0) {
    noTone(BUZZER_PIN);
    currentNoteActive = false;  // Pausa - lampeggio spento
  } else {
    tone(BUZZER_PIN, currentMelody[0].frequency);
    currentNoteActive = true;   // Nota attiva - lampeggio acceso
  }
}

// Aggiorna la riproduzione (chiamare nel loop)
void updateMelody() {
  if (!isPlayingMelody || currentMelody == NULL) return;

  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - noteStartTime;

  // Controlla se è ora di passare alla nota successiva
  if (elapsed >= currentMelody[currentNoteIndex].duration + 20) { // +20ms pausa tra note
    currentNoteIndex++;

    // Controlla se la melodia è finita
    if (currentNoteIndex >= currentMelodyLength) {
      noTone(BUZZER_PIN);
      isPlayingMelody = false;
      currentMelody = NULL;
      currentNoteActive = false;  // Melodia finita - lampeggio spento
      lastMelodyEndTime = millis(); // Salva quando la melodia finisce
      return;
    }

    // Suona la nota successiva e aggiorna stato per sincronizzazione lampeggio
    noteStartTime = currentTime;
    if (currentMelody[currentNoteIndex].frequency == 0) {
      noTone(BUZZER_PIN);
      currentNoteActive = false;  // Pausa - lampeggio spento
    } else {
      tone(BUZZER_PIN, currentMelody[currentNoteIndex].frequency);
      currentNoteActive = true;   // Nota attiva - lampeggio acceso
    }
  }
}

// Ferma la riproduzione della melodia
void stopMelody() {
  isPlayingMelody = false;
  currentMelody = NULL;
  currentNoteActive = false;  // Reset lampeggio
  noTone(BUZZER_PIN);
  lastMelodyEndTime = millis(); // Salva quando la melodia viene fermata
}

// Suona la melodia di inizio Pac-Man (bloccante, ma breve)
void playPacmanBeginningMelody() {
  if (!soundEnabled) return;
  playMelody(pacmanBeginningMelody, pacmanBeginningMelodyLength);
}

// Funzione per controllare se la sveglia deve suonare
void checkAlarm() {
  if (!alarmEnabled) return;

  // Ottieni ora corrente
  int currentHour = myTZ.hour();
  int currentMinute = myTZ.minute();
  int currentDay = myTZ.weekday(); // 0=Dom, 1=Lun, 2=Mar, 3=Mer, 4=Gio, 5=Ven, 6=Sab

  // Reset flag "già suonata oggi" a mezzanotte
  if (currentDay != lastCheckedDay) {
    alarmTriggeredToday = false;
    lastCheckedDay = currentDay;
  }

  // Reset flag quando usciamo dal minuto della sveglia (FIX IMPORTANTE!)
  if (currentHour != alarmHour || currentMinute != alarmMinute) {
    alarmTriggeredToday = false;
  }

  // Se la sveglia sta già suonando, controlla se deve fermarsi
  if (alarmRinging) {
    unsigned long elapsed = (millis() - alarmRingingStartTime) / 1000; // secondi
    if (elapsed >= alarmDuration) {
      // Tempo scaduto, segnala che non deve riavviare la melodia
      if (!alarmStopRequested) {
        alarmStopRequested = true;
      }
      // Ferma solo quando la melodia corrente è COMPLETAMENTE finita
      // Questo sincronizza la fine del lampeggio con la fine della suoneria
      if (!isPlayingMelody) {
        alarmRinging = false;
        alarmStopRequested = false;
        noTone(BUZZER_PIN);
        alarmTriggeredToday = true; // Segna come già suonata oggi

        // Forza ridisegno immediato dell'orologio senza lampeggio
        forceRedraw = true;
        clearMatrix();
      }
    }
    return; // Non controllare altre condizioni mentre sta suonando
  }

  // Controlla se è l'ora della sveglia
  if (currentHour == alarmHour && currentMinute == alarmMinute) {
    // Evita attivazioni multiple nello stesso minuto
    if (alarmTriggeredToday) return;

    // Converti weekday da ezTime (0=Dom, 1=Lun...) al nostro formato bitmap (bit0=Lun...)
    // ezTime: 0=Dom, 1=Lun, 2=Mar, 3=Mer, 4=Gio, 5=Ven, 6=Sab
    // Bitmap: bit0=Lun, bit1=Mar, bit2=Mer, bit3=Gio, bit4=Ven, bit5=Sab, bit6=Dom
    uint8_t dayBit;
    if (currentDay == 0) {
      dayBit = 6; // Domenica -> bit 6
    } else {
      dayBit = currentDay - 1; // Lun=0, Mar=1, etc.
    }

    // Controlla se questo giorno è abilitato
    if (alarmDays & (1 << dayBit)) {
      // Attiva la sveglia
      alarmRinging = true;
      alarmRingingStartTime = millis();
      alarmDisplayStarted = false;  // La suoneria partirà solo dopo il primo lampeggio rosso
      alarmStopRequested = false;   // Reset flag sincronizzazione per nuova sveglia
      // Passa immediatamente alla visualizzazione orologio
      changeState(STATE_GAME_CLOCK);
      // Non segnare come triggered ancora, lo farà quando finisce
    }
  }
}

// Helper per ottenere melodia e lunghezza dall'indice
void getMelodyByIndex(uint8_t index, const Note** melody, int* length) {
  switch (index) {
    case RINGTONE_MARIO: *melody = marioMelody; *length = marioMelodyLength; break;
    case RINGTONE_ZELDA: *melody = zeldaMelody; *length = zeldaMelodyLength; break;
    case RINGTONE_TETRIS: *melody = tetrisMelody; *length = tetrisMelodyLength; break;
    case RINGTONE_NOKIA: *melody = nokiaMelody; *length = nokiaMelodyLength; break;
    case RINGTONE_POKEMON: *melody = pokemonMelody; *length = pokemonMelodyLength; break;
    case RINGTONE_STARWARS: *melody = starWarsMelody; *length = starWarsMelodyLength; break;
    case RINGTONE_HARRYPOTTER: *melody = harryPotterMelody; *length = harryPotterMelodyLength; break;
    case RINGTONE_CLASSIC: *melody = classicAlarmMelody; *length = classicAlarmMelodyLength; break;
    case RINGTONE_BEEP: *melody = beepMelody; *length = beepMelodyLength; break;
    case RINGTONE_STARTREK: *melody = startrekMelody; *length = startrekMelodyLength; break;
    case RINGTONE_BACKTOTHEFUTURE: *melody = backToTheFutureMelody; *length = backToTheFutureMelodyLength; break;
    case RINGTONE_INDIANAJONES: *melody = indianaJonesMelody; *length = indianaJonesMelodyLength; break;
    default: *melody = beepMelody; *length = beepMelodyLength; break;
  }
}

// Funzione per gestire la riproduzione ciclica della suoneria (NON BLOCCANTE)
void handleAlarmRinging() {
  static bool wasRinging = false;

  // Reset quando la sveglia si ferma
  if (!alarmRinging) {
    if (wasRinging) {
      stopMelody();
      wasRinging = false;
      alarmStopRequested = false;  // Reset flag sincronizzazione
    }
    return;
  }

  // Aspetta che il display rosso sia stato mostrato prima di avviare la suoneria
  if (!alarmDisplayStarted) {
    return;
  }

  // Se la sveglia è appena iniziata, avvia la melodia
  if (!wasRinging) {
    const Note* melody;
    int length;
    getMelodyByIndex(alarmRingtone, &melody, &length);
    startMelody(melody, length);
    lastMelodyStartTime = millis();
    wasRinging = true;
    return;
  }

  // Se la melodia è finita, riavviala dopo una pausa (solo se non richiesto stop)
  if (!isPlayingMelody && !alarmStopRequested) {
    unsigned long currentTime = millis();
    const unsigned long pauseBetweenMelodies = 1000; // 1 secondo di pausa

    // Aspetta 1 secondo dalla FINE della melodia (non dall'inizio)
    if (currentTime - lastMelodyEndTime >= pauseBetweenMelodies) {
      const Note* melody;
      int length;
      getMelodyByIndex(alarmRingtone, &melody, &length);
      startMelody(melody, length);
      lastMelodyStartTime = currentTime;
    }
  }
}

void checkButtons() {
  static bool modePressed = false;
  static bool secPressed = false;
  static bool bothPressed = false;
  static unsigned long modePressTime = 0;
  static unsigned long secPressTime = 0;
  static unsigned long bothPressTime = 0;
  static bool wifiResetTriggered = false;

  bool modeButtonDown = (digitalRead(BUTTON_MODE) == LOW);
  bool secButtonDown = (digitalRead(BUTTON_SEC) == LOW);

  // PRIORITÀ 1: Controlla se ENTRAMBI i pulsanti sono premuti (WiFi Reset)
  if (modeButtonDown && secButtonDown) {
    if (!bothPressed) {
      bothPressed = true;
      bothPressTime = millis();
      wifiResetTriggered = false;
      Serial.println("Both buttons pressed - Hold for 5 seconds to reset WiFi");
    } else if (!wifiResetTriggered && (millis() - bothPressTime > 5000)) {
      // Reset WiFi dopo 5 secondi
      Serial.println("WiFi RESET triggered - Both buttons held for 5 seconds");
      wifiResetTriggered = true;

      // Feedback visivo: lampeggio rosso veloce
      for (int i = 0; i < 5; i++) {
        for (int j = 0; j < NUM_LEDS; j++) {
          leds[j] = CRGB(255, 0, 0);
        }
        FastLED.show();
        delay(100);
        clearMatrix();
        delay(100);
      }

      // Esegue il reset WiFi (preserva le altre impostazioni)
      WiFiConfig config;
      EEPROM.get(0, config);

      // Cancella solo le credenziali WiFi, preserva tutto il resto
      memset(config.ssid, 0, sizeof(config.ssid));
      memset(config.password, 0, sizeof(config.password));

      EEPROM.put(0, config);
      EEPROM.commit();
      Serial.println("WiFi credentials cleared (settings preserved) - Restarting...");
      ESP.restart();
    }
    return; // Non processare i singoli pulsanti se entrambi sono premuti
  } else {
    // Reset stato pressione contemporanea
    if (bothPressed) {
      bothPressed = false;
      if (!wifiResetTriggered) {
        Serial.println("Both buttons released before 5 seconds");
      }
    }
  }

  // PRIORITÀ 2: Gestione pulsante MODE (solo se SEC non è premuto)
  if (modeButtonDown && !secButtonDown) {
    if (!modePressed) {
      modePressed = true;
      modePressTime = millis();
    }
  } else {
    if (modePressed && !secButtonDown && millis() - modePressTime < 500) {
      // Pressione breve MODE

      // Se in modalità Bluetooth, torna a WiFi
      if (bluetoothMode) {
        Serial.println("MODE pressed in Bluetooth mode - Returning to WiFi");
        disableBluetoothMode();
        modePressed = false;
        return;
      }

      // Comportamento normale: vai all'orologio o cambia tipo
      if (currentState != STATE_GAME_CLOCK) {
        // Se NON sei già sull'orologio → vai all'orologio (senza cambiare tipo)
        changeState(STATE_GAME_CLOCK);
        Serial.println("MODE pressed - Switching to CLOCK");
      } else {
        // Se sei già sull'orologio → cambia tipo di orologio
        clockDisplayType++;
        if (clockDisplayType > 7) {
          clockDisplayType = 0; // Cicla: 0=classico, 1=compatto, 2=grande, 3=binario, 4=analogico, 5=verticale, 6=scorrevole, 7=compatto+giorno
        }
        Serial.print("Clock display type changed to: ");
        Serial.println(clockDisplayType);

        // Salva configurazione in EEPROM
        saveConfig();
      }

      // Forza il ridisegno
      forceRedraw = true;

      // Reset sequenza alternanze per mostrare subito l'orologio
      sequenceIndex = 0;
      currentDisplayMode = 0;
      lastClockWeatherSwitch = millis();

      lastInputTime = millis();
    }
    modePressed = false;
  }

  // PRIORITÀ 3: Gestione pulsante SEC (solo se MODE non è premuto)
  if (secButtonDown && !modeButtonDown) {
    if (!secPressed) {
      secPressed = true;
      secPressTime = millis();
    } else {
      // Pulsante tenuto premuto: regola luminosità gradualmente
      unsigned long holdTime = millis() - secPressTime;
      if (holdTime > 100) { // Aggiorna ogni 100ms per fluidità
        // Incremento lineare: da 1 a 128
        currentBrightness++;
        if (currentBrightness > 128) {
          currentBrightness = 1; // Ricomincia dal minimo
        }

        FastLED.setBrightness(currentBrightness);
        FastLED.show();
        delayMicroseconds(300);  // Stabilizza per evitare artefatti
        FastLED.show();

        secPressTime = millis(); // Reset timer per prossimo incremento

        Serial.print("Brightness: ");
        Serial.println(currentBrightness);
      }
    }
  } else {
    if (secPressed && !modeButtonDown) {
      // Pulsante rilasciato: salva configurazione in EEPROM
      saveConfig();

      Serial.print("Brightness saved to: ");
      Serial.println(currentBrightness);

      lastInputTime = millis();
    }
    secPressed = false;
  }
}

// ============================================
// SEGNAPUNTI - SCOREBOARD
// ============================================
void handleScoreboard() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Segnapunti 4 Giocatori</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;padding:20px;min-height:100vh;}";
  html += ".container{max-width:900px;margin:0 auto;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{font-size:2.5em;margin-bottom:40px;text-shadow:0 0 20px rgba(255,255,255,0.3);}";
  html += ".scoreboard{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin:40px 0;}";
  html += ".player-card{background:linear-gradient(135deg,rgba(26,26,46,0.9),rgba(22,33,62,0.9));padding:30px;border-radius:20px;border:3px solid;}";
  html += ".player-card.p1{border-color:#FF0000;}";
  html += ".player-card.p2{border-color:#0000FF;}";
  html += ".player-card.p3{border-color:#FFFF00;}";
  html += ".player-card.p4{border-color:#FFFFFF;}";
  html += ".player-name{font-size:1.3em;margin-bottom:15px;color:#aaa;}";
  html += ".player-name input{background:transparent;border:none;border-bottom:2px solid #666;color:#fff;font-size:0.9em;text-align:center;width:80%;padding:8px;}";
  html += ".score{font-size:3.5em;font-weight:bold;margin:20px 0;text-shadow:0 0 30px currentColor;}";
  html += ".player-card.p1 .score{color:#FF0000;}";
  html += ".player-card.p2 .score{color:#0000FF;}";
  html += ".player-card.p3 .score{color:#FFFF00;}";
  html += ".player-card.p4 .score{color:#FFFFFF;}";
  html += ".controls{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;}";
  html += ".btn{padding:10px 20px;font-size:1em;border:none;border-radius:8px;cursor:pointer;transition:0.3s;font-weight:bold;}";
  html += ".btn-add{background:#4CAF50;color:white;}";
  html += ".btn-add:hover{background:#45a049;transform:scale(1.05);}";
  html += ".btn-sub{background:#f44336;color:white;}";
  html += ".btn-sub:hover{background:#da190b;transform:scale(1.05);}";
  html += ".btn-reset{background:#FF9800;color:white;padding:15px 30px;margin-top:30px;}";
  html += ".btn-reset:hover{background:#e68900;transform:scale(1.05);}";
  html += ".btn-apply{background:#2196F3;color:white;margin-top:20px;}";
  html += ".btn-apply:hover{background:#0b7dda;transform:scale(1.05);}";
  html += "@media(max-width:768px){.scoreboard{grid-template-columns:1fr;}}";
  html += "</style>";
  html += "<script>";
  html += "let p1Score=" + String(scorePlayer1) + ";";
  html += "let p2Score=" + String(scorePlayer2) + ";";
  html += "let p3Score=" + String(scorePlayer3) + ";";
  html += "let p4Score=" + String(scorePlayer4) + ";";
  html += "function updateScore(player,delta){";
  html += "if(player==1){p1Score+=delta;if(p1Score<0)p1Score=0;document.getElementById('score1').innerText=p1Score;}";
  html += "else if(player==2){p2Score+=delta;if(p2Score<0)p2Score=0;document.getElementById('score2').innerText=p2Score;}";
  html += "else if(player==3){p3Score+=delta;if(p3Score<0)p3Score=0;document.getElementById('score3').innerText=p3Score;}";
  html += "else if(player==4){p4Score+=delta;if(p4Score<0)p4Score=0;document.getElementById('score4').innerText=p4Score;}";
  html += "fetch('/scoreboardcontrol?p1='+p1Score+'&p2='+p2Score+'&p3='+p3Score+'&p4='+p4Score);";
  html += "}";
  html += "function resetScores(){";
  html += "p1Score=0;p2Score=0;p3Score=0;p4Score=0;";
  html += "document.getElementById('score1').innerText=0;";
  html += "document.getElementById('score2').innerText=0;";
  html += "document.getElementById('score3').innerText=0;";
  html += "document.getElementById('score4').innerText=0;";
  html += "fetch('/scoreboardcontrol?p1=0&p2=0&p3=0&p4=0');";
  html += "}";
  html += "function applyNames(){";
  html += "let name1=document.getElementById('name1').value;";
  html += "let name2=document.getElementById('name2').value;";
  html += "let name3=document.getElementById('name3').value;";
  html += "let name4=document.getElementById('name4').value;";
  html += "fetch('/scoreboardcontrol?name1='+encodeURIComponent(name1)+'&name2='+encodeURIComponent(name2)+'&name3='+encodeURIComponent(name3)+'&name4='+encodeURIComponent(name4));";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>🏆 SEGNAPUNTI 4 GIOCATORI</h1>";
  html += "<div class='scoreboard'>";

  // Player 1 (ROSSO)
  html += "<div class='player-card p1'>";
  html += "<div class='player-name'>🔴 Giocatore 1<br><input type='text' id='name1' value='" + player1Name + "' maxlength='10'></div>";
  html += "<div class='score' id='score1'>" + String(scorePlayer1) + "</div>";
  html += "<div class='controls'>";
  html += "<button class='btn btn-add' onclick='updateScore(1,1)'>+1</button>";
  html += "<button class='btn btn-add' onclick='updateScore(1,5)'>+5</button>";
  html += "<button class='btn btn-sub' onclick='updateScore(1,-1)'>-1</button>";
  html += "</div>";
  html += "</div>";

  // Player 2 (BLU)
  html += "<div class='player-card p2'>";
  html += "<div class='player-name'>🔵 Giocatore 2<br><input type='text' id='name2' value='" + player2Name + "' maxlength='10'></div>";
  html += "<div class='score' id='score2'>" + String(scorePlayer2) + "</div>";
  html += "<div class='controls'>";
  html += "<button class='btn btn-add' onclick='updateScore(2,1)'>+1</button>";
  html += "<button class='btn btn-add' onclick='updateScore(2,5)'>+5</button>";
  html += "<button class='btn btn-sub' onclick='updateScore(2,-1)'>-1</button>";
  html += "</div>";
  html += "</div>";

  // Player 3 (GIALLO)
  html += "<div class='player-card p3'>";
  html += "<div class='player-name'>🟡 Giocatore 3<br><input type='text' id='name3' value='" + player3Name + "' maxlength='10'></div>";
  html += "<div class='score' id='score3'>" + String(scorePlayer3) + "</div>";
  html += "<div class='controls'>";
  html += "<button class='btn btn-add' onclick='updateScore(3,1)'>+1</button>";
  html += "<button class='btn btn-add' onclick='updateScore(3,5)'>+5</button>";
  html += "<button class='btn btn-sub' onclick='updateScore(3,-1)'>-1</button>";
  html += "</div>";
  html += "</div>";

  // Player 4 (BIANCO)
  html += "<div class='player-card p4'>";
  html += "<div class='player-name'>⚪ Giocatore 4<br><input type='text' id='name4' value='" + player4Name + "' maxlength='10'></div>";
  html += "<div class='score' id='score4'>" + String(scorePlayer4) + "</div>";
  html += "<div class='controls'>";
  html += "<button class='btn btn-add' onclick='updateScore(4,1)'>+1</button>";
  html += "<button class='btn btn-add' onclick='updateScore(4,5)'>+5</button>";
  html += "<button class='btn btn-sub' onclick='updateScore(4,-1)'>-1</button>";
  html += "</div>";
  html += "</div>";

  html += "</div>";
  html += "<button class='btn btn-apply' onclick='applyNames()'>✓ Applica Nomi</button>";
  html += "<br><button class='btn btn-reset' onclick='resetScores()'>🔄 Reset Punteggi</button>";
  html += "<p style='margin-top:40px;color:#aaa;'>I punteggi vengono visualizzati in tempo reale sulla matrice LED 16x16</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
  changeState(STATE_GAME_SCOREBOARD);
}

void handleScoreboardControl() {
  if (server.hasArg("p1") && server.hasArg("p2") && server.hasArg("p3") && server.hasArg("p4")) {
    int newP1 = server.arg("p1").toInt();
    int newP2 = server.arg("p2").toInt();
    int newP3 = server.arg("p3").toInt();
    int newP4 = server.arg("p4").toInt();

    // Suono quando si resetta (tutti a 0)
    if (newP1 == 0 && newP2 == 0 && newP3 == 0 && newP4 == 0 &&
        (scorePlayer1 != 0 || scorePlayer2 != 0 || scorePlayer3 != 0 || scorePlayer4 != 0)) {
      playBeep(); // Suono reset punteggi
    }
    // Suono quando si modifica il punteggio
    else if (newP1 != scorePlayer1 || newP2 != scorePlayer2 || newP3 != scorePlayer3 || newP4 != scorePlayer4) {
      playBeep(); // Suono aggiornamento punteggio
    }

    scorePlayer1 = newP1;
    scorePlayer2 = newP2;
    scorePlayer3 = newP3;
    scorePlayer4 = newP4;
  }

  if (server.hasArg("name1")) {
    player1Name = server.arg("name1");
    if (player1Name.length() > 10) player1Name = player1Name.substring(0, 10);
  }

  if (server.hasArg("name2")) {
    player2Name = server.arg("name2");
    if (player2Name.length() > 10) player2Name = player2Name.substring(0, 10);
  }

  if (server.hasArg("name3")) {
    player3Name = server.arg("name3");
    if (player3Name.length() > 10) player3Name = player3Name.substring(0, 10);
  }

  if (server.hasArg("name4")) {
    player4Name = server.arg("name4");
    if (player4Name.length() > 10) player4Name = player4Name.substring(0, 10);
  }

  server.send(200, "text/plain", "OK");
}

void drawScoreboard() {
  clearMatrixNoShow();

  // SEGNAPUNTI 4 GIOCATORI CON CROCE VERDE CENTRALE
  // Layout matrice 16x16:
  // - Player 1 (ROSSO) in alto a sinistra
  // - Player 2 (BLU) in alto a destra
  // - Croce verde al centro (linea orizzontale + verticale)
  // - Player 3 (GIALLO) in basso a sinistra
  // - Player 4 (BIANCO) in basso a destra

  // Riga verde orizzontale al centro (2 pixel di altezza)
  for (int x = 0; x < 16; x++) {
    setPixel(x, 7, CRGB(0, 100, 0)); // Riga centrale superiore
    setPixel(x, 8, CRGB(0, 100, 0)); // Riga centrale inferiore
  }

  // Linea verde verticale al centro (2 pixel di larghezza)
  for (int y = 0; y < 16; y++) {
    setPixel(7, y, CRGB(0, 100, 0)); // Colonna centrale sinistra
    setPixel(8, y, CRGB(0, 100, 0)); // Colonna centrale destra
  }

  // ===== PLAYER 1 (ROSSO) - Alto Sinistra (SPOSTATO -1 A SINISTRA) =====
  String p1Str = String(scorePlayer1);
  int p1Width = p1Str.length() * 4 - 1;
  int p1X = (8 - p1Width) / 2 - 1; // Centra nella metà sinistra (0-7) - offset 1
  if (p1X < 0) p1X = 0;
  for (unsigned int i = 0; i < p1Str.length() && i < 2; i++) { // Max 2 cifre
    drawCharacter(p1Str[i], p1X + i * 4, 1, CRGB(255, 0, 0)); // ROSSO
  }

  // ===== PLAYER 2 (BLU) - Alto Destra (SPOSTATO +1 A DESTRA) =====
  String p2Str = String(scorePlayer2);
  int p2Width = p2Str.length() * 4 - 1;
  int p2X = 8 + (8 - p2Width) / 2 + 1; // Centra nella metà destra (8-15) + 1 a destra
  if (p2X > 12) p2X = 12;
  for (unsigned int i = 0; i < p2Str.length() && i < 2; i++) { // Max 2 cifre
    drawCharacter(p2Str[i], p2X + i * 4, 1, CRGB(0, 0, 255)); // BLU
  }

  // ===== PLAYER 3 (GIALLO) - Basso Sinistra (SPOSTATO -1 A SINISTRA) =====
  String p3Str = String(scorePlayer3);
  int p3Width = p3Str.length() * 4 - 1;
  int p3X = (8 - p3Width) / 2 - 1; // Centra nella metà sinistra (0-7) - offset 1
  if (p3X < 0) p3X = 0;
  for (unsigned int i = 0; i < p3Str.length() && i < 2; i++) { // Max 2 cifre
    drawCharacter(p3Str[i], p3X + i * 4, 10, CRGB(255, 255, 0)); // GIALLO
  }

  // ===== PLAYER 4 (BIANCO) - Basso Destra (SPOSTATO +1 A DESTRA) =====
  String p4Str = String(scorePlayer4);
  int p4Width = p4Str.length() * 4 - 1;
  int p4X = 8 + (8 - p4Width) / 2 + 1; // Centra nella metà destra (8-15) + 1 a destra
  if (p4X > 12) p4X = 12;
  for (unsigned int i = 0; i < p4Str.length() && i < 2; i++) { // Max 2 cifre
    drawCharacter(p4Str[i], p4X + i * 4, 10, CRGB(255, 255, 255)); // BIANCO
  }

  FastLED.show();
}

// ============================================
// TETRIS
// ============================================

bool tetrisGetPieceBlock(int type, int rotation, int bx, int by) {
  uint16_t piece = tetrominoes[type][rotation];
  return (piece & (0x8000 >> (by * 4 + bx))) != 0;
}

CRGB tetrisGetPieceColor(int type) {
  switch(type) {
    case 0: return CRGB(0, 255, 255);   // I - Cyan
    case 1: return CRGB(255, 255, 0);   // O - Yellow
    case 2: return CRGB(128, 0, 128);   // T - Purple
    case 3: return CRGB(0, 255, 0);     // S - Green
    case 4: return CRGB(255, 0, 0);     // Z - Red
    case 5: return CRGB(255, 165, 0);   // L - Orange
    case 6: return CRGB(0, 0, 255);     // J - Blue
    default: return CRGB(255, 255, 255);
  }
}

bool tetrisCheckCollision(int type, int rotation, int x, int y) {
  for (int by = 0; by < 4; by++) {
    for (int bx = 0; bx < 4; bx++) {
      if (tetrisGetPieceBlock(type, rotation, bx, by)) {
        int worldX = x + bx;
        int worldY = y + by;

        if (worldX < 0 || worldX >= 14 || worldY >= 16) return true;
        if (worldY >= 0 && tetrisGrid[worldX][worldY] != 0) return true;
      }
    }
  }
  return false;
}

void tetrisLockPiece() {
  for (int by = 0; by < 4; by++) {
    for (int bx = 0; bx < 4; bx++) {
      if (tetrisGetPieceBlock(tetrisPieceType, tetrisPieceRotation, bx, by)) {
        int worldX = tetrisPieceX + bx;
        int worldY = tetrisPieceY + by;
        if (worldX >= 0 && worldX < 14 && worldY >= 0 && worldY < 16) {
          tetrisGrid[worldX][worldY] = tetrisPieceType + 1;
        }
      }
    }
  }
}

int tetrisClearLines() {
  int linesCleared = 0;

  for (int y = 15; y >= 0; y--) {
    bool fullLine = true;
    for (int x = 0; x < 14; x++) {
      if (tetrisGrid[x][y] == 0) {
        fullLine = false;
        break;
      }
    }

    if (fullLine) {
      linesCleared++;
      for (int yy = y; yy > 0; yy--) {
        for (int x = 0; x < 14; x++) {
          tetrisGrid[x][yy] = tetrisGrid[x][yy - 1];
        }
      }
      for (int x = 0; x < 14; x++) {
        tetrisGrid[x][0] = 0;
      }
      y++;
    }
  }

  return linesCleared;
}

void tetrisSpawnNewPiece() {
  tetrisPieceType = tetrisNextPieceType;
  tetrisNextPieceType = random(7);
  tetrisPieceRotation = 0;
  tetrisPieceX = 5;
  tetrisPieceY = 0;

  if (tetrisCheckCollision(tetrisPieceType, tetrisPieceRotation, tetrisPieceX, tetrisPieceY)) {
    tetrisLives--;
    showGameHUD(tetrisScore, tetrisLives, tetrisLevel, true);
    if (tetrisLives <= 0) {
      tetrisGameOver = true;
      tetrisGameActive = false;
      playGameOver(); // Suono game over
    } else {
      playError(); // Suono perdita vita
      for (int x = 0; x < 14; x++) {
        for (int y = 0; y < 16; y++) {
          tetrisGrid[x][y] = 0;
        }
      }
    }
  }
}

void tetrisMovePiece(int dx, int dy) {
  if (!tetrisGameActive) return;

  int newX = tetrisPieceX + dx;
  int newY = tetrisPieceY + dy;

  if (!tetrisCheckCollision(tetrisPieceType, tetrisPieceRotation, newX, newY)) {
    tetrisPieceX = newX;
    tetrisPieceY = newY;
  } else if (dy > 0) {
    tetrisLockPiece();
    int lines = tetrisClearLines();

    if (lines > 0) {
      tetrisLines += lines;
      tetrisScore += lines * 100 * tetrisLevel;
      playSuccess(); // Suono linea completata
      int oldLevel = tetrisLevel;
      tetrisLevel = (tetrisLines / 10) + 1;
      if (tetrisLevel > oldLevel) {
        playLevelUp(); // Suono livello superiore
        showGameHUD(tetrisScore, tetrisLives, tetrisLevel, true);
      }
      tetrisSpeed = max(100, 500 - (tetrisLevel * 50));
    }

    tetrisSpawnNewPiece();
  }
}

void tetrisRotatePiece() {
  if (!tetrisGameActive) return;

  int newRotation = (tetrisPieceRotation + 1) % 4;
  if (!tetrisCheckCollision(tetrisPieceType, newRotation, tetrisPieceX, tetrisPieceY)) {
    tetrisPieceRotation = newRotation;
    playBeep(); // Suono rotazione
  }
}

void resetTetris() {
  for (int x = 0; x < 14; x++) {
    for (int y = 0; y < 16; y++) {
      tetrisGrid[x][y] = 0;
    }
  }

  tetrisScore = 0;
  tetrisLevel = 1;
  tetrisLines = 0;
  tetrisLives = 5;
  tetrisSpeed = 500;
  tetrisGameActive = true;
  tetrisGameOver = false;
  tetrisLastUpdate = millis();

  tetrisNextPieceType = random(7);
  tetrisSpawnNewPiece();
}

void updateTetris() {
  // Se è GAME OVER, mostra il messaggio
  if (tetrisGameOver) {
    drawGameOver();
    return;
  }

  if (!tetrisGameActive) return;

  if (millis() - tetrisLastUpdate >= (unsigned long)tetrisSpeed) {
    tetrisMovePiece(0, 1);
    tetrisLastUpdate = millis();
  }

  drawTetris();
}

void drawTetris() {
  clearMatrixNoShow();

  // Disegna PRIMA le linee verticali bianche di delimitazione
  // Le linee sono a x=0 e x=15, mentre i pezzi vanno da x=1 a x=14
  // Disegnando le linee prima, evitiamo che sovrascrivano i pezzi
  CRGB white = CRGB(255, 255, 255);
  for (int y = 0; y < 16; y++) {
    setPixel(0, y, white);   // Linea sinistra (colonna 0)
    setPixel(15, y, white);  // Linea destra (colonna 15)
  }

  // Disegna griglia con pezzi bloccati (centrata con offset 1)
  for (int x = 0; x < 14; x++) {
    for (int y = 0; y < 16; y++) {
      if (tetrisGrid[x][y] > 0) {
        setPixel(x + 1, y, tetrisGetPieceColor(tetrisGrid[x][y] - 1));
      }
    }
  }

  // Disegna pezzo corrente (centrato con offset 1)
  CRGB color = tetrisGetPieceColor(tetrisPieceType);
  for (int by = 0; by < 4; by++) {
    for (int bx = 0; bx < 4; bx++) {
      if (tetrisGetPieceBlock(tetrisPieceType, tetrisPieceRotation, bx, by)) {
        int worldX = tetrisPieceX + bx;
        int worldY = tetrisPieceY + by;
        if (worldX >= 0 && worldX < 14 && worldY >= 0 && worldY < 16) {
          setPixel(worldX + 1, worldY, color);
        }
      }
    }
  }

  FastLED.show();
}

void handleTetris() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Tetris</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;padding:20px;min-height:100vh;}";
  html += ".container{max-width:600px;margin:0 auto;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{font-size:2.5em;margin-bottom:20px;}";
  html += ".stats{display:grid;grid-template-columns:1fr 1fr 1fr;gap:15px;margin:20px 0;text-align:center;}";
  html += ".stat-box{background:rgba(26,26,46,0.9);padding:15px;border-radius:10px;border:2px solid #4CAF50;}";
  html += ".stat-label{font-size:0.75em;color:#aaa;margin-bottom:5px;white-space:nowrap;overflow:hidden;}";
  html += ".stat-value{font-size:2em;font-weight:bold;color:#4CAF50;}";
  html += ".controls{margin:30px 0;}";
  html += ".dpad{display:grid;grid-template-columns:repeat(3,80px);grid-template-rows:repeat(3,80px);gap:10px;margin:0 auto;width:270px;}";
  html += ".dpad button{font-size:24px;border:none;background:#444;color:white;border-radius:10px;cursor:pointer;transition:0.2s;touch-action:none;-webkit-touch-callout:none;-webkit-user-select:none;user-select:none;}";
  html += ".dpad button:active{background:#666;transform:scale(0.95);}";
  html += ".action-btns{display:flex;gap:20px;justify-content:center;margin-top:30px;}";
  html += ".btn{padding:20px 40px;font-size:1.2em;border:none;border-radius:10px;cursor:pointer;font-weight:bold;transition:0.3s;}";
  html += ".btn-rotate{background:#FF9800;color:white;}";
  html += ".btn-rotate:active{background:#e68900;transform:scale(0.95);}";
  html += ".btn-start{background:#4CAF50;color:white;}";
  html += ".btn-start:active{background:#45a049;}";
  html += ".btn-reset{background:#f44336;color:white;}";
  html += ".btn-reset:active{background:#da190b;}";
  html += "</style>";
  html += "<script>";
  html += "function sendCmd(cmd){fetch('/tetriscontrol?cmd='+cmd);}";
  html += "function startGame(){fetch('/tetriscontrol?cmd=start');updateStats();}";
  html += "function resetGame(){fetch('/tetriscontrol?cmd=reset');setTimeout(updateStats,100);}";
  html += "function updateStats(){";
  html += "fetch('/tetriscontrol?cmd=status').then(r=>r.json()).then(d=>{";
  html += "document.querySelectorAll('.stat-value')[0].innerHTML=d.score;";
  html += "document.querySelectorAll('.stat-value')[1].innerHTML=d.level;";
  html += "document.querySelectorAll('.stat-value')[2].innerHTML='❤️ '+d.lives;";
  html += "let startBtn=document.querySelector('.btn-start');";
  html += "if(startBtn){";
  html += "if(!d.active||d.gameOver){startBtn.innerHTML='▶️ START';}";
  html += "else{startBtn.innerHTML='🔄 RIAVVIA';}";
  html += "}";
  html += "}).catch(e=>console.error('Error:',e));";
  html += "}";
  html += "setInterval(updateStats,1000);";
  html += "let activeKeys={};";
  html += "let softDropInterval=null;";
  html += "function startSoftDrop(){sendCmd('down');if(softDropInterval)clearInterval(softDropInterval);softDropInterval=setInterval(()=>sendCmd('down'),80);}";
  html += "function stopSoftDrop(){if(softDropInterval){clearInterval(softDropInterval);softDropInterval=null;}}";
  html += "document.addEventListener('keydown',function(e){";
  html += "if(e.key=='ArrowLeft'||e.key=='ArrowRight'||e.key=='ArrowDown'||e.key=='ArrowUp'||e.key==' '){e.preventDefault();}";
  html += "if(activeKeys[e.key])return;";
  html += "activeKeys[e.key]=true;";
  html += "if(e.key=='ArrowLeft')sendCmd('left');";
  html += "else if(e.key=='ArrowRight')sendCmd('right');";
  html += "else if(e.key=='ArrowDown')startSoftDrop();";
  html += "else if(e.key=='ArrowUp'||e.key==' ')sendCmd('rotate');";
  html += "});";
  html += "document.addEventListener('keyup',function(e){";
  html += "if(e.key=='ArrowLeft'||e.key=='ArrowRight'||e.key=='ArrowDown'||e.key=='ArrowUp'||e.key==' '){e.preventDefault();}";
  html += "if(e.key=='ArrowDown')stopSoftDrop();";
  html += "delete activeKeys[e.key];";
  html += "});";
  html += "document.addEventListener('touchmove',function(e){if(e.target.tagName=='BUTTON')e.preventDefault();},{passive:false});";
  html += "function enableBT(){window.location='/enableBluetooth?game=tetris';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a> <a href='#' onclick='enableBT()' style='background:#2196F3;margin-left:10px;'>🎮 Bluetooth</a></div>";
  html += "<div class='container'>";
  html += "<h1>🧩 TETRIS</h1>";

  html += "<div class='stats'>";
  html += "<div class='stat-box'><div class='stat-label'>PUNTEGGIO</div><div class='stat-value'>" + String(tetrisScore) + "</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>LIVELLO</div><div class='stat-value'>" + String(tetrisLevel) + "</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>VITE</div><div class='stat-value'>❤️ " + String(tetrisLives) + "</div></div>";
  html += "</div>";

  html += "<div class='controls'>";
  html += "<h3>Controlli (Tastiera o Touch)</h3>";
  html += "<div class='dpad'>";
  html += "<div></div><button ontouchstart='event.preventDefault();sendCmd(\"up\")' onclick='sendCmd(\"up\")'>⬆️</button><div></div>";
  html += "<button ontouchstart='event.preventDefault();sendCmd(\"left\")' onclick='sendCmd(\"left\")'>⬅️</button><button ontouchstart='event.preventDefault();sendCmd(\"rotate\")' onclick='sendCmd(\"rotate\")'>🔄</button><button ontouchstart='event.preventDefault();sendCmd(\"right\")' onclick='sendCmd(\"right\")'>➡️</button>";
  html += "<div></div><button ontouchstart='event.preventDefault();startSoftDrop()' ontouchend='event.preventDefault();stopSoftDrop()' onmousedown='startSoftDrop()' onmouseup='stopSoftDrop()' onmouseleave='stopSoftDrop()'>⬇️</button><div></div>";
  html += "</div>";
  html += "</div>";

  html += "<div class='action-btns'>";
  // Pulsante RUOTA rimosso - si usa solo quello centrale nel D-pad
  // Mostra sempre il pulsante START/RIAVVIA
  if (!tetrisGameActive || tetrisGameOver) {
    html += "<button class='btn btn-start' onclick='startGame()'>▶️ START</button>";
  } else {
    html += "<button class='btn btn-start' onclick='startGame()'>🔄 RIAVVIA</button>";
  }
  html += "<button class='btn btn-reset' onclick='resetGame()'>🔄 RESET</button>";
  html += "</div>";

  html += "<p style='margin-top:30px;color:#aaa;'>Usa le frecce per muovere, Spazio o ⬆️ per ruotare</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
  // Non cambia stato - l'orologio continua a funzionare
  // Lo stato cambia solo quando si preme START
}

void handleTetrisControl() {
  static unsigned long lastRotateTime = 0;  // Debounce per rotazione
  const unsigned long ROTATE_DEBOUNCE_MS = 200;  // 200ms tra rotazioni

  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");

    if (cmd == "status") {
      // Ritorna le statistiche in formato JSON
      String json = "{";
      json += "\"score\":" + String(tetrisScore) + ",";
      json += "\"level\":" + String(tetrisLevel) + ",";
      json += "\"lives\":" + String(tetrisLives) + ",";
      json += "\"active\":" + String(tetrisGameActive ? "true" : "false") + ",";
      json += "\"gameOver\":" + String(tetrisGameOver ? "true" : "false");
      json += "}";
      server.send(200, "application/json", json);
      return;
    } else if (cmd == "left") {
      tetrisMovePiece(-1, 0);
    } else if (cmd == "right") {
      tetrisMovePiece(1, 0);
    } else if (cmd == "down") {
      tetrisMovePiece(0, 1);
    } else if (cmd == "up" || cmd == "rotate") {
      // Rotazione con debounce - una sola rotazione per pressione
      unsigned long now = millis();
      if (now - lastRotateTime > ROTATE_DEBOUNCE_MS) {
        lastRotateTime = now;
        tetrisRotatePiece();
      }
    } else if (cmd == "start") {
      resetTetris();
      changeState(STATE_GAME_TETRIS); // Cambia stato solo quando si preme START
    } else if (cmd == "reset") {
      resetTetris();
    }
  }

  server.send(200, "text/plain", "OK");
}

// ============================================
// PAC-MAN
// ============================================

void initPacmanMaze() {
  // Crea un labirinto semplificato per 16x16
  // 0=vuoto, 1=muro, 2=dot, 3=power pellet

  byte maze[PACMAN_MAZE_HEIGHT][PACMAN_MAZE_WIDTH] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,1,2,2,1,1,2,1,1,2,1},
    {1,3,1,1,2,1,1,2,2,1,1,2,1,1,3,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,2,1,1,2,1,2,1,1,2,1},
    {1,2,2,2,2,1,2,0,0,2,1,2,2,2,2,1},
    {1,1,1,1,2,1,0,0,0,0,1,2,1,1,1,1},
    {1,1,1,1,2,1,0,0,0,0,1,2,1,1,1,1},
    {1,2,2,2,2,1,2,2,2,2,1,2,2,2,2,1},
    {1,2,1,1,2,2,2,1,1,2,2,2,1,1,2,1},
    {1,3,2,1,2,1,2,2,2,2,1,2,1,2,3,1},
    {1,1,2,1,2,1,2,1,1,2,1,2,1,2,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,1,1,1,1,1,1,1,1,2,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
  };

  // Copia il labirinto
  pacmanTotalDots = 0;
  for (int y = 0; y < PACMAN_MAZE_HEIGHT; y++) {
    for (int x = 0; x < PACMAN_MAZE_WIDTH; x++) {
      pacmanMaze[y][x] = maze[y][x];
      pacmanMazeOriginal[y][x] = maze[y][x];
      if (maze[y][x] == 2 || maze[y][x] == 3) {
        pacmanTotalDots++;
      }
    }
  }
  pacmanDotsRemaining = pacmanTotalDots;
}

void resetPacman(bool fullReset) {
  if (fullReset) {
    pacmanScore = 0;
    pacmanLives = 5;
    pacmanLevel = 1;
    pacmanSpeed = 200;
    pacmanGhostSpeed = 400;
    pacmanGhostFrightenedSpeed = 500;
  }

  // Reset labirinto
  initPacmanMaze();

  // Reset posizioni
  pacmanResetPositions();

  pacmanGameActive = true;
  pacmanGameOver = false;
  pacmanLevelComplete = false;
  pacmanPowerUpActive = false;
  pacmanLastUpdate = millis();
  pacmanLastGhostUpdate = millis();
  pacmanLastMouthUpdate = millis();

  // Reset flag suono GAME OVER per la prossima partita
  pacmanGameOverSoundPlayed = false;
  pacmanGameOverDisplayed = false;

  // Reset modalità fantasmi (inizia in Scatter Mode come nel gioco originale)
  currentGhostMode = GHOST_MODE_SCATTER;
  lastModeSwitch = millis();
  modePhase = 0;

  // Disegna il labirinto
  drawPacman();
}

void pacmanResetPositions() {
  // Posizione iniziale Pac-Man (riga libera al centro-basso)
  pacmanX = 8;
  pacmanY = 13;
  pacmanDirX = 0;
  pacmanDirY = 0;
  pacmanNextDirX = 0;
  pacmanNextDirY = 0;
  pacmanMouthOpen = 0;

  // Reset timer per ripresa fluida dopo HUD
  pacmanLastUpdate = millis();
  pacmanLastGhostUpdate = millis();
  pacmanLastMouthUpdate = millis();

  // Inizializza 4 fantasmi in posizioni SEPARATE e VISIBILI
  uint32_t ghostColors[] = {0, 1, 2, 3}; // rosso, rosa, ciano, arancione
  // Posizioni ben distanziate nella zona centrale
  int ghostStartPos[][2] = {{6, 7}, {9, 7}, {6, 8}, {9, 8}};

  for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
    pacmanGhosts[i].x = ghostStartPos[i][0];
    pacmanGhosts[i].y = ghostStartPos[i][1];
    pacmanGhosts[i].dirX = 0;
    pacmanGhosts[i].dirY = -1;
    pacmanGhosts[i].color = ghostColors[i];
    pacmanGhosts[i].frightened = false;
    pacmanGhosts[i].eaten = false;

    Serial.print("Fantasma ");
    Serial.print(i);
    Serial.print(" inizializzato in (");
    Serial.print(ghostStartPos[i][0]);
    Serial.print(", ");
    Serial.print(ghostStartPos[i][1]);
    Serial.print(") colore=");
    Serial.println(ghostColors[i]);
  }
}

bool pacmanCanMove(int x, int y, int dx, int dy) {
  int newX = x + dx;
  int newY = y + dy;

  if (newX < 0 || newX >= PACMAN_MAZE_WIDTH || newY < 0 || newY >= PACMAN_MAZE_HEIGHT) {
    return false;
  }

  return pacmanMaze[newY][newX] != 1; // Non può attraversare muri
}

void pacmanChangeDir(int dx, int dy) {
  if (!pacmanGameActive) return;
  if (hudOverlayActive) return;  // Non cambiare direzione durante visualizzazione HUD

  // Salva la prossima direzione desiderata
  pacmanNextDirX = dx;
  pacmanNextDirY = dy;
}

void pacmanEatDot(int x, int y) {
  if (pacmanMaze[y][x] == 2) {
    // Mangia dot normale
    pacmanMaze[y][x] = 0;
    pacmanScore += 1;  // Ridotto da 10 a 1 per mantenere punteggio a 3 cifre
    pacmanDotsRemaining--;
    playEat(); // Suono mangiare dot
  } else if (pacmanMaze[y][x] == 3) {
    // MANGIA POWER PELLET (pillola gialla)
    pacmanMaze[y][x] = 0;
    pacmanScore += 2;  // Ridotto ulteriormente per mantenere punteggio a 3 cifre
    pacmanDotsRemaining--;
    playSuccess(); // Suono power pellet

    // ATTIVA MODALITÀ POWER-UP per 6 secondi
    unsigned long powerUpTime = millis();
    pacmanPowerUpActive = true;
    pacmanPowerUpStartTime = powerUpTime;

    // SPAVENTA TUTTI I 4 FANTASMI - diventano BLU e scappano
    // IMPORTANTE: imposta TUTTI i flag prima di qualsiasi draw
    for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
      pacmanGhosts[i].frightened = true;  // TUTTI diventano frightened
      pacmanGhosts[i].frightenedStartTime = powerUpTime;
      // Se il fantasma era stato mangiato, riattivalo come spaventato
      if (pacmanGhosts[i].eaten) {
        pacmanGhosts[i].eaten = false;
      }
    }

    // Debug: stampa su seriale per verificare
    Serial.println("====================================");
    Serial.println("POWER PELLET MANGIATA!");
    Serial.print("Tempo power-up: ");
    Serial.print(PACMAN_POWERUP_DURATION);
    Serial.println(" ms (6 secondi)");
    Serial.print("pacmanPowerUpActive = ");
    Serial.println(pacmanPowerUpActive ? "TRUE" : "FALSE");
    Serial.print("Fantasmi spaventati (BLU): ");
    for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
      Serial.print(i);
      Serial.print("=");
      Serial.print(pacmanGhosts[i].frightened ? "SI" : "NO");
      Serial.print(" ");
    }
    Serial.println();
    Serial.println("====================================");

    // FORZA ridisegno IMMEDIATO per mostrare fantasmi BLU
    // Usa doppio draw per assicurare che lo stato sia visualizzato correttamente
    drawPacman();
    delay(10);  // Piccolo delay per assicurare sincronizzazione
    drawPacman();
  }

  // Controlla se ha finito il livello
  if (pacmanDotsRemaining <= 0) {
    pacmanLevelComplete = true;
    playLevelUp(); // Suono livello completato
  }
}

void pacmanCollisionCheck() {
  for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
    if (pacmanGhosts[i].x == pacmanX && pacmanGhosts[i].y == pacmanY) {
        if (pacmanGhosts[i].frightened && !pacmanGhosts[i].eaten) {
          // Mangia il fantasma
          pacmanGhosts[i].eaten = true;
          pacmanGhosts[i].frightened = false;
          pacmanScore += 10;  // Ridotto ulteriormente per mantenere punteggio a 3 cifre
          
          // SUONO QUANDO SI MANGIA UN FANTASMA (simile arcade)
          // Sequenza di note ascendenti come nell'originale
          tone(BUZZER_PIN, 200, 50);
          delay(50);
          tone(BUZZER_PIN, 250, 50);
          delay(50);
          tone(BUZZER_PIN, 300, 50);
          delay(50);
          tone(BUZZER_PIN, 400, 100);
          delay(100);
      } else if (!pacmanGhosts[i].eaten) {
        // Perde una vita
        pacmanLives--;

        // Lampeggio giallo con suono simultaneo (PRIMA dell'HUD)
        // Suono morte contemporaneo al lampeggio
        tone(BUZZER_PIN, 200, 100); // Inizia suono grave

        // Lampeggio giallo SOLO sulla posizione di Pacman (non tutto schermo)
        for (int flash = 0; flash < 6; flash++) {
          // Lampeggio ON - Pacman giallo
          setPixel(pacmanX, pacmanY, CRGB(255, 255, 0));
          FastLED.show();
          if (flash % 2 == 0) {
            tone(BUZZER_PIN, 150, 100); // Suono durante lampeggio
          }
          delay(100);

          // Lampeggio OFF - ridisegna labirinto in quella posizione
          if (pacmanMaze[pacmanY][pacmanX] == 1) {
            setPixel(pacmanX, pacmanY, CRGB(0, 0, 80)); // Muro blu
          } else {
            setPixel(pacmanX, pacmanY, CRGB(0, 0, 0)); // Nero
          }
          FastLED.show();
          delay(100);
        }

        // Suono finale morte
        tone(BUZZER_PIN, 100, 300);
        delay(300);

        if (pacmanLives <= 0) {
          pacmanGameActive = false;
          pacmanGameOver = true;
          hudOverlayActive = false;  // Disattiva HUD per mostrare GAME OVER
          // Non chiamare drawPacmanGameOver() qui - sarà chiamato da updatePacman()
          return;
        } else {
          // Mostra HUD per 5 secondi DOPO l'animazione di morte
          showGameHUD(pacmanScore, pacmanLives, pacmanLevel, true);
          // Reset posizioni - il gioco ripartirà automaticamente dopo l'HUD
          pacmanResetPositions();
        }
        return;
      }
    }
  }
}

void updatePacman() {
  static bool gameOverDrawn = false;

  // Se game over, mostra schermata GAME OVER SOLO UNA VOLTA
  if (pacmanGameOver) {
    if (!gameOverDrawn) {
      drawPacmanGameOver();
      gameOverDrawn = true;
    }
    return;
  } else {
    gameOverDrawn = false; // Reset quando il gioco è attivo
  }

  if (!pacmanGameActive) return;

  unsigned long currentTime = millis();
  bool needsRedraw = false;

  // Animazione bocca (ridisegna solo Pac-Man, non tutto)
  static int lastMouth = -1;
  if (currentTime - pacmanLastMouthUpdate >= 100) {
    pacmanMouthOpen = (pacmanMouthOpen + 1) % 3;
    pacmanLastMouthUpdate = currentTime;
    if (pacmanMouthOpen != lastMouth) {
      lastMouth = pacmanMouthOpen;
      needsRedraw = true;
    }
  }

  // Power pellet lampeggiante - ridisegna solo quando lo stato cambia
  static bool lastPelletState = false;
  bool currentPelletState = ((currentTime / 250) % 2 == 0);
  if (currentPelletState != lastPelletState) {
    lastPelletState = currentPelletState;
    needsRedraw = true;
  }

  // Movimento Pac-Man
  if (currentTime - pacmanLastUpdate >= pacmanSpeed) {
    pacmanLastUpdate = currentTime;

    // Prova a cambiare direzione se richiesto
    if (pacmanNextDirX != 0 || pacmanNextDirY != 0) {
      if (pacmanCanMove(pacmanX, pacmanY, pacmanNextDirX, pacmanNextDirY)) {
        pacmanDirX = pacmanNextDirX;
        pacmanDirY = pacmanNextDirY;
        pacmanNextDirX = 0;
        pacmanNextDirY = 0;
      }
    }

    // Muovi Pac-Man
    if (pacmanDirX != 0 || pacmanDirY != 0) {
      if (pacmanCanMove(pacmanX, pacmanY, pacmanDirX, pacmanDirY)) {
        pacmanX += pacmanDirX;
        pacmanY += pacmanDirY;

        // Mangia dots
        pacmanEatDot(pacmanX, pacmanY);
        needsRedraw = true; // Ridisegna sempre dopo il movimento
      }
    }
  }

  // Se power-up è attivo o qualsiasi fantasma è frightened, ridisegna sempre per mostrare fantasmi blu
  if (pacmanPowerUpActive) {
    needsRedraw = true;
  }
  // Verifica extra per assicurarsi che i fantasmi frightened vengano visualizzati correttamente
  for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
    if (pacmanGhosts[i].frightened && !pacmanGhosts[i].eaten) {
      needsRedraw = true;
      break;
    }
  }

  // Aggiorna fantasmi (ritorna true se si sono mossi)
  if (updatePacmanGhosts()) {
    needsRedraw = true;
  }

  // Controlla collisioni
  pacmanCollisionCheck();

  // Controlla power-up timeout (dopo 6 secondi)
  if (pacmanPowerUpActive && (currentTime - pacmanPowerUpStartTime >= PACMAN_POWERUP_DURATION)) {
    pacmanPowerUpActive = false;
    // DISATTIVA frightened per TUTTI i fantasmi - tornano ai colori normali
    for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
      pacmanGhosts[i].frightened = false;
    }
    needsRedraw = true;
    Serial.println("Power-up TERMINATO - Fantasmi tornano ai colori normali");
  }

  // Lampeggio fantasmi spaventati negli ultimi 2 secondi
  static bool lastFrightenedBlink = false;
  if (pacmanPowerUpActive) {
    unsigned long timeLeft = PACMAN_POWERUP_DURATION - (currentTime - pacmanPowerUpStartTime);
    if (timeLeft < 2000) {
      bool currentBlink = ((currentTime / 200) % 2 == 0);
      if (currentBlink != lastFrightenedBlink) {
        lastFrightenedBlink = currentBlink;
        needsRedraw = true;
      }
    }
  }

  // Controlla livello completato
  if (pacmanLevelComplete) {
    delay(1000);
    pacmanNextLevel();
    needsRedraw = true;
  }

  // Ridisegna solo se qualcosa è cambiato
  if (needsRedraw) {
    drawPacman();
  }
}

bool updatePacmanGhosts() {
  unsigned long currentTime = millis();

  // Alternanza Scatter/Chase Mode (solo se NON in Frightened Mode)
  // Pattern arcade originale: Scatter 7s → Chase 20s → Scatter 7s → Chase 20s → Scatter 5s → Chase 20s → Scatter 5s → Chase ∞
  if (!pacmanPowerUpActive) {
    unsigned long timeSinceSwitch = currentTime - lastModeSwitch;
    bool switchMode = false;

    switch (modePhase) {
      case 0: // Scatter iniziale: 7 secondi
        if (timeSinceSwitch >= 7000) switchMode = true;
        break;
      case 1: // Chase: 20 secondi
        if (timeSinceSwitch >= 20000) switchMode = true;
        break;
      case 2: // Scatter: 7 secondi
        if (timeSinceSwitch >= 7000) switchMode = true;
        break;
      case 3: // Chase: 20 secondi
        if (timeSinceSwitch >= 20000) switchMode = true;
        break;
      case 4: // Scatter: 5 secondi
        if (timeSinceSwitch >= 5000) switchMode = true;
        break;
      case 5: // Chase: 20 secondi
        if (timeSinceSwitch >= 20000) switchMode = true;
        break;
      case 6: // Scatter finale: 5 secondi
        if (timeSinceSwitch >= 5000) switchMode = true;
        break;
      // case 7 e oltre: Chase permanente (nessuno switch)
    }

    if (switchMode && modePhase < 7) {
      modePhase++;
      lastModeSwitch = currentTime;
      // Alterna tra Scatter e Chase
      if (modePhase % 2 == 0) {
        currentGhostMode = GHOST_MODE_SCATTER;
      } else {
        currentGhostMode = GHOST_MODE_CHASE;
      }
      Serial.print("Ghost Mode Switch: ");
      Serial.println(currentGhostMode == GHOST_MODE_SCATTER ? "SCATTER" : "CHASE");
    }
  }

  // Usa velocità diversa se i fantasmi sono spaventati (metà velocità nel gioco originale)
  int currentGhostSpeed = pacmanGhostSpeed;
  for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
    if (pacmanGhosts[i].frightened) {
      currentGhostSpeed = pacmanGhostFrightenedSpeed;
      break;
    }
  }

  if (currentTime - pacmanLastGhostUpdate < currentGhostSpeed) return false;
  pacmanLastGhostUpdate = currentTime;

  for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
    Ghost* ghost = &pacmanGhosts[i];

    // Se il fantasma è stato mangiato, torna alla base velocemente
    if (ghost->eaten) {
      // Muovi verso la base
      if (ghost->x < pacmanGhostBaseX) ghost->x++;
      else if (ghost->x > pacmanGhostBaseX) ghost->x--;
      else if (ghost->y < pacmanGhostBaseY) ghost->y++;
      else if (ghost->y > pacmanGhostBaseY) ghost->y--;

      // Se ha raggiunto la base, riappare
      if (ghost->x == pacmanGhostBaseX && ghost->y == pacmanGhostBaseY) {
        ghost->eaten = false;
        // Se il power-up è ancora attivo, il fantasma torna frightened (BLU)
        // altrimenti torna normale
        ghost->frightened = pacmanPowerUpActive;
      }
      continue;
    }

    // AI come nel Pac-Man arcade originale: Scatter, Chase, Frightened
    int targetX = pacmanX;
    int targetY = pacmanY;

    // FRIGHTENED MODE: Movimento CASUALE (come nel gioco originale)
    if (ghost->frightened) {
      // Nel gioco originale i fantasmi blu si muovono casualmente
      // Sceglie direzione casuale ad ogni incrocio
      int possibleMoves[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
      int validMoves = 0;
      int validDirs[4][2];

      // Trova tutte le direzioni valide
      for (int d = 0; d < 4; d++) {
        int newX = ghost->x + possibleMoves[d][0];
        int newY = ghost->y + possibleMoves[d][1];
        if (newX >= 0 && newX < PACMAN_MAZE_WIDTH &&
            newY >= 0 && newY < PACMAN_MAZE_HEIGHT &&
            pacmanMaze[newY][newX] != 1) {
          validDirs[validMoves][0] = possibleMoves[d][0];
          validDirs[validMoves][1] = possibleMoves[d][1];
          validMoves++;
        }
      }

      // Sceglie direzione casuale tra quelle valide
      if (validMoves > 0) {
        int chosenDir = random(validMoves);
        ghost->dirX = validDirs[chosenDir][0];
        ghost->dirY = validDirs[chosenDir][1];
        ghost->x += ghost->dirX;
        ghost->y += ghost->dirY;
      }
      continue; // Salta alla prossima iterazione
    }

    // SCATTER MODE: Ogni fantasma va al suo angolo casa (come nel gioco originale)
    if (currentGhostMode == GHOST_MODE_SCATTER) {
      switch (i) {
        case 0: // Blinky (Rosso) - angolo alto-DESTRA
          targetX = PACMAN_MAZE_WIDTH - 1;
          targetY = 0;
          break;
        case 1: // Pinky (Rosa) - angolo alto-SINISTRA
          targetX = 0;
          targetY = 0;
          break;
        case 2: // Inky (Ciano) - angolo basso-DESTRA
          targetX = PACMAN_MAZE_WIDTH - 1;
          targetY = PACMAN_MAZE_HEIGHT - 1;
          break;
        case 3: // Clyde (Arancione) - angolo basso-SINISTRA
          targetX = 0;
          targetY = PACMAN_MAZE_HEIGHT - 1;
          break;
      }
    }
    // CHASE MODE: Ogni fantasma ha comportamento unico (come nel gioco originale)
    else {
      switch (i) {
        case 0: // Blinky (Rosso) - Inseguitore diretto
          targetX = pacmanX;
          targetY = pacmanY;
          break;

        case 1: // Pinky (Rosa) - Anticipa 4 celle avanti
          // Nel gioco originale Pinky punta 4 tile davanti a Pac-Man
          targetX = pacmanX + pacmanDirX * 4;
          targetY = pacmanY + pacmanDirY * 4;
          // Limita ai bordi del labirinto
          if (targetX < 0) targetX = 0;
          if (targetX >= PACMAN_MAZE_WIDTH) targetX = PACMAN_MAZE_WIDTH - 1;
          if (targetY < 0) targetY = 0;
          if (targetY >= PACMAN_MAZE_HEIGHT) targetY = PACMAN_MAZE_HEIGHT - 1;
          break;

        case 2: // Inky (Ciano) - Strategia di intercettamento
          // Inky usa una combinazione della posizione di Blinky e Pac-Man
          targetX = pacmanX + pacmanDirX * 2;
          targetY = pacmanY + pacmanDirY * 2;
          break;

        case 3: // Clyde (Arancione) - Timido
          {
            // Calcola distanza da Pac-Man
            int distX = ghost->x - pacmanX;
            int distY = ghost->y - pacmanY;
            int dist = distX * distX + distY * distY;

            if (dist > 64) { // Se distanza > 8 tile (come nel gioco originale)
              // Insegue Pac-Man direttamente
              targetX = pacmanX;
              targetY = pacmanY;
            } else {
              // Quando vicino, va al suo angolo (basso-sinistra)
              targetX = 0;
              targetY = PACMAN_MAZE_HEIGHT - 1;
            }
            break;
          }
      }
    }

    // Trova la direzione migliore verso il target
    int possibleDirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int bestDir = -1;
    float bestDist = 999999;

    for (int d = 0; d < 4; d++) {
      int dx = possibleDirs[d][0];
      int dy = possibleDirs[d][1];

      // Non tornare indietro (tranne se è l'unica opzione)
      if (dx == -ghost->dirX && dy == -ghost->dirY && bestDir != -1) continue;

      if (pacmanCanMove(ghost->x, ghost->y, dx, dy)) {
        int newX = ghost->x + dx;
        int newY = ghost->y + dy;
        float dist = sqrt((newX - targetX) * (newX - targetX) + (newY - targetY) * (newY - targetY));

        // Trova la direzione che minimizza la distanza dal target
        if (dist < bestDist) {
          bestDist = dist;
          bestDir = d;
        }
      }
    }

    if (bestDir >= 0) {
      ghost->dirX = possibleDirs[bestDir][0];
      ghost->dirY = possibleDirs[bestDir][1];
      ghost->x += ghost->dirX;
      ghost->y += ghost->dirY;
    }
  }

  return true; // I fantasmi si sono mossi
}

void pacmanNextLevel() {
  pacmanLevel++;
  showGameHUD(pacmanScore, pacmanLives, pacmanLevel, true);
  pacmanLevelComplete = false;

  // Aumenta difficoltà progressivamente
  pacmanSpeed = max(120, pacmanSpeed - 10); // Pac-Man leggermente più veloce (min 120ms)
  pacmanGhostSpeed = max(250, pacmanGhostSpeed - 15); // Fantasmi più veloci (min 250ms)
  pacmanGhostFrightenedSpeed = max(350, pacmanGhostFrightenedSpeed - 15); // Fantasmi spaventati più veloci (min 350ms)

  // Reset per nuovo livello
  resetPacman(false);
}

CRGB pacmanGetGhostColor(int ghostIndex) {
  // Validazione indice
  if (ghostIndex < 0 || ghostIndex >= PACMAN_NUM_GHOSTS) {
    return CRGB(255, 0, 0);  // Rosso default per indice non valido
  }

  // Se fantasma è stato mangiato, mostra grigio chiaro (occhi che tornano)
  if (pacmanGhosts[ghostIndex].eaten) {
    return CRGB(150, 150, 150); // Grigio chiaro brillante
  }

  // PRIORITÀ: Se il power-up è attivo E il fantasma è frightened, DEVE essere blu
  // Questo assicura che i fantasmi diventino sempre blu durante il power-up
  if (pacmanPowerUpActive && pacmanGhosts[ghostIndex].frightened) {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - pacmanPowerUpStartTime;

    // Protezione overflow
    if (elapsed > PACMAN_POWERUP_DURATION) {
      elapsed = PACMAN_POWERUP_DURATION;
    }

    unsigned long timeLeft = PACMAN_POWERUP_DURATION - elapsed;

    // Lampeggia bianco/blu negli ultimi 2 secondi
    if (timeLeft < 2000) {
      if ((currentTime / 200) % 2 == 0) {
        return CRGB(255, 255, 255); // Bianco lampeggiante
      }
    }

    // BLU PURO BRILLANTE - colore molto distintivo per fantasmi spaventati
    return CRGB(0, 50, 255); // Blu brillante (leggermente più chiaro per visibilità)
  }

  // Fallback: se solo frightened è true (senza power-up attivo), comunque blu
  if (pacmanGhosts[ghostIndex].frightened) {
    return CRGB(0, 50, 255); // Blu brillante
  }

  // Colori normali per ogni fantasma (aumentata luminosità)
  switch (pacmanGhosts[ghostIndex].color) {
    case 0: return CRGB(255, 80, 80);   // Rosso brillante (Blinky)
    case 1: return CRGB(255, 180, 255); // Rosa brillante (Pinky)
    case 2: return CRGB(100, 255, 255); // Ciano brillante (Inky)
    case 3: return CRGB(255, 180, 50);  // Arancione brillante (Clyde)
    default: return CRGB(255, 80, 80);  // Default Rosso brillante
  }
}

void drawPacman() {
  // Debug: conta quante volte viene chiamato drawPacman
  static unsigned long drawCount = 0;
  static unsigned long lastDrawReport = 0;
  drawCount++;
  if (millis() - lastDrawReport > 5000) {
    Serial.print("drawPacman chiamato ");
    Serial.print(drawCount);
    Serial.println(" volte negli ultimi 5s");
    drawCount = 0;
    lastDrawReport = millis();
  }

  // NON chiamare clearMatrix() per evitare flickering - ridisegniamo tutti i pixel

  // Disegna labirinto
  for (int y = 0; y < PACMAN_MAZE_HEIGHT; y++) {
    for (int x = 0; x < PACMAN_MAZE_WIDTH; x++) {
      if (pacmanMaze[y][x] == 1) {
        // Muro - blu scuro
        setPixel(x, y, CRGB(0, 0, 80));
      } else if (pacmanMaze[y][x] == 2) {
        // Dot - bianco luminoso e ben visibile
        setPixel(x, y, CRGB(200, 200, 200));
      } else if (pacmanMaze[y][x] == 3) {
        // Power pellet - lampeggia giallo brillante
        if ((millis() / 250) % 2 == 0) {
          setPixel(x, y, CRGB(255, 200, 0));
        } else {
          setPixel(x, y, CRGB(0, 0, 0)); // Nero quando spento
        }
      } else {
        // Spazio vuoto - nero
        setPixel(x, y, CRGB(0, 0, 0));
      }
    }
  }

  // Disegna fantasmi con i loro colori
  static unsigned long lastGhostDebug = 0;
  bool printDebug = (millis() - lastGhostDebug > 1000) || pacmanPowerUpActive;

  if (printDebug) {
    Serial.print("Disegno ");
    Serial.print(PACMAN_NUM_GHOSTS);
    Serial.print(" fantasmi - PowerUp=");
    Serial.print(pacmanPowerUpActive ? "SI" : "NO");
    Serial.print(" | ");
    lastGhostDebug = millis();
  }

  for (int i = 0; i < PACMAN_NUM_GHOSTS; i++) {
    CRGB color = pacmanGetGhostColor(i);
    setPixel(pacmanGhosts[i].x, pacmanGhosts[i].y, color);

    if (printDebug) {
      Serial.print("F");
      Serial.print(i);
      Serial.print("(");
      Serial.print(pacmanGhosts[i].x);
      Serial.print(",");
      Serial.print(pacmanGhosts[i].y);
      Serial.print(")");
      if (pacmanGhosts[i].eaten) Serial.print("EATEN");
      else if (pacmanGhosts[i].frightened) Serial.print("SCARED");
      else Serial.print("NORM");
      Serial.print(" ");
    }
  }
  if (printDebug) Serial.println();

  // Disegna Pac-Man - sempre sopra tutto (massima luminosità)
  CRGB pacColor = CRGB(255, 255, 50); // Giallo brillantissimo
  if (pacmanMouthOpen == 1) {
    pacColor = CRGB(255, 240, 30); // Bocca semi-aperta - luminoso
  } else if (pacmanMouthOpen == 2) {
    pacColor = CRGB(255, 220, 20); // Bocca completamente aperta - luminoso
  }
  setPixel(pacmanX, pacmanY, pacColor);

  FastLED.show();
}

void drawPacmanGameOver() {
  // Mostra "GAME OVER" in rosso con font 3x5, lettere distanziate e centrato

  clearMatrixNoShow();

  // "GAME" alla riga Y=2 (centrato verticalmente nella parte alta)
  drawCharacter('G', 0, 2, CRGB(255, 0, 0));
  drawCharacter('A', 4, 2, CRGB(255, 0, 0));
  drawCharacter('M', 8, 2, CRGB(255, 0, 0));
  drawCharacter('E', 12, 2, CRGB(255, 0, 0));

  // "OVER" alla riga Y=9 (centrato verticalmente nella parte bassa)
  drawCharacter('O', 0, 9, CRGB(255, 0, 0));
  drawCharacter('V', 4, 9, CRGB(255, 0, 0));
  drawCharacter('E', 8, 9, CRGB(255, 0, 0));
  drawCharacter('R', 12, 9, CRGB(255, 0, 0));

  FastLED.show();

  // Suono GAME OVER - suona solo una volta per game over
  if (!pacmanGameOverSoundPlayed && !pacmanGameOverDisplayed) {
    playGameOver();
    pacmanGameOverSoundPlayed = true;
    pacmanGameOverDisplayed = true;
  }
}

void handlePacman() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Pac-Man</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:#000;color:#fff;padding:20px;touch-action:manipulation;-webkit-touch-callout:none;-webkit-user-select:none;user-select:none;}";
  html += ".container{max-width:600px;margin:0 auto;background:#1a1a1a;padding:20px;border-radius:10px;border:1px solid #333;}";
  html += ".score{font-size:24px;margin:15px;color:#ff0;}";
  html += ".lives{font-size:20px;margin:10px;color:#0ff;}";
  html += ".gamepad{display:grid;grid-template-columns:repeat(3,80px);gap:10px;margin:20px auto;justify-content:center;touch-action:manipulation;}";
  html += ".gamepad button{height:80px;font-size:24px;border:none;border-radius:10px;background:#444;color:white;cursor:pointer;display:flex;align-items:center;justify-content:center;touch-action:manipulation;}";
  html += ".gamepad button:active{background:#666;}";
  html += ".reset-btn{padding:15px 30px;background:#FFD700;color:#000;border:none;border-radius:10px;font-size:18px;margin:20px;cursor:pointer;touch-action:manipulation;}";
  html += ".reset-btn:active{background:#FFA500;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".pacman-icon{position:relative;display:inline-block;width:60px;height:60px;background:#FFD700;border-radius:50%;margin:0 auto;vertical-align:middle;}";
  html += ".pacman-icon:before{content:'';position:absolute;width:0;height:0;border-right:25px solid #1a1a1a;border-top:15px solid transparent;border-bottom:15px solid transparent;top:50%;right:0;transform:translateY(-50%);}";
  html += ".pacman-icon:after{content:'';position:absolute;width:6px;height:6px;background:#000;border-radius:50%;top:18px;left:20px;}";
  html += "</style>";
  html += "<script>";
  html += "function sendCmd(cmd){fetch('/pacman_control?cmd='+cmd);}";
  html += "function updateStatus(){fetch('/pacman_status').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('score').innerHTML='Score: '+d.score;";
  html += "document.getElementById('lives').innerHTML='Lives: '+d.lives+' | Level: '+d.level;";
  html += "if(d.gameOver){document.getElementById('status').innerHTML='GAME OVER!';}";
  html += "else if(d.active){document.getElementById('status').innerHTML='Playing';}";
  html += "else{document.getElementById('status').innerHTML='Press START';}";
  html += "});}";
  html += "setInterval(updateStatus, 500);";
  html += "let activeKeys={};";
  html += "document.addEventListener('keydown',function(e){";
  html += "if(e.key=='ArrowUp'||e.key=='ArrowDown'||e.key=='ArrowLeft'||e.key=='ArrowRight'||e.key==' '){e.preventDefault();}";
  html += "if(activeKeys[e.key])return;";
  html += "activeKeys[e.key]=true;";
  html += "if(e.key=='ArrowUp')sendCmd('up');";
  html += "if(e.key=='ArrowDown')sendCmd('down');";
  html += "if(e.key=='ArrowLeft')sendCmd('left');";
  html += "if(e.key=='ArrowRight')sendCmd('right');";
  html += "if(e.key==' ')sendCmd('start');";
  html += "});";
  html += "document.addEventListener('keyup',function(e){";
  html += "if(e.key=='ArrowUp'||e.key=='ArrowDown'||e.key=='ArrowLeft'||e.key=='ArrowRight'){e.preventDefault();}";
  html += "delete activeKeys[e.key];";
  html += "});";
  html += "document.addEventListener('touchmove',function(e){if(e.target.tagName=='BUTTON')e.preventDefault();},{passive:false});";
  html += "function enableBT(){window.location='/enableBluetooth?game=pacman';}";
  html += "</script></head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a> <a href='#' onclick='enableBT()' style='background:#2196F3;margin-left:10px;'>🎮 Bluetooth</a></div>";
  html += "<div class='container'>";
  html += "<h1 style='color:#FFD700;'><div class='pacman-icon' style='display:inline-block;margin-right:10px;'></div> PAC-MAN</h1>";
  html += "<div id='status' style='font-size:20px;margin:15px;'>Ready</div>";
  html += "<div id='score' class='score'>Score: " + String(min(pacmanScore, 999)) + "</div>";
  html += "<div id='lives' class='lives'>Lives: " + String(pacmanLives) + " | Level: " + String(pacmanLevel) + "</div>";
  html += "<div class='gamepad'>";
  html += "<div></div><button ontouchstart='event.preventDefault();sendCmd(\"up\")' onclick='sendCmd(\"up\")'>&#9650;</button><div></div>";
  html += "<button ontouchstart='event.preventDefault();sendCmd(\"left\")' onclick='sendCmd(\"left\")'>&#9664;</button>";
  html += "<button onclick='sendCmd(\"start\")' style='background:#FFD700;color:#000;font-weight:bold;'>START</button>";
  html += "<button ontouchstart='event.preventDefault();sendCmd(\"right\")' onclick='sendCmd(\"right\")'>&#9654;</button>";
  html += "<div></div><button ontouchstart='event.preventDefault();sendCmd(\"down\")' onclick='sendCmd(\"down\")'>&#9660;</button><div></div>";
  html += "</div>";
  html += "<button class='reset-btn' onclick='sendCmd(\"reset\")'>RESET</button>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handlePacmanControl() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");

    if (cmd == "up") {
      pacmanChangeDir(0, -1);
    } else if (cmd == "down") {
      pacmanChangeDir(0, 1);
    } else if (cmd == "left") {
      pacmanChangeDir(-1, 0);
    } else if (cmd == "right") {
      pacmanChangeDir(1, 0);
    } else if (cmd == "stop") {
      // Ferma Pac-Man al rilascio del tasto
      pacmanDirX = 0;
      pacmanDirY = 0;
      pacmanNextDirX = 0;
      pacmanNextDirY = 0;
    } else if (cmd == "start") {
      resetPacman(true);
      // Suona la melodia di inizio Pac-Man SOLO quando si preme START (non RESET)
      // Il labirinto è già visualizzato da resetPacman(), la melodia suona mentre è visibile
      playPacmanBeginningMelody();
      changeState(STATE_GAME_PACMAN);
    } else if (cmd == "reset") {
      resetPacman(true);
      pacmanGameActive = false;  // Reset senza avviare il gioco
      changeState(STATE_GAME_PACMAN);
    }
  }

  server.send(200, "text/plain", "OK");
}

void handlePacmanStatus() {
  String json = "{";
  json += "\"score\":" + String(min(pacmanScore, 999)) + ",";
  json += "\"lives\":" + String(pacmanLives) + ",";
  json += "\"level\":" + String(pacmanLevel) + ",";
  json += "\"active\":" + String(pacmanGameActive ? "true" : "false") + ",";
  json += "\"gameOver\":" + String(pacmanGameOver ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================
// SIMON SAYS
// ============================================

void resetSimon() {
  simonLevel = 0;
  simonCurrentStep = 0;
  simonPlayerInput = 0;
  simonScore = 0;
  simonState = SIMON_IDLE;
  simonGameActive = false;
  simonCurrentColor = -1;
  simonWaitingForRelease = false;
  simonShowDelay = 600; // Reset velocità

  // Azzera sequenza
  for (int i = 0; i < SIMON_MAX_SEQUENCE; i++) {
    simonSequence[i] = 0;
  }
}

void simonAddToSequence() {
  if (simonLevel < SIMON_MAX_SEQUENCE) {
    simonSequence[simonLevel] = random(0, 4); // Aggiungi colore random (0-3)
    simonLevel++;
    simonScore += 10; // +10 punti per ogni livello completato

    // Aumenta difficoltà riducendo il tempo di visualizzazione
    simonShowDelay = max(200, 600 - (simonLevel * 20));
  }
}

CRGB simonGetColor(byte color, bool highlight) {
  CRGB baseColor;

  switch(color) {
    case SIMON_RED:
      baseColor = highlight ? CRGB(255, 0, 0) : CRGB(60, 0, 0);
      break;
    case SIMON_GREEN:
      baseColor = highlight ? CRGB(0, 255, 0) : CRGB(0, 60, 0);
      break;
    case SIMON_BLUE:
      baseColor = highlight ? CRGB(0, 0, 255) : CRGB(0, 0, 60);
      break;
    case SIMON_YELLOW:
      baseColor = highlight ? CRGB(255, 255, 0) : CRGB(60, 60, 0);
      break;
    default:
      baseColor = CRGB(0, 0, 0);
  }

  return baseColor;
}

void simonDrawQuadrant(byte quadrant, bool highlight) {
  // Matrice 16x16 divisa in 4 quadranti 7x7 con spazio centrale
  // ROSSO: 0-6, 0-6 (alto sinistra)
  // VERDE: 9-15, 0-6 (alto destra)
  // BLU: 0-6, 9-15 (basso sinistra)
  // GIALLO: 9-15, 9-15 (basso destra)

  CRGB color = simonGetColor(quadrant, highlight);

  int startX, endX, startY, endY;

  switch(quadrant) {
    case SIMON_RED:
      startX = 0; endX = 6;
      startY = 0; endY = 6;
      break;
    case SIMON_GREEN:
      startX = 9; endX = 15;
      startY = 0; endY = 6;
      break;
    case SIMON_BLUE:
      startX = 0; endX = 6;
      startY = 9; endY = 15;
      break;
    case SIMON_YELLOW:
      startX = 9; endX = 15;
      startY = 9; endY = 15;
      break;
    default:
      return;
  }

  // Disegna quadrante con bordo arrotondato
  for (int x = startX; x <= endX; x++) {
    for (int y = startY; y <= endY; y++) {
      // Salta gli angoli per effetto arrotondato
      bool isCorner = false;
      if ((x == startX || x == endX) && (y == startY || y == endY)) {
        isCorner = true;
      }

      if (!isCorner) {
        setPixel(x, y, color);
      }
    }
  }
}

void drawSimon() {
  clearMatrixNoShow();

  // Disegna tutti e 4 i quadranti
  for (byte i = 0; i < 4; i++) {
    bool highlight = (simonCurrentColor == i);
    simonDrawQuadrant(i, highlight);
  }

  // Disegna spazio centrale nero (croce separatrice)
  for (int i = 0; i < 16; i++) {
    setPixel(7, i, CRGB(0, 0, 0));
    setPixel(8, i, CRGB(0, 0, 0));
    setPixel(i, 7, CRGB(0, 0, 0));
    setPixel(i, 8, CRGB(0, 0, 0));
  }

  FastLED.show();
}

void simonPlayTone(byte color) {
  if (soundEnabled && color >= 0 && color < 4) {
    tone(BUZZER_PIN, simonTones[color], simonShowDelay - 50);
  }
}

void updateSimon() {
  unsigned long currentTime = millis();

  switch(simonState) {
    case SIMON_IDLE:
      // In attesa - mostra schermata inizio
      drawSimon();
      break;

    case SIMON_SHOWING:
      // Mostra la sequenza al giocatore
      if (simonCurrentColor == -1) {
        // Inizia a mostrare il prossimo colore della sequenza
        if (simonCurrentStep < simonLevel) {
          simonCurrentColor = simonSequence[simonCurrentStep];
          simonColorStartTime = currentTime;
          simonPlayTone(simonCurrentColor);
          drawSimon();
        } else {
          // Sequenza completata, passa a input giocatore
          simonState = SIMON_WAITING_INPUT;
          simonCurrentStep = 0;
          simonPlayerInput = 0;
          simonCurrentColor = -1;
          drawSimon();
        }
      } else {
        // Colore attualmente illuminato
        if (currentTime - simonColorStartTime >= simonShowDelay) {
          // Spegni il colore
          simonCurrentColor = -1;
          drawSimon();
          simonCurrentStep++;
          delay(150); // Pausa tra i colori
        }
      }
      break;

    case SIMON_WAITING_INPUT:
      // Aspetta input giocatore (gestito via web)
      drawSimon();
      break;

    case SIMON_CORRECT:
      // Input corretto - animazione breve
      if (currentTime - simonLastUpdate >= 300) {
        if (simonPlayerInput >= simonLevel) {
          // Livello completato!
          simonState = SIMON_LEVEL_UP;
          simonLastUpdate = currentTime;
        } else {
          // Aspetta prossimo input
          simonState = SIMON_WAITING_INPUT;
          simonCurrentColor = -1;
          drawSimon();
        }
      }
      break;

    case SIMON_LEVEL_UP:
      // Animazione livello completato
      if (currentTime - simonLastUpdate >= 800) {
        // Aggiungi nuovo colore alla sequenza
        simonAddToSequence();
        simonCurrentStep = 0;
        simonState = SIMON_SHOWING;
        simonCurrentColor = -1;
        delay(300);
      } else {
        // Animazione: tutti i quadranti lampeggiano
        bool blink = ((currentTime / 150) % 2 == 0);
        clearMatrixNoShow();
        for (byte i = 0; i < 4; i++) {
          simonDrawQuadrant(i, blink);
        }
        FastLED.show();
      }
      break;

    case SIMON_WRONG:
      // Game Over - animazione
      if (currentTime - simonLastUpdate < 2000) {
        // Lampeggia tutto rosso per 2 secondi
        bool blink = ((currentTime / 200) % 2 == 0);
        if (blink) {
          for (int i = 0; i < NUM_LEDS; i++) {
            leds[i] = CRGB(255, 0, 0);
          }
        } else {
          clearMatrixNoShow();
        }
        FastLED.show();
      } else {
        // Torna a IDLE
        simonState = SIMON_IDLE;
        simonGameActive = false;
        drawSimon();
      }
      break;
  }
}

void simonCheckInput(byte color) {
  if (simonState != SIMON_WAITING_INPUT) return;

  // Illumina il quadrante premuto
  simonCurrentColor = color;
  simonPlayTone(color);
  drawSimon();

  // Controlla se è corretto
  if (color == simonSequence[simonPlayerInput]) {
    // CORRETTO!
    simonPlayerInput++;
    simonState = SIMON_CORRECT;
    simonLastUpdate = millis();

    // Il tono del colore già suonato è sufficiente come feedback
    // Non servono beep aggiuntivi
  } else {
    // SBAGLIATO!
    simonState = SIMON_WRONG;
    simonLastUpdate = millis();

    // Aggiorna miglior punteggio
    if (simonScore > simonBestScore) {
      simonBestScore = simonScore;
    }

    playGameOver();
  }
}

void handleSimon() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Simon Says</title>";
  html += "<style>";
  html += "body{margin:0;padding:0;font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;display:flex;justify-content:center;align-items:center;min-height:100vh;}";
  html += ".container{text-align:center;max-width:500px;padding:20px;}";
  html += ".nav{margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;}";
  html += "h1{font-size:2.5em;margin:20px 0;text-shadow:0 0 20px rgba(78,204,163,0.5);}";
  html += ".stats{display:flex;justify-content:space-around;margin:30px 0;font-size:1.3em;}";
  html += ".stat{background:rgba(15,52,96,0.6);padding:15px 25px;border-radius:10px;border:2px solid rgba(78,204,163,0.3);}";
  html += ".stat-label{font-size:0.8em;opacity:0.7;margin-bottom:5px;}";
  html += ".stat-value{font-size:1.5em;font-weight:bold;color:#4ecca3;}";
  html += ".game-grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;max-width:400px;margin:30px auto;}";
  html += ".simon-btn{width:100%;aspect-ratio:1;border:none;border-radius:20px;cursor:pointer;font-size:1.5em;font-weight:bold;";
  html += "transition:all 0.1s;box-shadow:0 8px 20px rgba(0,0,0,0.4);position:relative;}";
  html += ".simon-btn:active{transform:scale(0.95);box-shadow:0 4px 10px rgba(0,0,0,0.6);}";
  html += ".btn-red{background:linear-gradient(135deg,#c0392b,#e74c3c);color:#fff;}";
  html += ".btn-red:hover{background:linear-gradient(135deg,#e74c3c,#ff6b6b);}";
  html += ".btn-green{background:linear-gradient(135deg,#27ae60,#2ecc71);color:#fff;}";
  html += ".btn-green:hover{background:linear-gradient(135deg,#2ecc71,#55efc4);}";
  html += ".btn-blue{background:linear-gradient(135deg,#2980b9,#3498db);color:#fff;}";
  html += ".btn-blue:hover{background:linear-gradient(135deg,#3498db,#74b9ff);}";
  html += ".btn-yellow{background:linear-gradient(135deg,#f39c12,#f1c40f);color:#fff;}";
  html += ".btn-yellow:hover{background:linear-gradient(135deg,#f1c40f,#ffeaa7);}";
  html += ".controls{margin:30px 0;}";
  html += ".control-btn{padding:15px 40px;margin:10px;font-size:1.2em;background:linear-gradient(135deg,#4ecca3,#0fb9b1);";
  html += "border:none;border-radius:10px;color:#fff;cursor:pointer;box-shadow:0 4px 15px rgba(78,204,163,0.4);}";
  html += ".control-btn:hover{background:linear-gradient(135deg,#0fb9b1,#4ecca3);transform:translateY(-2px);box-shadow:0 6px 20px rgba(78,204,163,0.6);}";
  html += ".status{font-size:1.3em;margin:20px 0;min-height:30px;color:#4ecca3;}";
  html += "@keyframes pulse{0%,100%{transform:scale(1);}50%{transform:scale(1.05);}}";
  html += ".active{animation:pulse 0.3s ease;}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<h1>🎮 SIMON SAYS</h1>";

  html += "<div class='stats'>";
  html += "<div class='stat'><div class='stat-label'>LIVELLO</div><div class='stat-value' id='level'>0</div></div>";
  html += "<div class='stat'><div class='stat-label'>PUNTEGGIO</div><div class='stat-value' id='score'>0</div></div>";
  html += "<div class='stat'><div class='stat-label'>RECORD</div><div class='stat-value' id='best'>" + String(simonBestScore) + "</div></div>";
  html += "</div>";

  html += "<div class='status' id='status'>Premi START per giocare</div>";

  html += "<div class='game-grid'>";
  html += "<button class='simon-btn btn-red' onclick='press(0)'>ROSSO</button>";
  html += "<button class='simon-btn btn-green' onclick='press(1)'>VERDE</button>";
  html += "<button class='simon-btn btn-blue' onclick='press(2)'>BLU</button>";
  html += "<button class='simon-btn btn-yellow' onclick='press(3)'>GIALLO</button>";
  html += "</div>";

  html += "<div class='controls'>";
  html += "<button class='control-btn' onclick='start()'>START</button>";
  html += "<button class='control-btn' onclick='reset()'>RESET</button>";
  html += "</div>";

  html += "</div>";

  html += "<script>";
  html += "function press(color){";
  html += "  fetch('/simoncontrol?cmd=press&color='+color);";
  html += "  const btns=[document.querySelectorAll('.simon-btn')[color]];";
  html += "  btns[0].classList.add('active');";
  html += "  setTimeout(()=>btns[0].classList.remove('active'),300);";
  html += "}";
  html += "function start(){fetch('/simoncontrol?cmd=start');updateStatus();}";
  html += "function reset(){fetch('/simoncontrol?cmd=reset');updateStatus();}";
  html += "function updateStatus(){";
  html += "  fetch('/simoncontrol?cmd=status').then(r=>r.json()).then(data=>{";
  html += "    document.getElementById('level').textContent=data.level;";
  html += "    document.getElementById('score').textContent=data.score;";
  html += "    document.getElementById('best').textContent=data.best;";
  html += "    let status='';";
  html += "    if(!data.active)status='Premi START per giocare';";
  html += "    else if(data.state=='showing')status='Guarda la sequenza...';";
  html += "    else if(data.state=='waiting')status='Tocca a te! Ripeti la sequenza';";
  html += "    else if(data.state=='correct')status='Corretto! ✓';";
  html += "    else if(data.state=='levelup')status='LIVELLO COMPLETATO! 🎉';";
  html += "    else if(data.state=='wrong')status='GAME OVER! ✗';";
  html += "    document.getElementById('status').textContent=status;";
  html += "  });";
  html += "}";
  html += "setInterval(updateStatus,500);";
  html += "updateStatus();";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSimonControl() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");

    if (cmd == "status") {
      // Ritorna stato JSON
      String json = "{";
      json += "\"level\":" + String(simonLevel) + ",";
      json += "\"score\":" + String(simonScore) + ",";
      json += "\"best\":" + String(simonBestScore) + ",";
      json += "\"active\":" + String(simonGameActive ? "true" : "false") + ",";

      String stateStr = "idle";
      switch(simonState) {
        case SIMON_SHOWING: stateStr = "showing"; break;
        case SIMON_WAITING_INPUT: stateStr = "waiting"; break;
        case SIMON_CORRECT: stateStr = "correct"; break;
        case SIMON_LEVEL_UP: stateStr = "levelup"; break;
        case SIMON_WRONG: stateStr = "wrong"; break;
      }
      json += "\"state\":\"" + stateStr + "\"";
      json += "}";

      server.send(200, "application/json", json);
      return;
    }
    else if (cmd == "start") {
      resetSimon();
      simonGameActive = true;
      simonAddToSequence(); // Primo colore
      simonState = SIMON_SHOWING;
      changeState(STATE_GAME_SIMON);
    }
    else if (cmd == "reset") {
      resetSimon();
      changeState(STATE_GAME_SIMON);
    }
    else if (cmd == "press") {
      if (server.hasArg("color")) {
        byte color = server.arg("color").toInt();
        if (color >= 0 && color < 4) {
          simonCheckInput(color);
        }
      }
    }
  }

  server.send(200, "text/plain", "OK");
}

// ============================================
// CRONOMETRO
// ============================================
void handleStopwatch() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Cronometro</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#0f0f23 0%,#1a1a2e 50%,#16213e 100%);color:#fff;margin:0;padding:20px;min-height:100vh;}";
  html += ".container{max-width:600px;background:rgba(26,26,46,0.9);backdrop-filter:blur(10px);padding:40px;border-radius:20px;box-shadow:0 8px 32px rgba(0,0,0,0.5);border:2px solid rgba(66,133,244,0.3);margin:0 auto;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{font-size:2.5em;margin-bottom:40px;text-shadow:0 2px 10px rgba(0,0,0,0.3);}";
  html += ".display{font-size:3em;font-weight:bold;margin:40px 0;font-family:'Courier New',monospace;text-shadow:0 4px 20px rgba(0,0,0,0.5);letter-spacing:0.02em;}";
  html += ".milliseconds{font-size:0.5em;color:rgba(255,255,255,0.8);}";
  html += ".controls{display:flex;gap:20px;justify-content:center;flex-wrap:wrap;margin-top:40px;}";
  html += ".btn{padding:20px 50px;font-size:1.3em;border:none;border-radius:15px;cursor:pointer;font-weight:bold;transition:all 0.3s;text-transform:uppercase;box-shadow:0 4px 15px rgba(0,0,0,0.2);}";
  html += ".btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.3);}";
  html += ".btn:active{transform:translateY(0);}";
  html += ".btn-start{background:#4CAF50;color:white;}";
  html += ".btn-stop{background:#f44336;color:white;}";
  html += ".btn-reset{background:#FF9800;color:white;}";
  html += ".btn-lap{background:#2196F3;color:white;}";
  html += ".laps{margin-top:30px;max-height:200px;overflow-y:auto;background:rgba(0,0,0,0.2);border-radius:10px;padding:15px;}";
  html += ".lap-item{padding:10px;background:rgba(255,255,255,0.1);margin:5px 0;border-radius:5px;display:flex;justify-content:space-between;}";
  html += ".lap-number{font-weight:bold;color:#FFD700;}";
  html += ".color-picker{margin:20px 0;padding:15px;background:rgba(0,0,0,0.3);border-radius:10px;border:1px solid rgba(66,133,244,0.3);}";
  html += ".color-picker h4{margin:0 0 15px 0;font-size:1em;color:#aaa;}";
  html += ".color-buttons{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;}";
  html += ".color-btn{width:50px;height:50px;border:3px solid transparent;border-radius:50%;cursor:pointer;transition:all 0.3s;box-shadow:0 2px 5px rgba(0,0,0,0.3);}";
  html += ".color-btn:hover{transform:scale(1.1);box-shadow:0 4px 10px rgba(0,0,0,0.5);}";
  html += ".color-btn.active{border-color:#fff;transform:scale(1.15);}";
  html += "</style>";
  html += "<script>";
  html += "let startTime=0,elapsedTime=0,timerInterval=null,running=false,laps=[];";
  html += "function formatTime(ms){";
  html += "let totalSeconds=Math.floor(ms/1000);";
  html += "let hours=Math.floor(totalSeconds/3600);";
  html += "let minutes=Math.floor((totalSeconds%3600)/60);";
  html += "let seconds=totalSeconds%60;";
  html += "let milliseconds=Math.floor((ms%1000)/10);";
  html += "return (hours<10?'0':'')+hours+':'+(minutes<10?'0':'')+minutes+':'+(seconds<10?'0':'')+seconds+'<span class=\"milliseconds\">.'+( milliseconds<10?'0':'')+milliseconds+'</span>';";
  html += "}";
  html += "function updateDisplay(){";
  html += "let currentTime=running?Date.now()-startTime+elapsedTime:elapsedTime;";
  html += "document.getElementById('display').innerHTML=formatTime(currentTime);";
  html += "}";
  html += "function start(){";
  html += "if(!running){";
  html += "fetch('/stopwatchcontrol?cmd=start');";
  html += "startTime=Date.now();";
  html += "running=true;";
  html += "timerInterval=setInterval(updateDisplay,10);";
  html += "document.getElementById('startBtn').style.display='none';";
  html += "document.getElementById('stopBtn').style.display='inline-block';";
  html += "document.getElementById('lapBtn').style.display='inline-block';";
  html += "}";
  html += "}";
  html += "function stop(){";
  html += "if(running){";
  html += "fetch('/stopwatchcontrol?cmd=stop');";
  html += "elapsedTime+=Date.now()-startTime;";
  html += "running=false;";
  html += "clearInterval(timerInterval);";
  html += "document.getElementById('startBtn').style.display='inline-block';";
  html += "document.getElementById('stopBtn').style.display='none';";
  html += "document.getElementById('lapBtn').style.display='none';";
  html += "}";
  html += "}";
  html += "function reset(){";
  html += "fetch('/stopwatchcontrol?cmd=reset');";
  html += "stop();";
  html += "startTime=0;elapsedTime=0;laps=[];";
  html += "updateDisplay();";
  html += "document.getElementById('laps').innerHTML='';";
  html += "}";
  html += "function lap(){";
  html += "if(running){";
  html += "let currentTime=Date.now()-startTime+elapsedTime;";
  html += "laps.push(currentTime);";
  html += "let lapsList=document.getElementById('laps');";
  html += "let lapItem=document.createElement('div');";
  html += "lapItem.className='lap-item';";
  html += "lapItem.innerHTML='<span class=\"lap-number\">Giro '+laps.length+'</span><span>'+formatTime(currentTime)+'</span>';";
  html += "lapsList.insertBefore(lapItem,lapsList.firstChild);";
  html += "}";
  html += "}";
  html += "function setColor(mode){";
  html += "fetch('/stopwatchcontrol?cmd=color&mode='+mode);";
  html += "document.querySelectorAll('.color-btn').forEach(b=>b.classList.remove('active'));";
  html += "document.getElementById('color-'+mode).classList.add('active');";
  html += "}";
  html += "setInterval(updateDisplay,10);";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>⏱️ CRONOMETRO</h1>";
  html += "<div class='display' id='display'>00:00:00<span class='milliseconds'>.00</span></div>";
  html += "<div class='color-picker'>";
  html += "<h4>🎨 Colore Display</h4>";
  html += "<div class='color-buttons'>";
  html += "<div class='color-btn active' id='color-0' onclick='setColor(0)' style='background:#00c8ff;' title='Ciano'></div>";
  html += "<div class='color-btn' id='color-1' onclick='setColor(1)' style='background:#ff0000;' title='Rosso'></div>";
  html += "<div class='color-btn' id='color-2' onclick='setColor(2)' style='background:#00ff00;' title='Verde'></div>";
  html += "<div class='color-btn' id='color-3' onclick='setColor(3)' style='background:#0000ff;' title='Blu'></div>";
  html += "<div class='color-btn' id='color-4' onclick='setColor(4)' style='background:#ffff00;' title='Giallo'></div>";
  html += "<div class='color-btn' id='color-5' onclick='setColor(5)' style='background:#ff00ff;' title='Magenta'></div>";
  html += "<div class='color-btn' id='color-6' onclick='setColor(6)' style='background:#ffffff;' title='Bianco'></div>";
  html += "<div class='color-btn' id='color-7' onclick='setColor(7)' style='background:#ffa500;' title='Arancione'></div>";
  html += "<div class='color-btn' id='color-8' onclick='setColor(8)' style='background:linear-gradient(90deg,#ff0000,#ff7f00,#ffff00,#00ff00,#0000ff,#4b0082,#9400d3);' title='Rainbow'></div>";
  html += "</div>";
  html += "</div>";
  html += "<div class='controls'>";
  html += "<button id='startBtn' class='btn btn-start' onclick='start()'>▶️ Start</button>";
  html += "<button id='stopBtn' class='btn btn-stop' onclick='stop()' style='display:none;'>⏸️ Stop</button>";
  html += "<button id='lapBtn' class='btn btn-lap' onclick='lap()' style='display:none;'>🏁 Giro</button>";
  html += "<button class='btn btn-reset' onclick='reset()'>🔄 Reset</button>";
  html += "</div>";
  html += "<div class='laps' id='laps'></div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
  changeState(STATE_STOPWATCH);
}

// ============================================
// TIMER (CONTO ALLA ROVESCIA)
// ============================================
void handleTimer() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Timer</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#0f0f23 0%,#2e1a1a 50%,#3e1616 100%);color:#fff;margin:0;padding:20px;min-height:100vh;}";
  html += ".container{max-width:600px;background:rgba(46,26,26,0.9);backdrop-filter:blur(10px);padding:40px;border-radius:20px;box-shadow:0 8px 32px rgba(0,0,0,0.5);border:2px solid rgba(244,67,54,0.3);margin:0 auto;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{font-size:2.5em;margin-bottom:40px;text-shadow:0 2px 10px rgba(0,0,0,0.3);}";
  html += ".display{font-size:3.5em;font-weight:bold;margin:40px 0;font-family:'Courier New',monospace;text-shadow:0 4px 20px rgba(0,0,0,0.5);letter-spacing:0.05em;color:#4CAF50;}";
  html += ".display.warning{color:#FF9800;animation:pulse 1s infinite;}";
  html += ".display.danger{color:#f44336;animation:pulse 0.5s infinite;}";
  html += "@keyframes pulse{0%,100%{transform:scale(1);}50%{transform:scale(1.05);}}";
  html += ".time-inputs{display:flex;gap:15px;justify-content:center;margin:30px 0;flex-wrap:wrap;}";
  html += ".input-group{display:flex;flex-direction:column;align-items:center;}";
  html += ".input-group label{font-size:0.9em;margin-bottom:10px;text-transform:uppercase;letter-spacing:1px;}";
  html += ".input-group input{width:80px;padding:15px;font-size:2em;text-align:center;border:none;border-radius:10px;background:rgba(255,255,255,0.9);font-weight:bold;}";
  html += ".controls{display:flex;gap:20px;justify-content:center;flex-wrap:wrap;margin-top:40px;}";
  html += ".btn{padding:20px 50px;font-size:1.3em;border:none;border-radius:15px;cursor:pointer;font-weight:bold;transition:all 0.3s;text-transform:uppercase;box-shadow:0 4px 15px rgba(0,0,0,0.2);}";
  html += ".btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.3);}";
  html += ".btn:active{transform:translateY(0);}";
  html += ".btn-start{background:#4CAF50;color:white;}";
  html += ".btn-stop{background:#f44336;color:white;}";
  html += ".btn-reset{background:#FF9800;color:white;}";
  html += ".presets{margin-top:30px;display:flex;gap:10px;justify-content:center;flex-wrap:wrap;}";
  html += ".preset-btn{padding:10px 20px;background:rgba(255,255,255,0.2);border:none;border-radius:8px;color:white;cursor:pointer;font-size:1em;}";
  html += ".preset-btn:hover{background:rgba(255,255,255,0.3);}";
  html += ".color-picker{margin:20px 0;padding:15px;background:rgba(0,0,0,0.3);border-radius:10px;border:1px solid rgba(244,67,54,0.3);}";
  html += ".color-picker h4{margin:0 0 15px 0;font-size:1em;color:#aaa;}";
  html += ".color-buttons{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;}";
  html += ".color-btn{width:50px;height:50px;border:3px solid transparent;border-radius:50%;cursor:pointer;transition:all 0.3s;box-shadow:0 2px 5px rgba(0,0,0,0.3);}";
  html += ".color-btn:hover{transform:scale(1.1);box-shadow:0 4px 10px rgba(0,0,0,0.5);}";
  html += ".color-btn.active{border-color:#fff;transform:scale(1.15);}";
  html += "</style>";
  html += "<script>";
  html += "let timeLeft=0,timerInterval=null,running=false,totalTime=0;";
  html += "function formatTime(seconds){";
  html += "let h=Math.floor(seconds/3600);";
  html += "let m=Math.floor((seconds%3600)/60);";
  html += "let s=seconds%60;";
  html += "return (h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(s<10?'0':'')+s;";
  html += "}";
  html += "function updateDisplay(){";
  html += "let display=document.getElementById('display');";
  html += "display.textContent=formatTime(timeLeft);";
  html += "display.className='display';";
  html += "if(timeLeft<=10&&timeLeft>5)display.className='display warning';";
  html += "else if(timeLeft<=5&&timeLeft>0)display.className='display danger';";
  html += "}";
  html += "function start(){";
  html += "if(!running&&timeLeft>0){";
  html += "fetch('/timercontrol?cmd=start&duration='+timeLeft);";
  html += "running=true;";
  html += "timerInterval=setInterval(()=>{";
  html += "timeLeft--;";
  html += "updateDisplay();";
  html += "if(timeLeft<=0){";
  html += "stop();";
  html += "alert('⏰ TEMPO SCADUTO!');";
  html += "}";
  html += "},1000);";
  html += "document.getElementById('startBtn').style.display='none';";
  html += "document.getElementById('stopBtn').style.display='inline-block';";
  html += "document.querySelectorAll('.input-group input').forEach(i=>i.disabled=true);";
  html += "}";
  html += "}";
  html += "function stop(){";
  html += "fetch('/timercontrol?cmd=stop');";
  html += "running=false;";
  html += "clearInterval(timerInterval);";
  html += "document.getElementById('startBtn').style.display='inline-block';";
  html += "document.getElementById('stopBtn').style.display='none';";
  html += "document.querySelectorAll('.input-group input').forEach(i=>i.disabled=false);";
  html += "}";
  html += "function reset(){";
  html += "fetch('/timercontrol?cmd=reset');";
  html += "stop();";
  html += "document.getElementById('hours').value='0';";
  html += "document.getElementById('minutes').value='0';";
  html += "document.getElementById('seconds').value='0';";
  html += "timeLeft=0;";
  html += "updateDisplay();";
  html += "}";
  html += "function setTime(){";
  html += "if(!running){";
  html += "let h=parseInt(document.getElementById('hours').value)||0;";
  html += "let m=parseInt(document.getElementById('minutes').value)||0;";
  html += "let s=parseInt(document.getElementById('seconds').value)||0;";
  html += "timeLeft=h*3600+m*60+s;";
  html += "totalTime=timeLeft;";
  html += "fetch('/timercontrol?cmd=set&duration='+timeLeft);";
  html += "updateDisplay();";
  html += "}";
  html += "}";
  html += "function setPreset(minutes){";
  html += "stop();";
  html += "document.getElementById('hours').value='0';";
  html += "document.getElementById('minutes').value=minutes;";
  html += "document.getElementById('seconds').value='0';";
  html += "setTime();";
  html += "}";
  html += "function setColor(mode){";
  html += "fetch('/timercontrol?cmd=color&mode='+mode);";
  html += "document.querySelectorAll('.color-btn').forEach(b=>b.classList.remove('active'));";
  html += "document.getElementById('color-'+mode).classList.add('active');";
  html += "}";
  html += "document.addEventListener('DOMContentLoaded',()=>{";
  html += "document.querySelectorAll('.input-group input').forEach(input=>{";
  html += "input.addEventListener('change',setTime);";
  html += "input.addEventListener('input',setTime);";
  html += "});";
  html += "});";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>⏲️ TIMER</h1>";
  html += "<div class='display' id='display'>00:00:00</div>";
  html += "<div class='color-picker'>";
  html += "<h4>🎨 Colore Display</h4>";
  html += "<div class='color-buttons'>";
  html += "<div class='color-btn' id='color-0' onclick='setColor(0)' style='background:#00c8ff;' title='Ciano'></div>";
  html += "<div class='color-btn' id='color-1' onclick='setColor(1)' style='background:#ff0000;' title='Rosso'></div>";
  html += "<div class='color-btn active' id='color-2' onclick='setColor(2)' style='background:#00ff00;' title='Verde'></div>";
  html += "<div class='color-btn' id='color-3' onclick='setColor(3)' style='background:#0000ff;' title='Blu'></div>";
  html += "<div class='color-btn' id='color-4' onclick='setColor(4)' style='background:#ffff00;' title='Giallo'></div>";
  html += "<div class='color-btn' id='color-5' onclick='setColor(5)' style='background:#ff00ff;' title='Magenta'></div>";
  html += "<div class='color-btn' id='color-6' onclick='setColor(6)' style='background:#ffffff;' title='Bianco'></div>";
  html += "<div class='color-btn' id='color-7' onclick='setColor(7)' style='background:#ffa500;' title='Arancione'></div>";
  html += "<div class='color-btn' id='color-8' onclick='setColor(8)' style='background:linear-gradient(90deg,#ff0000,#ff7f00,#ffff00,#00ff00,#0000ff,#4b0082,#9400d3);' title='Rainbow'></div>";
  html += "</div>";
  html += "</div>";
  html += "<div class='time-inputs'>";
  html += "<div class='input-group'><label>Ore</label><input type='number' id='hours' min='0' max='23' value='0'></div>";
  html += "<div class='input-group'><label>Minuti</label><input type='number' id='minutes' min='0' max='59' value='0'></div>";
  html += "<div class='input-group'><label>Secondi</label><input type='number' id='seconds' min='0' max='59' value='0'></div>";
  html += "</div>";
  html += "<div class='presets'>";
  html += "<button class='preset-btn' onclick='setPreset(1)'>1 min</button>";
  html += "<button class='preset-btn' onclick='setPreset(3)'>3 min</button>";
  html += "<button class='preset-btn' onclick='setPreset(5)'>5 min</button>";
  html += "<button class='preset-btn' onclick='setPreset(10)'>10 min</button>";
  html += "<button class='preset-btn' onclick='setPreset(15)'>15 min</button>";
  html += "<button class='preset-btn' onclick='setPreset(30)'>30 min</button>";
  html += "</div>";
  html += "<div class='controls'>";
  html += "<button id='startBtn' class='btn btn-start' onclick='start()'>▶️ Start</button>";
  html += "<button id='stopBtn' class='btn btn-stop' onclick='stop()' style='display:none;'>⏸️ Stop</button>";
  html += "<button class='btn btn-reset' onclick='reset()'>🔄 Reset</button>";
  html += "</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
  changeState(STATE_TIMER);
}

// ============================================
// CONTROLLI CRONOMETRO E TIMER
// ============================================
void handleStopwatchControl() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");

    if (cmd == "start") {
      if (!stopwatchRunning) {
        stopwatchStartTime = millis();
        stopwatchRunning = true;
        playBeep(); // Suono avvio cronometro
      }
    } else if (cmd == "stop") {
      if (stopwatchRunning) {
        stopwatchElapsedTime += millis() - stopwatchStartTime;
        stopwatchRunning = false;
        playBeep(); // Suono stop cronometro
      }
    } else if (cmd == "reset") {
      stopwatchRunning = false;
      stopwatchStartTime = 0;
      stopwatchElapsedTime = 0;
      playBeep(); // Suono reset cronometro
    } else if (cmd == "color" && server.hasArg("mode")) {
      stopwatchColorMode = server.arg("mode").toInt();
      if (stopwatchColorMode > 8) stopwatchColorMode = 0;
    }
  }

  server.send(200, "text/plain", "OK");
}

void handleTimerControl() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");

    if (cmd == "start" && server.hasArg("duration")) {
      unsigned long duration = server.arg("duration").toInt();
      timerDuration = duration * 1000; // Converti in millisecondi
      timerStartTime = millis();
      timerRunning = true;
      timerFinished = false;
      playBeep(); // Suono avvio timer
    } else if (cmd == "stop") {
      timerRunning = false;
      playBeep(); // Suono stop timer
    } else if (cmd == "reset") {
      timerRunning = false;
      timerDuration = 0;
      timerFinished = false;
      playBeep(); // Suono reset timer
    } else if (cmd == "set" && server.hasArg("duration")) {
      if (!timerRunning) {
        timerDuration = server.arg("duration").toInt() * 1000;
        timerFinished = false;
      }
    } else if (cmd == "color" && server.hasArg("mode")) {
      timerColorMode = server.arg("mode").toInt();
      if (timerColorMode > 8) timerColorMode = 0;
    }
  }

  server.send(200, "text/plain", "OK");
}

// ============================================
// SENSORE HTU21 - TEMPERATURA/UMIDITÀ
// ============================================

void readSensorData() {
  if (!sensorAvailable) {
    return;
  }

  // Leggi solo ogni 2 secondi per non sovraccaricare il sensore
  if (millis() - lastSensorRead < 2000) {
    return;
  }

  lastSensorRead = millis();
  currentTemperature = htu.readTemperature() + sensorTempOffset;
  currentHumidity = htu.readHumidity();

  // Verifica se i valori sono validi
  if (isnan(currentTemperature) || isnan(currentHumidity)) {
    Serial.println("Failed to read from HTU21D sensor!");
    sensorAvailable = false;
  }
}

void handleSensorData() {

  // Aggiorna dati sensore HTU21 ogni 5 secondi invece che ogni loop
  static unsigned long lastSensorReadLoop = 0;
  if (millis() - lastSensorReadLoop > 5000) {
    readSensorData();
    lastSensorReadLoop = millis();
  }

  String json = "{";
  json += "\"available\":" + String(sensorAvailable ? "true" : "false") + ",";
  json += "\"temperature\":" + String(currentTemperature, 1) + ",";
  json += "\"humidity\":" + String(currentHumidity, 1);
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================
// HANDLER WEB SVEGLIA (ALARM)
// ============================================

void handleAlarm() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<title>Sveglia ESP32</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#16213e;border-radius:15px;padding:30px;box-shadow:0 8px 32px rgba(0,0,0,0.3);}";
  html += "h1{text-align:center;color:#4ecca3;margin-bottom:30px;font-size:2em;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#4ecca3;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".form-group{margin-bottom:25px;}";
  html += "label{display:block;margin-bottom:8px;color:#4ecca3;font-weight:bold;}";
  html += "input[type='time'],input[type='number']{width:100%;padding:12px;background:#0f3460;border:2px solid #4ecca3;border-radius:8px;color:#fff;font-size:16px;box-sizing:border-box;}";
  html += ".checkbox-group{display:flex;flex-wrap:wrap;gap:10px;margin-top:10px;}";
  html += ".day-checkbox{background:#0f3460;padding:10px 15px;border-radius:8px;border:2px solid #555;cursor:pointer;user-select:none;transition:all 0.3s;}";
  html += ".day-checkbox.active{border-color:#4ecca3;background:#1a4d3e;}";
  html += ".ringtone-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:10px;}";
  html += ".ringtone-btn{background:#0f3460;border:2px solid #555;color:#fff;padding:15px;border-radius:8px;cursor:pointer;transition:all 0.3s;font-size:14px;}";
  html += ".ringtone-btn.active{border-color:#4ecca3;background:#1a4d3e;}";
  html += ".switch-container{display:flex;align-items:center;justify-content:space-between;background:#0f3460;padding:15px;border-radius:8px;}";
  html += ".switch{position:relative;width:60px;height:30px;}";
  html += ".switch input{opacity:0;width:0;height:0;}";
  html += ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#555;transition:.4s;border-radius:30px;}";
  html += ".slider:before{position:absolute;content:'';height:22px;width:22px;left:4px;bottom:4px;background:#fff;transition:.4s;border-radius:50%;}";
  html += "input:checked+.slider{background:#4ecca3;}";
  html += "input:checked+.slider:before{transform:translateX(30px);}";
  html += ".btn{background:#4ecca3;color:#16213e;border:none;padding:15px 30px;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:10px;}";
  html += ".btn-stop{background:#e74c3c;}";
  html += ".alarm-status{text-align:center;padding:15px;margin-bottom:20px;border-radius:8px;font-weight:bold;font-size:1.1em;}";
  html += ".alarm-status.active{background:#1a4d3e;color:#4ecca3;}";
  html += ".alarm-status.inactive{background:#3e1a1a;color:#e74c3c;}";
  html += "</style>";

  html += "<script>";
  html += "let selectedDays=" + String(alarmDays) + ";";
  html += "let selectedRingtone=" + String(alarmRingtone) + ";";
  html += "function toggleDay(bit){selectedDays^=(1<<bit);document.getElementById('day'+bit).classList.toggle('active');}";
  html += "function selectRingtone(idx){selectedRingtone=idx;document.querySelectorAll('.ringtone-btn').forEach(b=>b.classList.remove('active'));document.getElementById('ring'+idx).classList.add('active');}";
  html += "function saveAlarm(){let enabled=document.getElementById('alarmSwitch').checked?1:0;let time=document.getElementById('alarmTime').value.split(':');let duration=document.getElementById('alarmDuration').value;fetch('/setalarm?enabled='+enabled+'&hour='+time[0]+'&minute='+time[1]+'&days='+selectedDays+'&ringtone='+selectedRingtone+'&duration='+duration).then(r=>r.text()).then(d=>{location.reload();});}";
  html += "function stopAlarm(){fetch('/stopalarm').then(()=>{location.reload();});}";
  html += "function testRingtone(){fetch('/testalarm?ringtone='+selectedRingtone);}";
  html += "</script>";

  html += "</head><body>";
  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>⏰ SVEGLIA</h1>";

  // Status sveglia
  if (alarmRinging) {
    html += "<div class='alarm-status active'>🔔 SVEGLIA ATTIVA - STA SUONANDO!</div>";
    html += "<button class='btn btn-stop' onclick='stopAlarm()'>🛑 FERMA SVEGLIA</button>";
  } else if (alarmEnabled) {
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", alarmHour, alarmMinute);
    html += "<div class='alarm-status active'>✅ Sveglia attiva per le " + String(timeStr) + "</div>";
  } else {
    html += "<div class='alarm-status inactive'>❌ Sveglia disattivata</div>";
  }

  // Abilita/Disabilita sveglia
  html += "<div class='form-group'>";
  html += "<div class='switch-container'>";
  html += "<label style='margin:0;'>Abilita Sveglia</label>";
  html += "<label class='switch'>";
  html += "<input type='checkbox' id='alarmSwitch' " + String(alarmEnabled ? "checked" : "") + ">";
  html += "<span class='slider'></span>";
  html += "</label></div></div>";

  // Orario
  html += "<div class='form-group'>";
  html += "<label>⏰ Orario</label>";
  char timeValue[6];
  sprintf(timeValue, "%02d:%02d", alarmHour, alarmMinute);
  html += "<input type='time' id='alarmTime' value='" + String(timeValue) + "'>";
  html += "</div>";

  // Giorni settimana
  html += "<div class='form-group'>";
  html += "<label>📅 Giorni della Settimana</label>";
  html += "<div class='checkbox-group'>";
  const char* days[] = {"Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"};
  for (int i = 0; i < 7; i++) {
    bool isActive = alarmDays & (1 << i);
    html += "<div class='day-checkbox " + String(isActive ? "active" : "") + "' id='day" + String(i) + "' onclick='toggleDay(" + String(i) + ")'>";
    html += String(days[i]) + "</div>";
  }
  html += "</div></div>";

  // Suoneria
  html += "<div class='form-group'>";
  html += "<label>🎵 Suoneria</label>";
  html += "<div class='ringtone-grid'>";
  const char* ringtones[] = {"🍄 Mario", "⚔️ Zelda", "🧱 Tetris", "📱 Nokia", "⚡ Pokemon", "🌟 StarWars", "🪄 Potter", "🔔 Classic", "📢 Beep", "🖖 StarTrek", "🕰️ Back to the Future", "🎬 Indiana"};
  for (int i = 0; i < 12; i++) {
    bool isActive = (i == alarmRingtone);
    html += "<div class='ringtone-btn " + String(isActive ? "active" : "") + "' id='ring" + String(i) + "' onclick='selectRingtone(" + String(i) + ")'>";
    html += String(ringtones[i]) + "</div>";
  }
  html += "</div>";
  html += "<button class='btn' onclick='testRingtone()' style='margin-top:15px;background:#3498db;'>🎵 Prova Suoneria</button>";
  html += "</div>";

  // Durata
  html += "<div class='form-group'>";
  html += "<label>⏱️ Durata Suoneria (secondi)</label>";
  html += "<input type='number' id='alarmDuration' min='5' max='180' step='5' value='" + String(alarmDuration) + "'>";
  html += "</div>";

  // Pulsante salva
  html += "<button class='btn' onclick='saveAlarm()'>💾 SALVA SVEGLIA</button>";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleSetAlarm() {
  if (server.hasArg("enabled")) {
    alarmEnabled = server.arg("enabled").toInt() == 1;
  }
  if (server.hasArg("hour")) {
    alarmHour = server.arg("hour").toInt();
  }
  if (server.hasArg("minute")) {
    alarmMinute = server.arg("minute").toInt();
  }
  if (server.hasArg("days")) {
    alarmDays = server.arg("days").toInt();
  }
  if (server.hasArg("ringtone")) {
    alarmRingtone = server.arg("ringtone").toInt();
  }
  if (server.hasArg("duration")) {
    alarmDuration = server.arg("duration").toInt();
  }

  // Validazione
  if (alarmHour > 23) alarmHour = 0;
  if (alarmMinute > 59) alarmMinute = 0;
  if (alarmRingtone > 11) alarmRingtone = 0;
  if (alarmDuration < 5) alarmDuration = 5;
  if (alarmDuration > 180) alarmDuration = 180;

  // Salva in EEPROM
  saveConfig();

  server.send(200, "text/plain", "OK");
}

void handleStopAlarm() {
  alarmRinging = false;
  alarmStopRequested = false;  // Reset flag sincronizzazione
  alarmTriggeredToday = true; // Evita riattivazione
  stopMelody();  // Ferma la melodia in corso
  noTone(BUZZER_PIN);

  // Forza ridisegno immediato dell'orologio senza lampeggio
  forceRedraw = true;
  clearMatrix();

  server.send(200, "text/plain", "OK");
}

void handleTestAlarm() {
  if (server.hasArg("ringtone")) {
    uint8_t ringtone = server.arg("ringtone").toInt();
    if (ringtone <= 11) {
      playAlarmRingtone(ringtone);
    }
  }

  server.send(200, "text/plain", "OK");
}

// ============================================
// CALENDARIO EVENTI - FUNZIONI
// ============================================

void saveCalendarEvents() {
  File file = LittleFS.open(CALENDAR_FILE, "w");
  if (!file) {
    Serial.println("Failed to open calendar file for writing");
    return;
  }

  DynamicJsonDocument doc(8192);
  JsonArray events = doc.createNestedArray("events");

  for (int i = 0; i < calendarEventCount; i++) {
    if (calendarEvents[i].active) {
      JsonObject event = events.createNestedObject();
      event["day"] = calendarEvents[i].day;
      event["month"] = calendarEvents[i].month;
      event["year"] = calendarEvents[i].year;
      event["hour"] = calendarEvents[i].hour;
      event["minute"] = calendarEvents[i].minute;
      event["hour2"] = calendarEvents[i].hour2;
      event["minute2"] = calendarEvents[i].minute2;
      event["hour3"] = calendarEvents[i].hour3;
      event["minute3"] = calendarEvents[i].minute3;
      event["text"] = calendarEvents[i].text;
      event["repeatYearly"] = calendarEvents[i].repeatYearly;
      event["textSize"] = calendarEvents[i].textSize;
      event["textColor"] = calendarEvents[i].textColor;
    }
  }

  serializeJson(doc, file);
  file.close();
  Serial.println("Calendar events saved to LittleFS");
}

void loadCalendarEvents() {
  File file = LittleFS.open(CALENDAR_FILE, "r");
  if (!file) {
    Serial.println("No calendar file found, starting empty");
    calendarEventCount = 0;
    return;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("Failed to parse calendar file: ");
    Serial.println(error.c_str());
    calendarEventCount = 0;
    return;
  }

  JsonArray events = doc["events"];
  calendarEventCount = 0;

  for (JsonObject event : events) {
    if (calendarEventCount >= MAX_CALENDAR_EVENTS) break;

    calendarEvents[calendarEventCount].active = true;
    calendarEvents[calendarEventCount].day = event["day"];
    calendarEvents[calendarEventCount].month = event["month"];
    calendarEvents[calendarEventCount].year = event["year"];
    calendarEvents[calendarEventCount].hour = event["hour"];
    calendarEvents[calendarEventCount].minute = event["minute"];
    calendarEvents[calendarEventCount].hour2 = event["hour2"] | 255;  // 255 = disabilitato
    calendarEvents[calendarEventCount].minute2 = event["minute2"] | 0;
    calendarEvents[calendarEventCount].hour3 = event["hour3"] | 255;  // 255 = disabilitato
    calendarEvents[calendarEventCount].minute3 = event["minute3"] | 0;
    strlcpy(calendarEvents[calendarEventCount].text, event["text"] | "", sizeof(calendarEvents[calendarEventCount].text));
    calendarEvents[calendarEventCount].repeatYearly = event["repeatYearly"] | false;
    calendarEvents[calendarEventCount].textSize = event["textSize"] | 0;
    calendarEvents[calendarEventCount].textColor = event["textColor"] | 7;  // Default arancione
    calendarEvents[calendarEventCount].notificationShown = false;
    calendarEvents[calendarEventCount].notificationShown2 = false;
    calendarEvents[calendarEventCount].notificationShown3 = false;

    calendarEventCount++;
  }

  Serial.print("Loaded ");
  Serial.print(calendarEventCount);
  Serial.println(" calendar events");
}

void checkCalendarEvents() {
  if (!wifiConnected || timeStatus() != timeSet) return;

  // Se c'è già un evento attivo, controlla se è finito
  if (calendarEventActive) {
    if (millis() - calendarEventStartTime > calendarEventDuration) {
      // Evento finito, torna allo stato precedente
      calendarEventActive = false;
      currentCalendarEventIndex = -1;
      changeState(stateBeforeCalendarEvent);
      forceRedraw = true;
    }
    return;
  }

  // Ottieni data/ora corrente
  int currentDay = myTZ.day();
  int currentMonth = myTZ.month();
  int currentYear = myTZ.year();
  int currentHour = myTZ.hour();
  int currentMinute = myTZ.minute();

  // Controlla ogni evento
  for (int i = 0; i < calendarEventCount; i++) {
    if (!calendarEvents[i].active) continue;

    bool dateMatch = false;

    // Controllo data
    if (calendarEvents[i].repeatYearly) {
      // Evento annuale: controlla solo giorno e mese
      dateMatch = (calendarEvents[i].day == currentDay &&
                   calendarEvents[i].month == currentMonth);
    } else {
      // Evento singolo: controlla giorno, mese e anno
      dateMatch = (calendarEvents[i].day == currentDay &&
                   calendarEvents[i].month == currentMonth &&
                   calendarEvents[i].year == currentYear);
    }

    if (!dateMatch) continue;

    // Controllo orario 1 (principale)
    if (!calendarEvents[i].notificationShown &&
        calendarEvents[i].hour == currentHour &&
        calendarEvents[i].minute == currentMinute) {
      calendarEvents[i].notificationShown = true;
      triggerCalendarEvent(i);
      break;
    }

    // Controllo orario 2 (se abilitato)
    if (!calendarEvents[i].notificationShown2 &&
        calendarEvents[i].hour2 != 255 &&
        calendarEvents[i].hour2 == currentHour &&
        calendarEvents[i].minute2 == currentMinute) {
      calendarEvents[i].notificationShown2 = true;
      triggerCalendarEvent(i);
      break;
    }

    // Controllo orario 3 (se abilitato)
    if (!calendarEvents[i].notificationShown3 &&
        calendarEvents[i].hour3 != 255 &&
        calendarEvents[i].hour3 == currentHour &&
        calendarEvents[i].minute3 == currentMinute) {
      calendarEvents[i].notificationShown3 = true;
      triggerCalendarEvent(i);
      break;
    }
  }

  // Reset notificationShown a mezzanotte
  static int lastResetDay = -1;
  if (currentDay != lastResetDay && currentHour == 0 && currentMinute == 0) {
    for (int i = 0; i < calendarEventCount; i++) {
      calendarEvents[i].notificationShown = false;
      calendarEvents[i].notificationShown2 = false;
      calendarEvents[i].notificationShown3 = false;
    }
    lastResetDay = currentDay;
  }
}

// Funzione helper per attivare un evento calendario
void triggerCalendarEvent(int index) {
  calendarEventActive = true;
  currentCalendarEventIndex = index;
  calendarEventStartTime = millis();

  // Salva lo stato corrente e passa alla visualizzazione evento
  stateBeforeCalendarEvent = currentState;
  changeState(STATE_CALENDAR_EVENT);

  // Suona notifica
  playSuccess();

  Serial.print("Calendar event triggered: ");
  Serial.println(calendarEvents[index].text);
}

// Helper per ottenere il colore dell'evento calendario
CRGB getCalendarEventColor(int colorIndex, int charIndex) {
  static unsigned long calRainbowOffset = 0;

  // Modalità rainbow: colore animato per ogni carattere
  if (colorIndex == 8) {
    calRainbowOffset = (calRainbowOffset + 1) % 256;
    uint8_t hue = (calRainbowOffset + charIndex * 20) % 256;
    return gamma32(ColorHSV_NeoPixel(hue * 256));
  }

  // Colori fissi
  switch (colorIndex) {
    case 0: return CRGB(255, 0, 0);     // Rosso
    case 1: return CRGB(0, 255, 0);     // Verde
    case 2: return CRGB(0, 0, 255);     // Blu
    case 3: return CRGB(255, 255, 0);   // Giallo
    case 4: return CRGB(0, 255, 255);   // Ciano
    case 5: return CRGB(255, 0, 255);   // Magenta
    case 6: return CRGB(255, 255, 255); // Bianco
    case 7: return CRGB(255, 128, 0);   // Arancione
    default: return CRGB(255, 128, 0);  // Default arancione
  }
}

void drawCalendarEvent() {
  if (currentCalendarEventIndex < 0 || currentCalendarEventIndex >= calendarEventCount) {
    return;
  }

  static int calScrollPos = MATRIX_WIDTH;
  static unsigned long lastCalScrollUpdate = 0;

  // Ottieni le impostazioni dell'evento
  uint8_t textSize = calendarEvents[currentCalendarEventIndex].textSize;
  uint8_t textColorIdx = calendarEvents[currentCalendarEventIndex].textColor;

  // Calcola dimensioni in base alla scelta
  int charWidth, charHeight, charY;
  if (textSize == 0) {
    // Piccolo: 3x5
    charWidth = 4;
    charHeight = 5;
    charY = 5;
  } else if (textSize == 1) {
    // Medio: 5x7
    charWidth = 6;
    charHeight = 7;
    charY = 4;
  } else {
    // Grande: 10x14
    charWidth = 12;
    charHeight = 14;
    charY = 1;
  }

  if (millis() - lastCalScrollUpdate > 60) {
    lastCalScrollUpdate = millis();
    calScrollPos--;

    String eventText = calendarEvents[currentCalendarEventIndex].text;
    int textLen = eventText.length();

    // Reset quando il testo è completamente uscito
    if (calScrollPos < -textLen * charWidth) {
      calScrollPos = MATRIX_WIDTH;
    }

    clearMatrixNoShow();

    // Disegna ogni carattere
    for (int i = 0; i < textLen; i++) {
      int charX = calScrollPos + (i * charWidth);
      if (charX > -charWidth && charX < MATRIX_WIDTH) {
        CRGB eventColor = getCalendarEventColor(textColorIdx, i);

        if (textSize == 0) {
          // Font piccolo 3x5
          drawCharacter(eventText.charAt(i), charX, charY, eventColor);
        } else if (textSize == 1) {
          // Font medio 5x7
          drawChar5x7(eventText.charAt(i), charX, charY, eventColor);
        } else {
          // Font grande 10x14 (scaling 2x)
          drawChar5x7Scaled2x(eventText.charAt(i), charX, charY, eventColor);
        }
      }
    }

    // Icona calendario in alto (solo per font piccolo/medio)
    if (textSize < 2) {
      setPixel(7, 0, CRGB(255, 0, 0));
      setPixel(8, 0, CRGB(255, 0, 0));
      setPixel(7, 1, CRGB(255, 255, 255));
      setPixel(8, 1, CRGB(255, 255, 255));
    }

    FastLED.show();
  }
}

void handleCalendar() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<title>Calendario - CONSOLE QUADRA</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;}";
  html += ".container{max-width:700px;margin:0 auto;background:#16213e;border-radius:15px;padding:30px;box-shadow:0 8px 32px rgba(0,0,0,0.3);}";
  html += "h1{text-align:center;color:#FF9800;margin-bottom:30px;font-size:2em;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#FF9800;text-decoration:none;font-size:1.2em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".form-group{margin-bottom:20px;}";
  html += "label{display:block;margin-bottom:8px;color:#FF9800;font-weight:bold;}";
  html += "input[type='text'],input[type='number'],select{width:100%;padding:12px;background:#0f3460;border:2px solid #FF9800;border-radius:8px;color:#fff;font-size:16px;box-sizing:border-box;}";
  html += ".form-row{display:grid;grid-template-columns:1fr 1fr;gap:15px;}";
  html += ".form-row-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:15px;}";
  html += ".time-section{background:#0f3460;padding:15px;border-radius:10px;margin-bottom:15px;}";
  html += ".time-section h4{color:#aaa;margin:0 0 10px 0;font-size:0.9em;}";
  html += ".checkbox-group{display:flex;align-items:center;gap:10px;margin:10px 0;}";
  html += ".checkbox-group input{width:auto;}";
  html += ".btn{background:#FF9800;color:#16213e;border:none;padding:15px 30px;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:10px;}";
  html += ".btn:hover{background:#F57C00;}";
  html += ".btn-danger{background:#e74c3c;color:#fff;padding:8px 15px;font-size:0.9em;width:auto;}";
  html += ".events-list{margin-top:20px;}";
  html += ".event-item{background:#0f3460;padding:15px;border-radius:10px;margin-bottom:10px;border-left:4px solid #FF9800;}";
  html += ".event-header{display:flex;justify-content:space-between;align-items:flex-start;}";
  html += ".event-info h4{margin:0 0 5px 0;color:#FF9800;}";
  html += ".event-info p{margin:2px 0;color:#aaa;font-size:0.85em;}";
  html += ".color-preview{display:inline-block;width:20px;height:20px;border-radius:4px;vertical-align:middle;margin-right:5px;border:1px solid #fff;}";
  html += ".section-title{color:#FF9800;margin:25px 0 15px 0;padding-bottom:10px;border-bottom:1px solid #333;}";
  html += ".btn-edit,.btn-del{border:none;padding:8px 12px;border-radius:6px;cursor:pointer;font-size:14px;}";
  html += ".btn-edit{background:#4CAF50;color:#fff;}";
  html += ".btn-del{background:#e74c3c;color:#fff;}";
  html += ".btn-edit:hover{background:#45a049;}";
  html += ".btn-del:hover{background:#c0392b;}";
  html += "</style></head><body>";

  html += "<div class='nav'><a href='/'>🏠 Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>📅 CALENDARIO EVENTI</h1>";

  // Form per nuovo evento
  html += "<form id='eventForm'>";

  // Testo
  html += "<div class='form-group'>";
  html += "<label>📝 Testo da visualizzare (max 47 caratteri)</label>";
  html += "<input type='text' id='eventText' maxlength='47' placeholder='Es: Compleanno Mario!' required>";
  html += "</div>";

  // Data
  html += "<h3 class='section-title'>📆 Data</h3>";
  html += "<div class='form-row-3'>";
  html += "<div class='form-group'><label>Giorno</label><input type='number' id='eventDay' min='1' max='31' required></div>";
  html += "<div class='form-group'><label>Mese</label><select id='eventMonth' required>";
  html += "<option value='1'>Gennaio</option><option value='2'>Febbraio</option><option value='3'>Marzo</option>";
  html += "<option value='4'>Aprile</option><option value='5'>Maggio</option><option value='6'>Giugno</option>";
  html += "<option value='7'>Luglio</option><option value='8'>Agosto</option><option value='9'>Settembre</option>";
  html += "<option value='10'>Ottobre</option><option value='11'>Novembre</option><option value='12'>Dicembre</option>";
  html += "</select></div>";
  html += "<div class='form-group'><label>Anno</label><input type='number' id='eventYear' min='2024' max='2099' value='" + String(myTZ.year()) + "' required></div>";
  html += "</div>";

  html += "<div class='checkbox-group'>";
  html += "<input type='checkbox' id='eventRepeat'>";
  html += "<label for='eventRepeat' style='color:#aaa;font-weight:normal;'>🔄 Ripeti ogni anno (compleanni, anniversari)</label>";
  html += "</div>";

  // Orari
  html += "<h3 class='section-title'>⏰ Orari Notifica (fino a 3 orari al giorno)</h3>";

  // Orario 1 (principale)
  html += "<div class='time-section'>";
  html += "<h4>Orario 1 (obbligatorio)</h4>";
  html += "<div class='form-row'>";
  html += "<div class='form-group'><label>Ora</label><input type='number' id='eventHour' min='0' max='23' required></div>";
  html += "<div class='form-group'><label>Minuto</label><input type='number' id='eventMinute' min='0' max='59' required></div>";
  html += "</div></div>";

  // Orario 2 (opzionale)
  html += "<div class='time-section'>";
  html += "<div class='checkbox-group'>";
  html += "<input type='checkbox' id='enableTime2' onchange='toggleTime2()'>";
  html += "<label for='enableTime2' style='color:#aaa;font-weight:normal;'>Orario 2 (opzionale)</label>";
  html += "</div>";
  html += "<div id='time2Fields' style='display:none;'>";
  html += "<div class='form-row'>";
  html += "<div class='form-group'><label>Ora</label><input type='number' id='eventHour2' min='0' max='23' value='12'></div>";
  html += "<div class='form-group'><label>Minuto</label><input type='number' id='eventMinute2' min='0' max='59' value='0'></div>";
  html += "</div></div></div>";

  // Orario 3 (opzionale)
  html += "<div class='time-section'>";
  html += "<div class='checkbox-group'>";
  html += "<input type='checkbox' id='enableTime3' onchange='toggleTime3()'>";
  html += "<label for='enableTime3' style='color:#aaa;font-weight:normal;'>Orario 3 (opzionale)</label>";
  html += "</div>";
  html += "<div id='time3Fields' style='display:none;'>";
  html += "<div class='form-row'>";
  html += "<div class='form-group'><label>Ora</label><input type='number' id='eventHour3' min='0' max='23' value='18'></div>";
  html += "<div class='form-group'><label>Minuto</label><input type='number' id='eventMinute3' min='0' max='59' value='0'></div>";
  html += "</div></div></div>";

  // Aspetto testo
  html += "<h3 class='section-title'>🎨 Aspetto Testo</h3>";
  html += "<div class='form-row'>";

  // Dimensione
  html += "<div class='form-group'><label>Dimensione</label>";
  html += "<select id='eventSize'>";
  html += "<option value='0'>Piccolo (3x5)</option>";
  html += "<option value='1'>Medio (5x7)</option>";
  html += "<option value='2'>Grande (10x14)</option>";
  html += "</select></div>";

  // Colore
  html += "<div class='form-group'><label>Colore</label>";
  html += "<select id='eventColor'>";
  html += "<option value='0'>🔴 Rosso</option>";
  html += "<option value='1'>🟢 Verde</option>";
  html += "<option value='2'>🔵 Blu</option>";
  html += "<option value='3'>🟡 Giallo</option>";
  html += "<option value='4'>🩵 Ciano</option>";
  html += "<option value='5'>🟣 Magenta</option>";
  html += "<option value='6'>⚪ Bianco</option>";
  html += "<option value='7' selected>🟠 Arancione</option>";
  html += "<option value='8'>🌈 Rainbow</option>";
  html += "</select></div>";
  html += "</div>";

  html += "<div style='display:flex;gap:10px;'>";
  html += "<button type='button' class='btn' id='btnSave' onclick='saveEvent()'>💾 Salva Evento</button>";
  html += "<button type='button' class='btn' id='btnCancel' onclick='cancelEdit()' style='display:none;background:#666;'>❌ Annulla</button>";
  html += "</div>";
  html += "</form>";

  // Lista eventi esistenti
  html += "<h3 class='section-title'>📋 Eventi Salvati (" + String(calendarEventCount) + "/" + String(MAX_CALENDAR_EVENTS) + ")</h3>";
  html += "<div class='events-list' id='eventsList'></div>";

  html += "</div>"; // container

  // JavaScript
  html += "<script>";
  html += "const colorNames=['Rosso','Verde','Blu','Giallo','Ciano','Magenta','Bianco','Arancione','Rainbow'];";
  html += "const colorHex=['#ff0000','#00ff00','#0000ff','#ffff00','#00ffff','#ff00ff','#ffffff','#ff8000','linear-gradient(90deg,red,orange,yellow,green,blue,violet)'];";
  html += "const sizeNames=['Piccolo','Medio','Grande'];";
  html += "let editIndex=-1;";  // -1 = nuovo evento, >=0 = modifica

  html += "function toggleTime2(){document.getElementById('time2Fields').style.display=document.getElementById('enableTime2').checked?'block':'none';}";
  html += "function toggleTime3(){document.getElementById('time3Fields').style.display=document.getElementById('enableTime3').checked?'block':'none';}";

  html += "function loadEvents(){";
  html += "fetch('/calendarlist').then(r=>r.json()).then(data=>{";
  html += "let html='';";
  html += "data.events.forEach((e,i)=>{";
  html += "let dateStr=e.day+'/'+e.month+(e.repeatYearly?' (ogni anno)':'/'+e.year);";
  html += "let t1=String(e.hour).padStart(2,'0')+':'+String(e.minute).padStart(2,'0');";
  html += "let times=t1;";
  html += "if(e.hour2!=255)times+=', '+String(e.hour2).padStart(2,'0')+':'+String(e.minute2).padStart(2,'0');";
  html += "if(e.hour3!=255)times+=', '+String(e.hour3).padStart(2,'0')+':'+String(e.minute3).padStart(2,'0');";
  html += "let colStyle=e.textColor==8?'background:'+colorHex[8]:'background:'+colorHex[e.textColor];";
  html += "html+='<div class=\"event-item\" id=\"ev'+i+'\"><div class=\"event-header\"><div class=\"event-info\">';";
  html += "html+='<h4>'+e.text+'</h4>';";
  html += "html+='<p>📆 '+dateStr+'</p>';";
  html += "html+='<p>⏰ '+times+'</p>';";
  html += "html+='<p><span class=\"color-preview\" style=\"'+colStyle+'\"></span>'+colorNames[e.textColor]+' | '+sizeNames[e.textSize]+'</p>';";
  html += "html+='</div><div style=\"display:flex;flex-direction:column;gap:5px;\">';";
  html += "html+='<button class=\"btn-edit\" onclick=\"editEvent('+i+')\">✏️</button>';";
  html += "html+='<button class=\"btn-del\" onclick=\"deleteEvent('+i+')\">🗑️</button>';";
  html += "html+='</div></div></div>';";
  html += "});";
  html += "if(data.events.length===0)html='<p style=\"color:#666;text-align:center;\">Nessun evento salvato</p>';";
  html += "document.getElementById('eventsList').innerHTML=html;";
  html += "window.eventsData=data.events;";
  html += "});";
  html += "}";

  html += "function editEvent(index){";
  html += "let e=window.eventsData[index];";
  html += "editIndex=index;";
  html += "document.getElementById('eventText').value=e.text;";
  html += "document.getElementById('eventDay').value=e.day;";
  html += "document.getElementById('eventMonth').value=e.month;";
  html += "document.getElementById('eventYear').value=e.year;";
  html += "document.getElementById('eventHour').value=e.hour;";
  html += "document.getElementById('eventMinute').value=e.minute;";
  html += "document.getElementById('eventRepeat').checked=e.repeatYearly;";
  html += "document.getElementById('eventSize').value=e.textSize;";
  html += "document.getElementById('eventColor').value=e.textColor;";
  html += "if(e.hour2!=255){document.getElementById('enableTime2').checked=true;document.getElementById('eventHour2').value=e.hour2;document.getElementById('eventMinute2').value=e.minute2;toggleTime2();}";
  html += "if(e.hour3!=255){document.getElementById('enableTime3').checked=true;document.getElementById('eventHour3').value=e.hour3;document.getElementById('eventMinute3').value=e.minute3;toggleTime3();}";
  html += "document.getElementById('btnSave').innerHTML='💾 Aggiorna Evento';";
  html += "document.getElementById('btnCancel').style.display='block';";
  html += "document.querySelectorAll('.event-item').forEach(el=>el.style.borderLeftColor='#FF9800');";
  html += "document.getElementById('ev'+index).style.borderLeftColor='#4CAF50';";
  html += "window.scrollTo({top:0,behavior:'smooth'});";
  html += "}";

  html += "function cancelEdit(){";
  html += "editIndex=-1;";
  html += "document.getElementById('eventForm').reset();";
  html += "document.getElementById('eventYear').value='" + String(myTZ.year()) + "';";
  html += "document.getElementById('time2Fields').style.display='none';";
  html += "document.getElementById('time3Fields').style.display='none';";
  html += "document.getElementById('enableTime2').checked=false;";
  html += "document.getElementById('enableTime3').checked=false;";
  html += "document.getElementById('btnSave').innerHTML='💾 Salva Evento';";
  html += "document.getElementById('btnCancel').style.display='none';";
  html += "document.querySelectorAll('.event-item').forEach(el=>el.style.borderLeftColor='#FF9800');";
  html += "}";

  html += "function deleteEvent(index){";
  html += "fetch('/calendardelete?index='+index).then(()=>{if(editIndex==index)cancelEdit();loadEvents();});";
  html += "}";

  html += "function saveEvent(){";
  html += "let text=document.getElementById('eventText').value;";
  html += "let day=document.getElementById('eventDay').value;";
  html += "let hour=document.getElementById('eventHour').value;";
  html += "let minute=document.getElementById('eventMinute').value;";
  html += "if(!text||!day||hour===''||minute==='')return;";
  html += "let params=new URLSearchParams({";
  html += "text:text,";
  html += "day:day,";
  html += "month:document.getElementById('eventMonth').value,";
  html += "year:document.getElementById('eventYear').value,";
  html += "hour:hour,";
  html += "minute:minute,";
  html += "hour2:document.getElementById('enableTime2').checked?document.getElementById('eventHour2').value:'255',";
  html += "minute2:document.getElementById('enableTime2').checked?document.getElementById('eventMinute2').value:'0',";
  html += "hour3:document.getElementById('enableTime3').checked?document.getElementById('eventHour3').value:'255',";
  html += "minute3:document.getElementById('enableTime3').checked?document.getElementById('eventMinute3').value:'0',";
  html += "repeat:document.getElementById('eventRepeat').checked?'1':'0',";
  html += "size:document.getElementById('eventSize').value,";
  html += "color:document.getElementById('eventColor').value";
  html += "});";
  html += "let url=editIndex>=0?'/calendarupdate?index='+editIndex+'&'+params.toString():'/calendarsave?'+params.toString();";
  html += "fetch(url).then(r=>r.json()).then(data=>{";
  html += "if(data.success){cancelEdit();loadEvents();}";
  html += "});";
  html += "}";

  html += "document.getElementById('eventForm').addEventListener('submit',function(e){e.preventDefault();});";
  html += "loadEvents();";
  html += "</script>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleCalendarSave() {
  if (calendarEventCount >= MAX_CALENDAR_EVENTS) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Limite eventi raggiunto\"}");
    return;
  }

  if (!server.hasArg("text") || !server.hasArg("day") || !server.hasArg("month") ||
      !server.hasArg("year") || !server.hasArg("hour") || !server.hasArg("minute")) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Parametri mancanti\"}");
    return;
  }

  CalendarEvent newEvent;
  newEvent.active = true;
  newEvent.day = server.arg("day").toInt();
  newEvent.month = server.arg("month").toInt();
  newEvent.year = server.arg("year").toInt();
  newEvent.hour = server.arg("hour").toInt();
  newEvent.minute = server.arg("minute").toInt();

  // Orari opzionali (255 = disabilitato)
  newEvent.hour2 = server.hasArg("hour2") ? server.arg("hour2").toInt() : 255;
  newEvent.minute2 = server.hasArg("minute2") ? server.arg("minute2").toInt() : 0;
  newEvent.hour3 = server.hasArg("hour3") ? server.arg("hour3").toInt() : 255;
  newEvent.minute3 = server.hasArg("minute3") ? server.arg("minute3").toInt() : 0;

  strlcpy(newEvent.text, server.arg("text").c_str(), sizeof(newEvent.text));
  newEvent.repeatYearly = (server.arg("repeat") == "1");

  // Aspetto testo
  newEvent.textSize = server.hasArg("size") ? server.arg("size").toInt() : 0;
  newEvent.textColor = server.hasArg("color") ? server.arg("color").toInt() : 7; // Default arancione

  newEvent.notificationShown = false;
  newEvent.notificationShown2 = false;
  newEvent.notificationShown3 = false;

  // Validazione
  if (newEvent.day < 1 || newEvent.day > 31 ||
      newEvent.month < 1 || newEvent.month > 12 ||
      newEvent.hour > 23 || newEvent.minute > 59) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Valori non validi\"}");
    return;
  }

  // Validazione orari opzionali (se abilitati)
  if (newEvent.hour2 != 255 && (newEvent.hour2 > 23 || newEvent.minute2 > 59)) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Orario 2 non valido\"}");
    return;
  }
  if (newEvent.hour3 != 255 && (newEvent.hour3 > 23 || newEvent.minute3 > 59)) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Orario 3 non valido\"}");
    return;
  }

  calendarEvents[calendarEventCount] = newEvent;
  calendarEventCount++;
  saveCalendarEvents();

  server.send(200, "application/json", "{\"success\":true}");
}

void handleCalendarUpdate() {
  if (!server.hasArg("index")) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Indice mancante\"}");
    return;
  }

  int index = server.arg("index").toInt();
  if (index < 0 || index >= calendarEventCount) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Indice non valido\"}");
    return;
  }

  if (!server.hasArg("text") || !server.hasArg("day") || !server.hasArg("month") ||
      !server.hasArg("year") || !server.hasArg("hour") || !server.hasArg("minute")) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Parametri mancanti\"}");
    return;
  }

  // Aggiorna l'evento esistente
  calendarEvents[index].day = server.arg("day").toInt();
  calendarEvents[index].month = server.arg("month").toInt();
  calendarEvents[index].year = server.arg("year").toInt();
  calendarEvents[index].hour = server.arg("hour").toInt();
  calendarEvents[index].minute = server.arg("minute").toInt();
  calendarEvents[index].hour2 = server.hasArg("hour2") ? server.arg("hour2").toInt() : 255;
  calendarEvents[index].minute2 = server.hasArg("minute2") ? server.arg("minute2").toInt() : 0;
  calendarEvents[index].hour3 = server.hasArg("hour3") ? server.arg("hour3").toInt() : 255;
  calendarEvents[index].minute3 = server.hasArg("minute3") ? server.arg("minute3").toInt() : 0;
  strlcpy(calendarEvents[index].text, server.arg("text").c_str(), sizeof(calendarEvents[index].text));
  calendarEvents[index].repeatYearly = (server.arg("repeat") == "1");
  calendarEvents[index].textSize = server.hasArg("size") ? server.arg("size").toInt() : 0;
  calendarEvents[index].textColor = server.hasArg("color") ? server.arg("color").toInt() : 7;

  // Reset notifiche (l'evento modificato potrebbe avere orari diversi)
  calendarEvents[index].notificationShown = false;
  calendarEvents[index].notificationShown2 = false;
  calendarEvents[index].notificationShown3 = false;

  // Validazione
  if (calendarEvents[index].day < 1 || calendarEvents[index].day > 31 ||
      calendarEvents[index].month < 1 || calendarEvents[index].month > 12 ||
      calendarEvents[index].hour > 23 || calendarEvents[index].minute > 59) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Valori non validi\"}");
    return;
  }

  saveCalendarEvents();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleCalendarDelete() {
  if (!server.hasArg("index")) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Indice mancante\"}");
    return;
  }

  int index = server.arg("index").toInt();
  if (index < 0 || index >= calendarEventCount) {
    server.send(200, "application/json", "{\"success\":false,\"error\":\"Indice non valido\"}");
    return;
  }

  // Sposta tutti gli eventi successivi
  for (int i = index; i < calendarEventCount - 1; i++) {
    calendarEvents[i] = calendarEvents[i + 1];
  }
  calendarEventCount--;

  saveCalendarEvents();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleCalendarList() {
  DynamicJsonDocument doc(6144);
  JsonArray events = doc.createNestedArray("events");

  for (int i = 0; i < calendarEventCount; i++) {
    if (calendarEvents[i].active) {
      JsonObject event = events.createNestedObject();
      event["day"] = calendarEvents[i].day;
      event["month"] = calendarEvents[i].month;
      event["year"] = calendarEvents[i].year;
      event["hour"] = calendarEvents[i].hour;
      event["minute"] = calendarEvents[i].minute;
      event["hour2"] = calendarEvents[i].hour2;
      event["minute2"] = calendarEvents[i].minute2;
      event["hour3"] = calendarEvents[i].hour3;
      event["minute3"] = calendarEvents[i].minute3;
      event["text"] = calendarEvents[i].text;
      event["repeatYearly"] = calendarEvents[i].repeatYearly;
      event["textSize"] = calendarEvents[i].textSize;
      event["textColor"] = calendarEvents[i].textColor;
    }
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

// ============================================
// FUNZIONI DI AGGIORNAMENTO E DISEGNO
// ============================================
void updateStopwatch() {
  // Aggiornamento automatico, il disegno viene fatto in drawStopwatch()
}

void updateTimer() {
  if (timerRunning && !timerFinished) {
    unsigned long elapsed = millis() - timerStartTime;
    if (elapsed >= timerDuration) {
      timerRunning = false;
      timerFinished = true;
      playLevelUp(); // Suono allarme timer finito
      // Effetto visivo di fine timer (lampeggio)
    }
  }
}

// Font 3x5 per cifre piccole
const byte digit3x5[10][5] = {
  {0x7, 0x5, 0x5, 0x5, 0x7}, // 0
  {0x2, 0x6, 0x2, 0x2, 0x7}, // 1
  {0x7, 0x1, 0x7, 0x4, 0x7}, // 2
  {0x7, 0x1, 0x3, 0x1, 0x7}, // 3
  {0x5, 0x5, 0x7, 0x1, 0x1}, // 4
  {0x7, 0x4, 0x7, 0x1, 0x7}, // 5
  {0x7, 0x4, 0x7, 0x5, 0x7}, // 6
  {0x7, 0x1, 0x2, 0x2, 0x2}, // 7 - con stanghetta diagonale
  {0x7, 0x5, 0x7, 0x5, 0x7}, // 8
  {0x7, 0x5, 0x7, 0x1, 0x7}  // 9
};

void drawSmallDigit3x5(int digit, int x, int y, CRGB color) {
  if (digit < 0 || digit > 9) return;

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (digit3x5[digit][row] & (1 << (2 - col))) {
        setPixel(x + col, y + row, color);
      }
    }
  }
}

// Funzione per ottenere il colore in base alla modalità
CRGB getColorFromMode(uint8_t colorMode, unsigned long offset = 0) {
  switch (colorMode) {
    case 0: return CRGB(0, 200, 255);     // Ciano
    case 1: return CRGB(255, 0, 0);       // Rosso
    case 2: return CRGB(0, 255, 0);       // Verde
    case 3: return CRGB(0, 0, 255);       // Blu
    case 4: return CRGB(255, 255, 0);     // Giallo
    case 5: return CRGB(255, 0, 255);     // Magenta
    case 6: return CRGB(255, 255, 255);   // Bianco
    case 7: return CRGB(255, 165, 0);     // Arancione
    case 8: // Rainbow
      {
        unsigned long hue = (millis() / 20 + offset) % 360;
        return ColorHSV_NeoPixel((hue * 65536) / 360, 255, 255);
      }
    default: return CRGB(255, 255, 255); // Default bianco
  }
}

void drawStopwatch() {
  clearMatrixNoShow();

  unsigned long totalTime;
  if (stopwatchRunning) {
    totalTime = stopwatchElapsedTime + (millis() - stopwatchStartTime);
  } else {
    totalTime = stopwatchElapsedTime;
  }

  // Converti in minuti e secondi
  unsigned long totalSeconds = totalTime / 1000;
  int minutes = (totalSeconds / 60) % 100; // Max 99 minuti
  int seconds = totalSeconds % 60;

  // Ottieni il colore in base alla modalità selezionata
  CRGB color = getColorFromMode(stopwatchColorMode);

  int startX = 0;
  int startY = 6; // Centrato verticalmente

  // Decine minuti
  drawSmallDigit3x5(minutes / 10, startX, startY, color);
  // Unità minuti
  drawSmallDigit3x5(minutes % 10, startX + 4, startY, color);

  // Due punti (separatore) - LAMPEGGIANTI ogni secondo solo quando il cronometro è attivo
  if (stopwatchRunning) {
    bool showColon = (millis() % 1000) < 500;
    if (showColon) {
      setPixel(startX + 7, startY + 1, color);
      setPixel(startX + 7, startY + 3, color);
    }
  } else {
    // Quando è fermo, mostra i due punti fissi
    setPixel(startX + 7, startY + 1, color);
    setPixel(startX + 7, startY + 3, color);
  }

  // Decine secondi
  drawSmallDigit3x5(seconds / 10, startX + 9, startY, color);
  // Unità secondi
  drawSmallDigit3x5(seconds % 10, startX + 13, startY, color);

  FastLED.show();
}

void drawTimer() {
  clearMatrixNoShow();

  unsigned long timeLeft = 0;

  if (timerRunning && !timerFinished) {
    unsigned long elapsed = millis() - timerStartTime;
    if (elapsed < timerDuration) {
      timeLeft = (timerDuration - elapsed) / 1000;
    }
  } else if (!timerRunning && !timerFinished && timerDuration > 0) {
    timeLeft = timerDuration / 1000;
  }

  int minutes = (timeLeft / 60) % 100;
  int seconds = timeLeft % 60;

  // Colore base dalla modalità selezionata
  CRGB color;

  if (timerFinished) {
    // Rosso lampeggiante quando finito (ignora la modalità colore)
    if ((millis() % 500) < 250) {
      color = CRGB(255, 0, 0);
    } else {
      color = CRGB(100, 0, 0);
    }
  } else {
    // Usa il colore selezionato dall'utente
    color = getColorFromMode(timerColorMode);
  }

  int startX = 0;
  int startY = 6;

  // Decine minuti
  drawSmallDigit3x5(minutes / 10, startX, startY, color);
  // Unità minuti
  drawSmallDigit3x5(minutes % 10, startX + 4, startY, color);

  // Due punti (separatore) - LAMPEGGIANTI ogni secondo solo quando il timer è attivo
  if (timerRunning && !timerFinished) {
    bool showColon = (millis() % 1000) < 500;
    if (showColon) {
      setPixel(startX + 7, startY + 1, color);
      setPixel(startX + 7, startY + 3, color);
    }
  } else {
    // Quando è fermo o finito, mostra i due punti fissi
    setPixel(startX + 7, startY + 1, color);
    setPixel(startX + 7, startY + 3, color);
  }

  // Decine secondi
  drawSmallDigit3x5(seconds / 10, startX + 9, startY, color);
  // Unità secondi
  drawSmallDigit3x5(seconds % 10, startX + 13, startY, color);

  FastLED.show();
}

// ============================================
// CRONOTERMOSTATO - FUNZIONI
// ============================================

// Inizializza i valori di default per gli slot e la programmazione
void initThermostatDefaults() {
  // Slot di default
  strncpy(thermostatSlots[0].name, "Mattina", sizeof(thermostatSlots[0].name));
  thermostatSlots[0].temperature = 20.0;

  strncpy(thermostatSlots[1].name, "Giorno", sizeof(thermostatSlots[1].name));
  thermostatSlots[1].temperature = 19.0;

  strncpy(thermostatSlots[2].name, "Sera", sizeof(thermostatSlots[2].name));
  thermostatSlots[2].temperature = 21.0;

  strncpy(thermostatSlots[3].name, "Notte", sizeof(thermostatSlots[3].name));
  thermostatSlots[3].temperature = 17.0;

  // Programmazione di default per tutti i giorni
  for (int day = 0; day < 7; day++) {
    for (int hour = 0; hour < 24; hour++) {
      if (hour >= 6 && hour < 9) {
        thermostatSchedule[day].slot[hour] = 0; // Mattina
      } else if (hour >= 9 && hour < 18) {
        thermostatSchedule[day].slot[hour] = 1; // Giorno
      } else if (hour >= 18 && hour < 23) {
        thermostatSchedule[day].slot[hour] = 2; // Sera
      } else {
        thermostatSchedule[day].slot[hour] = 3; // Notte
      }
    }
  }
}

// Salva la programmazione del termostato su LittleFS
void saveThermostatSchedule() {
  File file = LittleFS.open(THERMOSTAT_FILE, "w");
  if (!file) {
    Serial.println("Errore apertura file termostato per scrittura");
    return;
  }

  StaticJsonDocument<2048> doc;

  // Salva gli slot
  JsonArray slotsArray = doc.createNestedArray("slots");
  for (int i = 0; i < 4; i++) {
    JsonObject slot = slotsArray.createNestedObject();
    slot["name"] = thermostatSlots[i].name;
    slot["temp"] = thermostatSlots[i].temperature;
  }

  // Salva la programmazione settimanale
  JsonObject schedule = doc.createNestedObject("schedule");
  const char* dayNames[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

  for (int day = 0; day < 7; day++) {
    JsonArray dayArray = schedule.createNestedArray(dayNames[day]);
    for (int hour = 0; hour < 24; hour++) {
      dayArray.add(thermostatSchedule[day].slot[hour]);
    }
  }

  serializeJson(doc, file);
  file.close();
  Serial.println("Programmazione termostato salvata");
}

// Carica la programmazione del termostato da LittleFS
void loadThermostatSchedule() {
  if (!LittleFS.exists(THERMOSTAT_FILE)) {
    Serial.println("File programmazione termostato non trovato, uso default");
    initThermostatDefaults();
    saveThermostatSchedule();
    thermostatScheduleLoaded = true;
    return;
  }

  File file = LittleFS.open(THERMOSTAT_FILE, "r");
  if (!file) {
    Serial.println("Errore apertura file termostato");
    initThermostatDefaults();
    thermostatScheduleLoaded = true;
    return;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Errore parsing JSON termostato");
    initThermostatDefaults();
    thermostatScheduleLoaded = true;
    return;
  }

  // Carica gli slot
  JsonArray slotsArray = doc["slots"];
  for (int i = 0; i < 4 && i < (int)slotsArray.size(); i++) {
    strncpy(thermostatSlots[i].name, slotsArray[i]["name"] | "Slot", sizeof(thermostatSlots[i].name));
    thermostatSlots[i].temperature = slotsArray[i]["temp"] | 19.0;
  }

  // Carica la programmazione
  JsonObject schedule = doc["schedule"];
  const char* dayNames[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

  for (int day = 0; day < 7; day++) {
    JsonArray dayArray = schedule[dayNames[day]];
    for (int hour = 0; hour < 24 && hour < (int)dayArray.size(); hour++) {
      thermostatSchedule[day].slot[hour] = dayArray[hour] | 255;
    }
  }

  thermostatScheduleLoaded = true;
  Serial.println("Programmazione termostato caricata");
}

// Ottiene la temperatura target dalla programmazione corrente
float getThermostatTargetTemp() {
  if (!thermostatScheduleLoaded) {
    loadThermostatSchedule();
  }

  // Se override manuale attivo, usa quella temperatura
  if (thermostatManualOverride) {
    return thermostatManualTemp;
  }

  // Ottieni giorno e ora correnti
  int dayOfWeek = myTZ.weekday(); // 0=Dom, 1=Lun...6=Sab
  int hour = myTZ.hour();

  // Converti da ezTime (0=Dom) al nostro formato (0=Lun)
  int day;
  if (dayOfWeek == 0) {
    day = 6; // Domenica -> indice 6
  } else {
    day = dayOfWeek - 1; // Lun=0, Mar=1, etc.
  }

  // Trova lo slot per quest'ora
  uint8_t slotIndex = thermostatSchedule[day].slot[hour];

  if (slotIndex < 4) {
    return thermostatSlots[slotIndex].temperature;
  }

  // Se slot non valido, usa temperatura default
  return (float)thermostatDefaultTemp;
}

// Invia comando allo Shelly
bool setShellyState(bool on) {
  if (shellyIP.length() == 0) {
    Serial.println("Shelly IP non configurato");
    return false;
  }

  // Rate limiting
  if (millis() - lastShellyCommand < SHELLY_MIN_INTERVAL) {
    return false;
  }

  HTTPClient http;
  String url = "http://" + shellyIP + "/relay/0?turn=" + (on ? "on" : "off");

  Serial.print("Shelly comando: ");
  Serial.println(url);

  http.begin(url);
  http.setTimeout(5000);
  int httpCode = http.GET();
  http.end();

  lastShellyCommand = millis();

  if (httpCode == 200) {
    thermostatHeatingOn = on;
    shellyConnected = true;
    Serial.println(on ? "Shelly: ACCESO" : "Shelly: SPENTO");
    return true;
  } else {
    Serial.print("Shelly errore HTTP: ");
    Serial.println(httpCode);
    shellyConnected = false;
    return false;
  }
}

// Legge lo stato attuale dello Shelly
bool getShellyState() {
  if (shellyIP.length() == 0) return false;

  HTTPClient http;
  String url = "http://" + shellyIP + "/relay/0";

  http.begin(url);
  http.setTimeout(3000);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    http.end();

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      bool isOn = doc["ison"] | false;
      thermostatHeatingOn = isOn;
      shellyConnected = true;
      return isOn;
    }
  }

  http.end();
  shellyConnected = false;
  return false;
}

// Controllo principale del termostato (chiamato nel loop)
void checkThermostat() {
  if (!thermostatEnabled || !sensorAvailable) return;
  if (millis() - lastThermostatCheck < THERMOSTAT_CHECK_INTERVAL) return;

  lastThermostatCheck = millis();

  // Aggiorna stato Shelly ogni tanto
  if (millis() - lastShellyStatusCheck > 60000) { // Ogni minuto
    getShellyState();
    lastShellyStatusCheck = millis();
  }

  // Determina temperatura target
  thermostatTargetTemp = getThermostatTargetTemp();

  // Logica isteresi
  float tempLow = thermostatTargetTemp - thermostatHysteresis;
  float tempHigh = thermostatTargetTemp + thermostatHysteresis;

  if (!thermostatHeatingOn && currentTemperature < tempLow) {
    // Temperatura troppo bassa, accendi
    Serial.print("Temp ");
    Serial.print(currentTemperature);
    Serial.print(" < ");
    Serial.print(tempLow);
    Serial.println(" -> ACCENDO");
    setShellyState(true);
  } else if (thermostatHeatingOn && currentTemperature > tempHigh) {
    // Temperatura raggiunta, spegni
    Serial.print("Temp ");
    Serial.print(currentTemperature);
    Serial.print(" > ");
    Serial.print(tempHigh);
    Serial.println(" -> SPENGO");
    setShellyState(false);
  }
}

// Disegna il termostato sulla matrice LED
void drawThermostat() {
  clearMatrixNoShow();

  // Aggiorna temperatura target
  thermostatTargetTemp = getThermostatTargetTemp();

  // Colore temperatura ambiente
  CRGB tempColor = CRGB(0, 200, 255); // Ciano

  // Colore target
  CRGB targetColor;
  if (currentTemperature >= thermostatTargetTemp - 0.3) {
    targetColor = CRGB(0, 255, 0); // Verde - raggiunto
  } else {
    targetColor = CRGB(100, 100, 255); // Blu chiaro - sotto target
  }

  // Icona fiamma in alto a sinistra (piccola 3x4)
  if (thermostatHeatingOn) {
    int frame = (millis() / 150) % 2;
    CRGB flame1 = CRGB(255, 80, 0);
    CRGB flame2 = CRGB(255, 40, 0);
    // Fiamma animata
    setPixel(1, 1, flame1);
    setPixel(2, 1, flame1);
    setPixel(0, 2, frame ? flame2 : flame1);
    setPixel(1, 2, flame1);
    setPixel(2, 2, flame1);
    setPixel(3, 2, frame ? flame1 : flame2);
    setPixel(1, 3, flame2);
    setPixel(2, 3, flame2);
  } else {
    // Fiamma spenta
    CRGB gray = CRGB(40, 40, 40);
    setPixel(1, 1, gray);
    setPixel(2, 1, gray);
    setPixel(1, 2, gray);
    setPixel(2, 2, gray);
  }

  // Stato ON/OFF in alto a destra
  if (thermostatEnabled) {
    CRGB onColor = thermostatHeatingOn ? CRGB(255, 100, 0) : CRGB(0, 150, 0);
    setPixel(13, 1, onColor);
    setPixel(14, 1, onColor);
    setPixel(13, 2, onColor);
    setPixel(14, 2, onColor);
  }

  // Temperatura ambiente - 2 cifre allineate verticalmente con target, alzate di 1 LED
  int tempInt = (int)(currentTemperature + 0.5);
  if (tempInt > 99) tempInt = 99;
  if (tempInt < 0) tempInt = 0;

  // Posizione allineata con target (X=5, X=9) e alzata di 1 LED (Y=4)
  drawSmallDigit3x5(tempInt / 10, 5, 4, tempColor);
  drawSmallDigit3x5(tempInt % 10, 9, 4, tempColor);

  // Simbolo gradi (1 solo LED bianco, distanziato di 1 LED dalla cifra)
  CRGB whiteColor = CRGB(255, 255, 255);
  setPixel(13, 4, whiteColor);

  // Temperatura target in basso - formato "->20"
  int targetInt = (int)(thermostatTargetTemp + 0.5);
  if (targetInt > 99) targetInt = 99;
  if (targetInt < 0) targetInt = 0;

  // Freccia piccola
  setPixel(1, 13, targetColor);
  setPixel(2, 12, targetColor);
  setPixel(2, 13, targetColor);
  setPixel(2, 14, targetColor);

  // Target: 2 cifre (allineate con temperatura ambiente)
  drawSmallDigit3x5(targetInt / 10, 5, 11, targetColor);
  drawSmallDigit3x5(targetInt % 10, 9, 11, targetColor);

  // Simbolo gradi piccolo per target (1 LED bianco, distanziato di 1 LED)
  setPixel(13, 11, whiteColor);

  FastLED.show();
}

// ============================================
// HANDLER WEB CRONOTERMOSTATO
// ============================================

// Pagina principale termostato
void handleThermostat() {
  changeState(STATE_THERMOSTAT);
  forceRedraw = true;

  // Carica programmazione se non caricata
  if (!thermostatScheduleLoaded) {
    loadThermostatSchedule();
  }

  // Aggiorna temperatura target
  thermostatTargetTemp = getThermostatTargetTemp();

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<title>Cronotermostato</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#16213e;border-radius:15px;padding:25px;box-shadow:0 8px 32px rgba(0,0,0,0.3);}";
  html += "h1{text-align:center;color:#ff9800;margin-bottom:25px;font-size:1.8em;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#ff9800;text-decoration:none;font-size:1.1em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".temp-display{text-align:center;padding:20px;background:#0f3460;border-radius:12px;margin-bottom:20px;}";
  html += ".current-temp{font-size:56px;font-weight:bold;color:#00d4ff;}";
  html += ".target-temp{font-size:20px;color:#4ecca3;margin-top:10px;}";
  html += ".slot-info{color:#aaa;margin-top:8px;font-size:14px;}";
  html += ".status{display:flex;justify-content:center;gap:15px;margin:20px 0;flex-wrap:wrap;}";
  html += ".status-item{padding:10px 18px;border-radius:8px;font-weight:bold;font-size:14px;}";
  html += ".heating-on{background:#e65100;color:#fff;}";
  html += ".heating-off{background:#455a64;color:#fff;}";
  html += ".connected{background:#2e7d32;color:#fff;}";
  html += ".disconnected{background:#c62828;color:#fff;}";
  html += ".controls{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:20px;}";
  html += ".btn{padding:15px 10px;border:none;border-radius:8px;font-size:14px;font-weight:bold;cursor:pointer;text-align:center;word-wrap:break-word;}";
  html += ".btn-temp{font-size:28px;background:#1976D2;color:#fff;}";
  html += ".btn-override{background:#f57c00;color:#fff;}";
  html += ".btn-override.active{background:#d32f2f;}";
  html += ".btn-power{background:#388e3c;color:#fff;}";
  html += ".btn-power.off{background:#d32f2f;}";
  html += ".links{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:20px;}";
  html += ".link-btn{display:flex;align-items:center;justify-content:center;padding:15px 10px;background:#0f3460;border:2px solid #ff9800;border-radius:8px;text-decoration:none;color:#fff;font-weight:bold;font-size:13px;text-align:center;min-height:20px;}";
  html += "</style></head><body>";

  html += "<div class='nav'><a href='/'>&#127968; Menu</a></div>";
  html += "<div class='container'>";
  html += "<h1>&#128293; TERMOSTATO</h1>";

  // Display temperatura
  html += "<div class='temp-display'>";
  if (sensorAvailable) {
    html += "<div class='current-temp'>" + String(currentTemperature, 1) + "&deg;C</div>";
  } else {
    html += "<div class='current-temp' style='color:#f44336;'>--.-&deg;C</div>";
  }
  html += "<div class='target-temp'>Target: " + String(thermostatTargetTemp, 1) + "&deg;C</div>";

  // Mostra slot corrente
  if (!thermostatManualOverride) {
    int dayOfWeek = myTZ.weekday();
    int hour = myTZ.hour();
    int day = (dayOfWeek == 0) ? 6 : dayOfWeek - 1;
    uint8_t slotIndex = thermostatSchedule[day].slot[hour];
    if (slotIndex < 4) {
      html += "<div class='slot-info'>Fascia: " + String(thermostatSlots[slotIndex].name) + "</div>";
    }
  } else {
    html += "<div class='slot-info' style='color:#ff9800;'>OVERRIDE MANUALE</div>";
  }
  html += "</div>";

  // Status
  html += "<div class='status'>";
  html += thermostatHeatingOn ? "<div class='status-item heating-on'>CALDAIA ON</div>" : "<div class='status-item heating-off'>CALDAIA OFF</div>";
  html += shellyConnected ? "<div class='status-item connected'>SHELLY OK</div>" : "<div class='status-item disconnected'>SHELLY OFF</div>";
  html += "</div>";

  // Controlli
  html += "<div class='controls'>";
  html += "<button class='btn btn-temp' onclick='adjustTemp(-0.5)'>-</button>";
  html += "<button class='btn btn-temp' onclick='adjustTemp(0.5)'>+</button>";
  html += "<button class='btn btn-override" + String(thermostatManualOverride ? " active" : "") + "' onclick='toggleOverride()'>" + String(thermostatManualOverride ? "DISATTIVA" : "OVERRIDE") + "</button>";
  html += "<button class='btn btn-power" + String(thermostatEnabled ? "" : " off") + "' onclick='togglePower()'>" + String(thermostatEnabled ? "SPEGNI" : "ACCENDI") + "</button>";
  html += "</div>";

  // Link
  html += "<div class='links'>";
  html += "<a href='/thermostatconfig' class='link-btn'>CONFIGURA</a>";
  html += "<a href='/thermostatschedule' class='link-btn'>PROGRAMMA</a>";
  html += "</div>";

  html += "</div>";

  // JavaScript
  html += "<script>";
  html += "function adjustTemp(d){fetch('/setthermostat?manualtemp='+(parseFloat('" + String(thermostatManualTemp, 1) + "')+d)).then(()=>location.reload());}";
  html += "function toggleOverride(){fetch('/setthermostat?override=" + String(thermostatManualOverride ? "0" : "1") + "').then(()=>location.reload());}";
  html += "function togglePower(){fetch('/setthermostat?enabled=" + String(thermostatEnabled ? "0" : "1") + "').then(()=>location.reload());}";
  html += "setTimeout(()=>location.reload(),30000);";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

// Pagina configurazione termostato
void handleThermostatConfig() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<title>Configurazione Termostato</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;}";
  html += ".container{max-width:600px;margin:0 auto;background:#16213e;border-radius:15px;padding:25px;padding-bottom:30px;box-shadow:0 8px 32px rgba(0,0,0,0.3);box-sizing:border-box;}";
  html += "h1{text-align:center;color:#ff9800;margin-bottom:25px;font-size:1.5em;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#ff9800;text-decoration:none;font-size:1.1em;padding:10px 20px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += ".form-group{margin-bottom:20px;}";
  html += "label{display:block;margin-bottom:8px;color:#ff9800;font-weight:bold;}";
  html += "input[type='text']{width:100%;padding:12px;background:#0f3460;border:2px solid #ff9800;border-radius:8px;color:#fff;font-size:16px;box-sizing:border-box;}";
  html += ".slider-container{display:flex;align-items:center;gap:15px;}";
  html += ".slider-container input[type='range']{flex:1;height:8px;}";
  html += ".slider-value{min-width:55px;text-align:center;font-weight:bold;background:#0f3460;padding:8px;border-radius:6px;}";
  html += ".btn{display:block;width:100%;padding:15px;border:none;border-radius:8px;font-size:15px;font-weight:bold;cursor:pointer;margin-top:12px;text-align:center;box-sizing:border-box;}";
  html += ".btn-save{background:#4ecca3;color:#16213e;}";
  html += ".btn-test{background:#3498db;color:#fff;}";
  html += ".btn-back{background:#0f3460;border:2px solid #ff9800;color:#fff;text-decoration:none;display:block;margin-top:15px;margin-bottom:0;}";
  html += ".status-msg{padding:12px;border-radius:8px;text-align:center;margin-top:12px;display:none;}";
  html += "</style></head><body>";

  html += "<div class='nav'><a href='/thermostat'>&#128293; Termostato</a></div>";
  html += "<div class='container'>";
  html += "<h1>&#9881; CONFIGURAZIONE</h1>";

  // IP Shelly
  html += "<div class='form-group'>";
  html += "<label>Indirizzo IP Shelly</label>";
  html += "<input type='text' id='shellyIP' placeholder='es: 192.168.1.100' value='" + shellyIP + "'>";
  html += "</div>";

  html += "<button class='btn btn-test' onclick='testShelly()'>TEST SHELLY</button>";
  html += "<div id='testStatus' class='status-msg'></div>";

  // Isteresi
  html += "<div class='form-group' style='margin-top:20px;'>";
  html += "<label>Isteresi</label>";
  html += "<div class='slider-container'>";
  html += "<input type='range' id='hysteresis' min='0.2' max='2.0' step='0.1' value='" + String(thermostatHysteresis, 1) + "' oninput='updateHyst()'>";
  html += "<span class='slider-value' id='hystVal'>" + String(thermostatHysteresis, 1) + "&deg;C</span>";
  html += "</div></div>";

  // Temperatura default
  html += "<div class='form-group'>";
  html += "<label>Temp. Default</label>";
  html += "<div class='slider-container'>";
  html += "<input type='range' id='defaultTemp' min='10' max='25' step='1' value='" + String(thermostatDefaultTemp) + "' oninput='updateDef()'>";
  html += "<span class='slider-value' id='defVal'>" + String(thermostatDefaultTemp) + "&deg;C</span>";
  html += "</div></div>";

  html += "<button class='btn btn-save' onclick='saveConfig()'>SALVA</button>";
  html += "<a href='/thermostat' class='btn btn-back'>INDIETRO</a>";
  html += "</div>";

  html += "<script>";
  html += "function updateHyst(){document.getElementById('hystVal').innerText=document.getElementById('hysteresis').value+'°C';}";
  html += "function updateDef(){document.getElementById('defVal').innerText=document.getElementById('defaultTemp').value+'°C';}";
  html += "function testShelly(){var ip=document.getElementById('shellyIP').value;var s=document.getElementById('testStatus');s.style.display='block';s.style.background='#ff9800';s.innerText='Testing...';fetch('/setthermostat?testshelly='+encodeURIComponent(ip)).then(r=>r.text()).then(t=>{s.style.background=t=='OK'?'#4ecca3':'#e74c3c';s.innerText=t=='OK'?'Connesso!':'Errore: '+t;});}";
  html += "function saveConfig(){var ip=document.getElementById('shellyIP').value;var h=document.getElementById('hysteresis').value;var d=document.getElementById('defaultTemp').value;fetch('/setthermostat?shellyip='+encodeURIComponent(ip)+'&hysteresis='+h+'&defaulttemp='+d).then(()=>alert('Salvato!'));}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

// Pagina programmazione settimanale
void handleThermostatSchedule() {
  if (!thermostatScheduleLoaded) {
    loadThermostatSchedule();
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<title>Programmazione Termostato</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:10px;}";
  html += ".container{max-width:900px;margin:0 auto;padding:15px;padding-bottom:20px;box-sizing:border-box;}";
  html += ".nav{text-align:center;margin-bottom:15px;}";
  html += ".nav a{color:#ff9800;text-decoration:none;padding:8px 16px;background:#0f3460;border-radius:8px;display:inline-block;}";
  html += "h1{text-align:center;color:#ff9800;font-size:1.4em;margin:10px 0 15px;}";
  html += ".card{background:#16213e;border-radius:12px;padding:12px;margin-bottom:12px;}";
  html += ".slots{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}";
  html += ".slot{padding:8px;border-radius:6px;text-align:center;}";
  html += ".slot input{width:50px;padding:4px;border:none;border-radius:4px;text-align:center;font-size:13px;background:rgba(255,255,255,0.2);color:#fff;}";
  html += ".slot-name{font-weight:bold;font-size:11px;margin-bottom:4px;}";
  html += ".slot-0{background:#e65100;}";
  html += ".slot-1{background:#1565c0;}";
  html += ".slot-2{background:#7b1fa2;}";
  html += ".slot-3{background:#455a64;}";
  html += ".schedule-grid{overflow-x:auto;-webkit-overflow-scrolling:touch;}";
  html += "table{border-collapse:collapse;font-size:10px;min-width:500px;}";
  html += "th,td{padding:3px 1px;text-align:center;border:1px solid #0f3460;}";
  html += "th{background:#0f3460;font-size:9px;position:sticky;top:0;}";
  html += ".hour-cell{width:18px;height:22px;cursor:pointer;}";
  html += ".c0{background:#e65100;}";
  html += ".c1{background:#1565c0;}";
  html += ".c2{background:#7b1fa2;}";
  html += ".c3{background:#455a64;}";
  html += ".day-name{font-weight:bold;text-align:left;padding-left:4px;background:#0f3460;position:sticky;left:0;}";
  html += ".btn{display:block;width:100%;padding:12px;border:none;border-radius:8px;font-size:14px;font-weight:bold;cursor:pointer;margin-top:10px;text-align:center;box-sizing:border-box;}";
  html += ".btn-save{background:#4ecca3;color:#16213e;}";
  html += ".btn-back{background:#0f3460;border:2px solid #ff9800;color:#fff;text-decoration:none;margin-bottom:0;}";
  html += ".legend{display:flex;justify-content:center;gap:10px;margin:10px 0;flex-wrap:wrap;}";
  html += ".legend-item{display:flex;align-items:center;gap:4px;font-size:11px;}";
  html += ".legend-color{width:16px;height:16px;border-radius:3px;}";
  html += "</style></head><body>";

  html += "<div class='nav'><a href='/thermostat'>&#128293; Termostato</a></div>";
  html += "<div class='container'>";
  html += "<h1>&#128197; PROGRAMMAZIONE</h1>";

  // Slot temperature
  html += "<div class='card'>";
  html += "<div class='slots'>";
  for (int i = 0; i < 4; i++) {
    html += "<div class='slot slot-" + String(i) + "'>";
    html += "<div class='slot-name'>" + String(thermostatSlots[i].name) + "</div>";
    html += "<input type='number' id='slotTemp" + String(i) + "' value='" + String(thermostatSlots[i].temperature, 1) + "' step='0.5' min='5' max='30'>&deg;";
    html += "</div>";
  }
  html += "</div></div>";

  // Legenda
  html += "<div class='legend'>";
  html += "<div class='legend-item'><div class='legend-color c0'></div>Matt</div>";
  html += "<div class='legend-item'><div class='legend-color c1'></div>Giorno</div>";
  html += "<div class='legend-item'><div class='legend-color c2'></div>Sera</div>";
  html += "<div class='legend-item'><div class='legend-color c3'></div>Notte</div>";
  html += "</div>";

  // Griglia programmazione
  html += "<div class='card'><div class='schedule-grid'>";
  html += "<table><tr><th></th>";
  for (int h = 0; h < 24; h++) {
    html += "<th>" + String(h) + "</th>";
  }
  html += "</tr>";

  const char* dayNames[] = {"Lu", "Ma", "Me", "Gi", "Ve", "Sa", "Do"};
  for (int day = 0; day < 7; day++) {
    html += "<tr><td class='day-name'>" + String(dayNames[day]) + "</td>";
    for (int hour = 0; hour < 24; hour++) {
      int slot = thermostatSchedule[day].slot[hour];
      if (slot > 3) slot = 3;
      html += "<td class='hour-cell c" + String(slot) + "' onclick='cycleSlot(" + String(day) + "," + String(hour) + ")' id='c" + String(day) + "_" + String(hour) + "'></td>";
    }
    html += "</tr>";
  }
  html += "</table></div></div>";

  html += "<button class='btn btn-save' onclick='saveSchedule()'>SALVA</button>";
  html += "<a href='/thermostat' class='btn btn-back'>INDIETRO</a>";
  html += "</div>";

  // JavaScript
  html += "<script>var s=[";
  for (int day = 0; day < 7; day++) {
    html += "[";
    for (int hour = 0; hour < 24; hour++) {
      html += String(thermostatSchedule[day].slot[hour]);
      if (hour < 23) html += ",";
    }
    html += "]";
    if (day < 6) html += ",";
  }
  html += "];";
  html += "function cycleSlot(d,h){var n=(s[d][h]+1)%4;s[d][h]=n;document.getElementById('c'+d+'_'+h).className='hour-cell c'+n;}";
  html += "function saveSchedule(){var t=[];for(var i=0;i<4;i++)t.push(document.getElementById('slotTemp'+i).value);fetch('/setthermostat?schedule='+encodeURIComponent(JSON.stringify({slots:t,schedule:s}))).then(r=>{if(r.ok){alert('Salvato!');location.href='/thermostat';}else alert('Errore');});}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

// API per impostare parametri termostato
void handleSetThermostat() {
  bool configChanged = false;

  // Abilita/Disabilita termostato
  if (server.hasArg("enabled")) {
    thermostatEnabled = (server.arg("enabled") == "1");
    configChanged = true;
  }

  // Override manuale
  if (server.hasArg("override")) {
    thermostatManualOverride = (server.arg("override") == "1");
    configChanged = true;
  }

  // Temperatura override manuale
  if (server.hasArg("manualtemp")) {
    thermostatManualTemp = server.arg("manualtemp").toFloat();
    if (thermostatManualTemp < 5.0) thermostatManualTemp = 5.0;
    if (thermostatManualTemp > 30.0) thermostatManualTemp = 30.0;
    thermostatManualOverride = true; // Attiva override automaticamente
    configChanged = true;
  }

  // IP Shelly
  if (server.hasArg("shellyip")) {
    shellyIP = server.arg("shellyip");
    configChanged = true;
  }

  // Isteresi
  if (server.hasArg("hysteresis")) {
    thermostatHysteresis = server.arg("hysteresis").toFloat();
    if (thermostatHysteresis < 0.1) thermostatHysteresis = 0.1;
    if (thermostatHysteresis > 2.0) thermostatHysteresis = 2.0;
    configChanged = true;
  }

  // Temperatura default
  if (server.hasArg("defaulttemp")) {
    thermostatDefaultTemp = server.arg("defaulttemp").toInt();
    if (thermostatDefaultTemp < 5) thermostatDefaultTemp = 5;
    if (thermostatDefaultTemp > 30) thermostatDefaultTemp = 30;
    configChanged = true;
  }

  // Test connessione Shelly
  if (server.hasArg("testshelly")) {
    String testIP = server.arg("testshelly");
    HTTPClient http;
    String url = "http://" + testIP + "/relay/0";
    http.begin(url);
    http.setTimeout(5000);
    int httpCode = http.GET();
    http.end();

    if (httpCode == 200) {
      server.send(200, "text/plain", "OK");
    } else {
      server.send(200, "text/plain", "Errore HTTP " + String(httpCode));
    }
    return;
  }

  // Salva programmazione settimanale
  if (server.hasArg("schedule")) {
    String scheduleJson = server.arg("schedule");
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, scheduleJson);

    if (!error) {
      // Aggiorna temperature slot
      JsonArray temps = doc["slots"];
      for (int i = 0; i < 4 && i < (int)temps.size(); i++) {
        thermostatSlots[i].temperature = temps[i].as<float>();
      }

      // Aggiorna programmazione
      JsonArray sched = doc["schedule"];
      for (int day = 0; day < 7 && day < (int)sched.size(); day++) {
        JsonArray dayArray = sched[day];
        for (int hour = 0; hour < 24 && hour < (int)dayArray.size(); hour++) {
          thermostatSchedule[day].slot[hour] = dayArray[hour].as<int>();
        }
      }

      saveThermostatSchedule();
      server.send(200, "text/plain", "OK");
      return;
    } else {
      server.send(400, "text/plain", "JSON Error");
      return;
    }
  }

  if (configChanged) {
    saveConfig();
  }

  server.send(200, "text/plain", "OK");
}

// API stato termostato (JSON)
void handleThermostatStatus() {
  thermostatTargetTemp = getThermostatTargetTemp();

  String json = "{";
  json += "\"enabled\":" + String(thermostatEnabled ? "true" : "false") + ",";
  json += "\"sensorAvailable\":" + String(sensorAvailable ? "true" : "false") + ",";
  json += "\"currentTemp\":" + String(currentTemperature, 1) + ",";
  json += "\"targetTemp\":" + String(thermostatTargetTemp, 1) + ",";
  json += "\"heatingOn\":" + String(thermostatHeatingOn ? "true" : "false") + ",";
  json += "\"shellyConnected\":" + String(shellyConnected ? "true" : "false") + ",";
  json += "\"manualOverride\":" + String(thermostatManualOverride ? "true" : "false") + ",";
  json += "\"manualTemp\":" + String(thermostatManualTemp, 1) + ",";
  json += "\"hysteresis\":" + String(thermostatHysteresis, 1);
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================
// ZUMA GAME - IMPLEMENTAZIONE COMPLETA
// ============================================

// Genera il percorso a spirale
void initZumaPath() {
  int idx = 0;
  int minX = 0, maxX = 15, minY = 0, maxY = 15;

  while (minX <= maxX && minY <= maxY && idx < ZUMA_PATH_LENGTH) {
    // Bordo superiore: da sinistra a destra
    for (int x = minX; x <= maxX && idx < ZUMA_PATH_LENGTH; x++) {
      if (x >= 7 && x <= 8 && minY >= 7 && minY <= 8) continue;
      zumaPathData[idx][0] = x;
      zumaPathData[idx][1] = minY;
      idx++;
    }
    minY++;

    // Bordo destro: dall'alto al basso
    for (int y = minY; y <= maxY && idx < ZUMA_PATH_LENGTH; y++) {
      if (maxX >= 7 && maxX <= 8 && y >= 7 && y <= 8) continue;
      zumaPathData[idx][0] = maxX;
      zumaPathData[idx][1] = y;
      idx++;
    }
    maxX--;

    // Bordo inferiore: da destra a sinistra
    if (minY <= maxY) {
      for (int x = maxX; x >= minX && idx < ZUMA_PATH_LENGTH; x--) {
        if (x >= 7 && x <= 8 && maxY >= 7 && maxY <= 8) continue;
        zumaPathData[idx][0] = x;
        zumaPathData[idx][1] = maxY;
        idx++;
      }
      maxY--;
    }

    // Bordo sinistro: dal basso all'alto
    if (minX <= maxX) {
      for (int y = maxY; y >= minY && idx < ZUMA_PATH_LENGTH; y--) {
        if (minX >= 7 && minX <= 8 && y >= 7 && y <= 8) continue;
        zumaPathData[idx][0] = minX;
        zumaPathData[idx][1] = y;
        idx++;
      }
      minX++;
    }
  }

  Serial.printf("ZUMA: Path generated with %d positions\n", idx);
}

// Colori palline ZUMA
CRGB zumaGetBallColor(uint8_t colorIndex) {
  switch (colorIndex) {
    case 0: return CRGB(255, 0, 0);     // Rosso
    case 1: return CRGB(0, 255, 0);     // Verde
    case 2: return CRGB(0, 100, 255);   // Blu
    case 3: return CRGB(255, 255, 0);   // Giallo
    case 4: return CRGB(255, 0, 255);   // Magenta
    default: return CRGB(255, 255, 255);
  }
}

// Reset gioco ZUMA
void resetZuma() {
  static bool pathInitialized = false;
  if (!pathInitialized) {
    initZumaPath();
    pathInitialized = true;
  }

  zumaChainLength = 0;
  for (int i = 0; i < ZUMA_MAX_BALLS; i++) {
    zumaChain[i].active = false;
    zumaChain[i].exploding = false;
  }

  int initialBalls = 8 + (zumaLevel * 2);
  if (initialBalls > 20) initialBalls = 20;

  for (int i = 0; i < initialBalls; i++) {
    zumaChain[i].position = i * 1.0;
    zumaChain[i].color = random(0, ZUMA_NUM_COLORS);
    zumaChain[i].active = true;
    zumaChain[i].exploding = false;
    zumaChainLength++;
  }

  zumaProjectile.active = false;
  zumaShooterColor = random(0, ZUMA_NUM_COLORS);
  zumaNextColor = random(0, ZUMA_NUM_COLORS);
  zumaAimAngle = 0;

  if (zumaLevel == 1) {
    zumaScore = 0;
    zumaLives = 5;
  }

  zumaGameActive = true;
  zumaGameOver = false;
  zumaChainMoving = true;
  zumaSpeed = 500 - (zumaLevel * 30);
  if (zumaSpeed < 150) zumaSpeed = 150;

  zumaLastUpdate = millis();
  zumaLastSpawn = millis();
  zumaComboCount = 0;

  Serial.println("ZUMA: Game reset");
  drawZuma();
}

// Update logica gioco ZUMA
void updateZuma() {
  static bool gameOverDrawn = false;

  if (zumaGameOver) {
    if (!gameOverDrawn) {
      drawGameOver();
      gameOverDrawn = true;
    }
    return;
  } else {
    gameOverDrawn = false;
  }

  if (!zumaGameActive) return;

  unsigned long now = millis();

  // Update proiettile
  if (zumaProjectile.active) {
    zumaProjectile.x += zumaProjectile.dx * ZUMA_SHOOT_SPEED * 0.15;
    zumaProjectile.y += zumaProjectile.dy * ZUMA_SHOOT_SPEED * 0.15;

    int px = (int)zumaProjectile.x;
    int py = (int)zumaProjectile.y;

    if (px < 0 || px > 15 || py < 0 || py > 15) {
      zumaProjectile.active = false;
    } else {
      for (int i = 0; i < zumaChainLength; i++) {
        if (!zumaChain[i].active) continue;

        int ballPos = (int)zumaChain[i].position;
        if (ballPos < 0 || ballPos >= ZUMA_PATH_LENGTH) continue;

        int bx = zumaPathData[ballPos][0];
        int by = zumaPathData[ballPos][1];

        if (abs(px - bx) <= 1 && abs(py - by) <= 1) {
          zumaInsertBall(i);
          zumaProjectile.active = false;

          int points = zumaCheckMatches(i);
          if (points > 0) {
            zumaScore += points;
            playSuccess();
          } else {
            playBeep();
          }
          break;
        }
      }
    }
  }

  // Avanza catena
  if (zumaChainMoving && now - zumaLastUpdate >= (unsigned long)zumaSpeed) {
    zumaLastUpdate = now;
    zumaAdvanceChain();
    zumaGameOverCheck();
  }

  // Spawn nuove palline
  if (now - zumaLastSpawn >= ZUMA_BALL_SPAWN_INTERVAL && zumaChainLength < ZUMA_MAX_BALLS - 5) {
    zumaLastSpawn = now;
    zumaSpawnBall();
  }

  // Reset combo
  if (zumaComboCount > 0 && now - zumaComboTime > 2000) {
    zumaComboCount = 0;
  }

  // Vittoria livello
  if (zumaChainLength == 0) {
    zumaLevel++;
    playLevelUp();
    resetZuma();
  }

  drawZuma();
}

// Avanza la catena
void zumaAdvanceChain() {
  for (int i = 0; i < zumaChainLength; i++) {
    if (zumaChain[i].active && !zumaChain[i].exploding) {
      zumaChain[i].position += 0.5;
    }
  }
}

// Spawn nuova pallina
void zumaSpawnBall() {
  if (zumaChainLength >= ZUMA_MAX_BALLS) return;

  for (int i = zumaChainLength; i > 0; i--) {
    zumaChain[i] = zumaChain[i - 1];
  }

  zumaChain[0].position = 0;
  zumaChain[0].color = random(0, ZUMA_NUM_COLORS);
  zumaChain[0].active = true;
  zumaChain[0].exploding = false;
  zumaChainLength++;
}

// Spara pallina
void zumaShoot() {
  if (zumaProjectile.active || !zumaGameActive) return;

  float radians = zumaAimAngle * PI / 180.0;

  zumaProjectile.x = ZUMA_CANNON_X + 0.5;
  zumaProjectile.y = ZUMA_CANNON_Y + 0.5;
  zumaProjectile.dx = sin(radians);
  zumaProjectile.dy = -cos(radians);
  zumaProjectile.color = zumaShooterColor;
  zumaProjectile.active = true;

  zumaShooterColor = zumaNextColor;
  zumaNextColor = random(0, ZUMA_NUM_COLORS);

  playShoot();
}

// Ruota mira
void zumaRotateAim(int direction) {
  zumaAimAngle += direction * 15;
  if (zumaAimAngle < 0) zumaAimAngle += 360;
  if (zumaAimAngle >= 360) zumaAimAngle -= 360;
}

// Scambia colori
void zumaSwapColors() {
  uint8_t temp = zumaShooterColor;
  zumaShooterColor = zumaNextColor;
  zumaNextColor = temp;
  playBeep();
}

// Inserisci pallina nella catena
void zumaInsertBall(int insertPos) {
  if (zumaChainLength >= ZUMA_MAX_BALLS) return;

  for (int i = zumaChainLength; i > insertPos + 1; i--) {
    zumaChain[i] = zumaChain[i - 1];
  }

  float newPos = zumaChain[insertPos].position + 1.0;
  zumaChain[insertPos + 1].position = newPos;
  zumaChain[insertPos + 1].color = zumaProjectile.color;
  zumaChain[insertPos + 1].active = true;
  zumaChain[insertPos + 1].exploding = false;
  zumaChainLength++;

  for (int i = insertPos + 2; i < zumaChainLength; i++) {
    zumaChain[i].position += 1.0;
  }
}

// Controlla match
int zumaCheckMatches(int fromPos) {
  if (fromPos < 0 || fromPos >= zumaChainLength) return 0;

  uint8_t targetColor = zumaChain[fromPos].color;
  int startMatch = fromPos;
  int endMatch = fromPos;

  while (startMatch > 0 && zumaChain[startMatch - 1].active &&
         zumaChain[startMatch - 1].color == targetColor) {
    startMatch--;
  }

  while (endMatch < zumaChainLength - 1 && zumaChain[endMatch + 1].active &&
         zumaChain[endMatch + 1].color == targetColor) {
    endMatch++;
  }

  int matchCount = endMatch - startMatch + 1;

  if (matchCount >= ZUMA_MIN_MATCH) {
    zumaRemoveBalls(startMatch, matchCount);
    zumaComboCount++;
    zumaComboTime = millis();

    int points = matchCount * 10 * zumaComboCount;
    zumaCollapseChain();

    if (startMatch < zumaChainLength && startMatch > 0) {
      int chainPoints = zumaCheckMatches(startMatch);
      points += chainPoints;
    }

    return points;
  }

  return 0;
}

// Rimuovi palline
void zumaRemoveBalls(int startPos, int count) {
  for (int i = startPos; i < startPos + count && i < zumaChainLength; i++) {
    zumaChain[i].active = false;
  }
}

// Compatta catena
void zumaCollapseChain() {
  int writeIdx = 0;

  // Compatta l'array rimuovendo le palline inattive
  for (int readIdx = 0; readIdx < zumaChainLength; readIdx++) {
    if (zumaChain[readIdx].active) {
      if (writeIdx != readIdx) {
        zumaChain[writeIdx] = zumaChain[readIdx];
      }
      writeIdx++;
    }
  }

  zumaChainLength = writeIdx;

  if (zumaChainLength == 0) return;

  // Mantieni la posizione dell'ultima pallina (la piu' avanzata verso il centro)
  // e ricalcola le posizioni delle altre all'indietro
  float lastPosition = zumaChain[zumaChainLength - 1].position;

  // Riposiziona tutte le palline in modo consecutivo
  // partendo dalla posizione dell'ultima e andando indietro
  for (int i = zumaChainLength - 1; i >= 0; i--) {
    zumaChain[i].position = lastPosition - (zumaChainLength - 1 - i);
  }

  // Assicurati che nessuna posizione sia negativa
  if (zumaChain[0].position < 0) {
    float offset = -zumaChain[0].position;
    for (int i = 0; i < zumaChainLength; i++) {
      zumaChain[i].position += offset;
    }
  }
}

// Controlla game over
void zumaGameOverCheck() {
  for (int i = 0; i < zumaChainLength; i++) {
    if (!zumaChain[i].active) continue;

    int pos = (int)zumaChain[i].position;
    if (pos >= ZUMA_PATH_LENGTH - 5) {
      int bx = zumaPathData[pos][0];
      int by = zumaPathData[pos][1];

      int dist = abs(bx - ZUMA_CANNON_X) + abs(by - ZUMA_CANNON_Y);
      if (dist <= 2) {
        zumaLives--;
        playGameOver();

        if (zumaLives <= 0) {
          zumaGameOver = true;
          zumaGameActive = false;
        } else {
          zumaLevel = 1;
          int savedLives = zumaLives;
          resetZuma();
          zumaLives = savedLives;
        }
        return;
      }
    }
  }
}

// Disegna catena
void zumaDrawChain() {
  for (int i = 0; i < zumaChainLength; i++) {
    if (!zumaChain[i].active) continue;

    int pos = (int)zumaChain[i].position;
    if (pos < 0 || pos >= ZUMA_PATH_LENGTH) continue;

    int x = zumaPathData[pos][0];
    int y = zumaPathData[pos][1];

    CRGB color = zumaGetBallColor(zumaChain[i].color);
    setPixel(x, y, color);
  }
}

// Disegna cannone e mira
void zumaDrawCannon() {
  CRGB cannonColor = zumaGetBallColor(zumaShooterColor);
  setPixel(ZUMA_CANNON_X, ZUMA_CANNON_Y, cannonColor);

  static unsigned long lastBlink = 0;
  static bool blinkState = true;
  if (millis() - lastBlink > 300) {
    lastBlink = millis();
    blinkState = !blinkState;
  }

  if (blinkState) {
    CRGB nextColor = zumaGetBallColor(zumaNextColor);
    nextColor.r /= 3;
    nextColor.g /= 3;
    nextColor.b /= 3;
    setPixel(ZUMA_CANNON_X + 1, ZUMA_CANNON_Y, nextColor);
  }

  float radians = zumaAimAngle * PI / 180.0;
  for (int i = 2; i <= 4; i++) {
    int aimX = ZUMA_CANNON_X + (int)(sin(radians) * i);
    int aimY = ZUMA_CANNON_Y - (int)(cos(radians) * i);

    if (aimX >= 0 && aimX < 16 && aimY >= 0 && aimY < 16) {
      CRGB aimColor = cannonColor;
      aimColor.r /= i;
      aimColor.g /= i;
      aimColor.b /= i;
      setPixel(aimX, aimY, aimColor);
    }
  }
}

// Disegna proiettile
void zumaDrawProjectile() {
  if (!zumaProjectile.active) return;

  int x = (int)zumaProjectile.x;
  int y = (int)zumaProjectile.y;

  if (x >= 0 && x < 16 && y >= 0 && y < 16) {
    CRGB color = zumaGetBallColor(zumaProjectile.color);
    setPixel(x, y, color);
  }
}

// Disegna tutto ZUMA
void drawZuma() {
  clearMatrixNoShow();
  zumaDrawChain();
  zumaDrawProjectile();
  zumaDrawCannon();
  FastLED.show();
}

// Handler gamepad ZUMA
#ifdef ENABLE_BLUEPAD32
void handleGamepadZuma(int idx, ControllerPtr ctl) {
  unsigned long now = millis();
  GamepadState &gs = gamepadStates[idx];

  // Solo controller 0
  if (idx != 0) return;

  // ============================================
  // PULSANTI ATTIVI IN ZUMA:
  // - START: Riavvia partita (sempre)
  // - D-Pad/Stick SINISTRA-DESTRA: Ruota mira
  // - A: Spara pallina
  // - B: Scambia colore
  // ============================================

  // START per riavviare la partita (funziona SEMPRE)
  if (ctl->miscButtons() & MISC_BUTTON_START) {
    if (now - gs.lastButtonStart > GAMEPAD_DEBOUNCE_MS) {
      gs.lastButtonStart = now;
      zumaLevel = 1;
      resetZuma();
      return;
    }
  }

  // Se il gioco non e' attivo, ignora tutti gli altri input
  if (!zumaGameActive || zumaGameOver) return;

  uint8_t dpad = ctl->dpad();
  int32_t axisX = ctl->axisX();
  uint16_t buttons = ctl->buttons();

  // D-Pad SINISTRA o Stick SINISTRA: Ruota mira in senso antiorario
  if ((dpad & DPAD_LEFT_MASK) || axisX < -ANALOG_DEADZONE) {
    if (now - gs.lastDpadLeft > 80) {
      gs.lastDpadLeft = now;
      zumaRotateAim(-1);
    }
  }

  // D-Pad DESTRA o Stick DESTRA: Ruota mira in senso orario
  if ((dpad & DPAD_RIGHT_MASK) || axisX > ANALOG_DEADZONE) {
    if (now - gs.lastDpadRight > 80) {
      gs.lastDpadRight = now;
      zumaRotateAim(1);
    }
  }

  // Pulsante A: Spara pallina
  if (buttons & 0x0001) {
    if (now - gs.lastButtonA > 300) {
      gs.lastButtonA = now;
      zumaShoot();
    }
  }

  // Pulsante B: Scambia colore corrente con prossimo
  if (buttons & 0x0002) {
    if (now - gs.lastButtonB > 300) {
      gs.lastButtonB = now;
      zumaSwapColors();
    }
  }

  // NOTA: Tutti gli altri pulsanti (X, Y, SELECT, HOME, L, R, ecc.)
  // sono disabilitati e non fanno nulla in ZUMA
}
#endif

// Pagina web ZUMA
void handleZuma() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>ZUMA - Quetzalcoatl</title>";
  html += "<style>";
  html += "body{text-align:center;font-family:Arial,sans-serif;background:linear-gradient(135deg,#1a0a2e 0%,#2d1b4e 50%,#1a0a2e 100%);color:#fff;padding:20px;margin:0;}";
  html += ".container{max-width:600px;margin:0 auto;background:linear-gradient(180deg,#2d1b4e,#1a0a2e);padding:20px;border-radius:15px;border:3px solid #d4af37;box-shadow:0 0 30px rgba(212,175,55,0.3);}";
  html += ".score{font-size:28px;margin:15px;color:#fbbf24;text-shadow:0 0 10px #fbbf24;}";
  html += ".level{font-size:20px;color:#a78bfa;}";
  html += ".lives{font-size:22px;color:#f87171;margin:10px;}";
  html += ".controls{display:grid;grid-template-columns:repeat(3,70px);gap:8px;margin:20px auto;justify-content:center;}";
  html += ".controls button{height:70px;font-size:28px;border:none;border-radius:12px;background:linear-gradient(145deg,#4c1d95,#7c3aed);color:white;cursor:pointer;box-shadow:0 4px 15px rgba(124,58,237,0.4);}";
  html += ".controls button:active{transform:scale(0.95);background:linear-gradient(145deg,#7c3aed,#4c1d95);}";
  html += ".fire-btn{grid-column:span 3;height:80px!important;background:linear-gradient(145deg,#dc2626,#ef4444)!important;font-size:24px!important;box-shadow:0 4px 20px rgba(239,68,68,0.5)!important;}";
  html += ".swap-btn{background:linear-gradient(145deg,#0891b2,#06b6d4)!important;box-shadow:0 4px 15px rgba(6,182,212,0.4)!important;}";
  html += ".reset-btn{padding:15px 30px;background:linear-gradient(145deg,#059669,#10b981);color:white;border:none;border-radius:10px;font-size:18px;margin:15px;cursor:pointer;}";
  html += ".nav{text-align:center;margin-bottom:20px;}";
  html += ".nav a{color:#a78bfa;text-decoration:none;font-size:1.1em;padding:10px 20px;background:#4c1d95;border-radius:8px;display:inline-block;margin:5px;}";
  html += "h1{color:#fbbf24;text-shadow:0 0 20px #fbbf24;margin:10px 0;font-size:2em;}";
  html += ".aim-indicator{font-size:32px;margin:10px;color:#a78bfa;}";
  html += "#colorDisplay{display:flex;justify-content:center;gap:20px;margin:15px;}";
  html += ".color-ball{width:40px;height:40px;border-radius:50%;border:3px solid white;display:flex;align-items:center;justify-content:center;font-size:12px;}";
  // Icona Serpente Piumato Azteco (Quetzalcoatl)
  html += ".quetzal{width:80px;height:80px;margin:10px auto;position:relative;}";
  html += ".quetzal svg{width:100%;height:100%;filter:drop-shadow(0 0 10px #d4af37);}";
  html += ".quetzal-anim{animation:float 3s ease-in-out infinite;}";
  html += "@keyframes float{0%,100%{transform:translateY(0)}50%{transform:translateY(-5px)}}";
  html += ".aztec-border{border:3px solid transparent;border-image:repeating-linear-gradient(90deg,#d4af37,#d4af37 10px,#8b5cf6 10px,#8b5cf6 20px) 3;}";
  html += "</style>";
  html += "<script>";
  html += "function ctrl(cmd){fetch('/zumacontrol?'+cmd);}";
  html += "function updateStatus(){fetch('/zumacontrol?status=1').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('score').innerText=d.score;";
  html += "document.getElementById('level').innerText=d.level;";
  html += "document.getElementById('lives').innerText='\\u2764\\ufe0f '.repeat(d.lives);";
  html += "document.getElementById('aim').innerText=d.aim+'\\u00b0';";
  html += "document.getElementById('status').innerText=d.active?(d.gameover?'\\ud83d\\udd34 GAME OVER':'\\ud83d\\udfe2 In gioco'):'\\ud83d\\udfe1 Premi START';";
  html += "document.getElementById('currentColor').style.background=d.currentColor;";
  html += "document.getElementById('nextColor').style.background=d.nextColor;";
  html += "});}";
  html += "setInterval(updateStatus,200);";
  html += "document.addEventListener('keydown',function(e){";
  html += "e.preventDefault();";
  html += "if(e.key=='ArrowLeft')ctrl('aim=-1');";
  html += "else if(e.key=='ArrowRight')ctrl('aim=1');";
  html += "else if(e.key==' '||e.key=='ArrowUp')ctrl('shoot=1');";
  html += "else if(e.key=='s'||e.key=='ArrowDown')ctrl('swap=1');";
  html += "});";
  html += "function enableBT(){window.location='/enableBluetooth?game=zuma';}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='nav'>";
  html += "<a href='/'>&#127968; Menu</a>";
  html += "<a href='#' onclick='enableBT()' style='background:#2563eb;'>&#127918; Bluetooth</a>";
  html += "</div>";
  html += "<div class='container'>";
  // Icona SVG Rana Azteca di Pietra (ZUMA)
  html += "<div class='quetzal quetzal-anim'>";
  html += "<svg viewBox='0 0 100 100' xmlns='http://www.w3.org/2000/svg'>";
  // Base pietra azteca
  html += "<ellipse cx='50' cy='85' rx='35' ry='10' fill='#4a4a4a'/>";
  // Corpo rana (pietra verde-grigia)
  html += "<ellipse cx='50' cy='55' rx='30' ry='25' fill='#5d7a5d'/>";
  html += "<ellipse cx='50' cy='55' rx='28' ry='23' fill='#6b8b6b'/>";
  // Decorazioni azteche sul corpo
  html += "<path d='M35,50 L40,45 L45,50 L40,55 Z' fill='#d4af37'/>";
  html += "<path d='M55,50 L60,45 L65,50 L60,55 Z' fill='#d4af37'/>";
  html += "<path d='M45,60 L50,55 L55,60 L50,65 Z' fill='#d4af37'/>";
  // Testa rana
  html += "<ellipse cx='50' cy='35' rx='22' ry='18' fill='#6b8b6b'/>";
  // Occhi grandi sporgenti
  html += "<circle cx='38' cy='28' r='10' fill='#5d7a5d'/>";
  html += "<circle cx='62' cy='28' r='10' fill='#5d7a5d'/>";
  html += "<circle cx='38' cy='28' r='7' fill='#fbbf24'/>";
  html += "<circle cx='62' cy='28' r='7' fill='#fbbf24'/>";
  html += "<circle cx='38' cy='28' r='3' fill='#000'/>";
  html += "<circle cx='62' cy='28' r='3' fill='#000'/>";
  // Bocca (da dove spara le palline)
  html += "<ellipse cx='50' cy='42' rx='8' ry='5' fill='#3d5a3d'/>";
  // Pallina in bocca pronta a sparare
  html += "<circle cx='50' cy='42' r='4' fill='#ef4444'/>";
  // Zampe anteriori
  html += "<path d='M25,65 Q15,70 20,80 L28,75 L25,65' fill='#5d7a5d'/>";
  html += "<path d='M75,65 Q85,70 80,80 L72,75 L75,65' fill='#5d7a5d'/>";
  // Simboli aztechi sulla base
  html += "<rect x='30' y='82' width='6' height='6' fill='#d4af37' transform='rotate(45,33,85)'/>";
  html += "<rect x='64' y='82' width='6' height='6' fill='#d4af37' transform='rotate(45,67,85)'/>";
  html += "</svg></div>";
  html += "<h1>ZUMA</h1>";
  html += "<div style='color:#d4af37;font-size:14px;margin-bottom:15px;'>La Rana Azteca</div>";
  html += "<div class='level'>Livello: <span id='level'>1</span></div>";
  html += "<div class='score'>Punteggio: <span id='score'>0</span></div>";
  html += "<div class='lives' id='lives'>&#10084;&#65039; &#10084;&#65039; &#10084;&#65039; &#10084;&#65039; &#10084;&#65039;</div>";
  html += "<div id='status' style='font-size:18px;margin:10px;'>&#128994; In gioco</div>";
  html += "<div class='aim-indicator'>Mira: <span id='aim'>0</span></div>";
  html += "<div id='colorDisplay'>";
  html += "<div><div class='color-ball' id='currentColor'>ORA</div><small>Corrente</small></div>";
  html += "<div><div class='color-ball' id='nextColor'>NEXT</div><small>Prossimo</small></div>";
  html += "</div>";
  html += "<div class='controls'>";
  html += "<button onclick=\"ctrl('aim=-1')\">&#8634;</button>";
  html += "<button class='swap-btn' onclick=\"ctrl('swap=1')\">&#128260;</button>";
  html += "<button onclick=\"ctrl('aim=1')\">&#8635;</button>";
  html += "<button class='fire-btn' onclick=\"ctrl('shoot=1')\">&#128165; SPARA &#128165;</button>";
  html += "</div>";
  html += "<button class='reset-btn' onclick=\"ctrl('reset=1')\">&#128260; Nuova Partita</button>";
  html += "</div>";
  html += "<p style='color:#6b7280;margin-top:20px;font-size:14px;'>";
  html += "Tastiera: &#8592;&#8594; = Mira | Spazio = Spara | S = Scambia colore</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Controlli web ZUMA
void handleZumaControl() {
  if (server.hasArg("status")) {
    String colorNames[] = {"#ff0000", "#00ff00", "#0064ff", "#ffff00", "#ff00ff"};

    String json = "{";
    json += "\"score\":" + String(zumaScore) + ",";
    json += "\"level\":" + String(zumaLevel) + ",";
    json += "\"lives\":" + String(zumaLives) + ",";
    json += "\"aim\":" + String(zumaAimAngle) + ",";
    json += "\"active\":" + String(zumaGameActive ? "true" : "false") + ",";
    json += "\"gameover\":" + String(zumaGameOver ? "true" : "false") + ",";
    json += "\"currentColor\":\"" + colorNames[zumaShooterColor] + "\",";
    json += "\"nextColor\":\"" + colorNames[zumaNextColor] + "\"";
    json += "}";

    server.send(200, "application/json", json);
    return;
  }

  if (server.hasArg("reset")) {
    zumaLevel = 1;
    resetZuma();
    changeState(STATE_GAME_ZUMA);
  }

  if (server.hasArg("shoot")) {
    zumaShoot();
  }

  if (server.hasArg("aim")) {
    int dir = server.arg("aim").toInt();
    zumaRotateAim(dir);
  }

  if (server.hasArg("swap")) {
    zumaSwapColors();
  }

  server.send(200, "text/plain", "OK");
}
