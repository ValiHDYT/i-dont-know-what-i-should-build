/* ============================================================================
 * mp3_test.ino – MP3-Player TEST-SKETCH (Simulation ohne MP3-Modul)
 * ----------------------------------------------------------------------------
 * Was dieser Sketch kann:
 *   - Simuliert einen MP3-Player komplett auf dem OLED – ideal zum Testen
 *     des Layouts, BEVOR ein echtes MP3-Modul (z. B. DFPlayer Mini) da ist.
 *   - OK     = Play/Pause umschalten (Icon wechselt zwischen Pause und Play)
 *   - RIGHT  = nächster Song  (neuer Titel + neue ZUFALLS-Länge)
 *   - LEFT   = vorheriger Song (neuer Titel + neue ZUFALLS-Länge)
 *   - UP     = Lautstärke lauter
 *   - DOWN   = Lautstärke leiser
 *   - Die Spielzeit läuft in Echtzeit mit, der Fortschrittsbalken füllt sich,
 *     und am Ende eines Songs springt der Player automatisch weiter.
 *
 * Anschluss Display (I2C, 4 Drähte):
 *   OLED VCC  -> ESP32 3.3V
 *   OLED GND  -> ESP32 GND
 *   OLED SDA  -> ESP32 GPIO21
 *   OLED SCL  -> ESP32 GPIO22
 *
 * Anschluss Taster (je 2 Drähte):
 *   Jeder Taster: EIN Bein -> GND, ANDERES Bein -> GPIO-Pin.
 *   (INPUT_PULLUP erledigt den Rest – keine externen Widerstände nötig!)
 *
 *   UP    -> GPIO13
 *   DOWN  -> GPIO12   * ACHTUNG: GPIO12 ist ein "Strapping-Pin". Falls dein
 *                     * ESP32 nach dem Anschließen nicht mehr bootet, nimm
 *                     * für DOWN einfach GPIO25 (dann hier umbenennen).
 *   LEFT  -> GPIO14
 *   RIGHT -> GPIO27
 *   OK    -> GPIO26
 *
 * Benötigte Bibliotheken (Arduino IDE: Werkzeuge -> Bibliotheken verwalten):
 *   - "Adafruit SSD1306"  (Display-Treiber)
 *   - "Adafruit GFX"      (Grafik-Bibliothek, wird automatisch mitinstalliert)
 *   - "Adafruit BusIO"    (wird automatisch mitinstalliert)
 *
 * Board-Auswahl in der Arduino IDE:
 *   Werkzeuge -> Board -> esp32 -> "ESP32 Dev Module"
 * ========================================================================== */

#include <Wire.h>                  // I2C-Bibliothek (SDA/SCL)
#include <Adafruit_GFX.h>          // Grafik: drawPixel, drawRect, drawText, ...
#include <Adafruit_SSD1306.h>      // Der eigentliche Display-Treiber
#include <string.h>                // strlen() für die Titel-Zentrierung

// ---------- Konstanten: Display-Größe & I2C-Adresse -------------------------
#define SCREEN_WIDTH  128          // Pixel in X-Richtung
#define SCREEN_HEIGHT 64           // Pixel in Y-Richtung
#define OLED_ADDR     0x3C         // Standard-Adresse der meisten SSD1306-Module
                                   // (falls nichts angezeigt wird: 0x3D probieren)

// I2C-Pins des ESP32 (Standard: SDA=21, SCL=22).
// Wire.begin(sda, scl) setzt sie explizit – so funktioniert es auf jedem Board.
#define PIN_SDA 21
#define PIN_SCL 22

// ---------- Taster-Pins (D-Pad + OK) ----------------------------------------
#define BTN_UP    13
#define BTN_DOWN  12   // Strapping-Pin: bei Boot-Problemen auf 25 ändern!
#define BTN_LEFT  14
#define BTN_RIGHT 27
#define BTN_OK    26

#define BTN_COUNT 5
const int buttonPins[BTN_COUNT] = { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_OK };

// Indizes in den Arrays (0..4), damit wir nicht Pin-Nummern als
// Array-Index benutzen müssen:
enum { IDX_UP = 0, IDX_DOWN = 1, IDX_LEFT = 2, IDX_RIGHT = 3, IDX_OK = 4 };

#define DEBOUNCE_MS 50              // Entprellzeit (50 ms = Standard)

// ---------- Lautstärke ------------------------------------------------------
#define VOLUME_MAX 30               // DFPlayer-Wertebereich: 0 (leise) bis 30 (laut)
int volume = 15;                    // Startlautstärke (Mitte)

// ---------- Simulation: Playlist & Zeiten -----------------------------------
const char* songTitles[] = {        // Fiktive Titel zum Testen (ohne Umlaute,
  "Song 1 - Beat",                  // die Schrift kann nur ASCII-Zeichen)
  "Mein Track", 
  "Titel 3", 
  "Sunset Drive",
  "Test Nummer 5",
  "Lopaka Rock",
  "OLED Night",
  "Letzter Song"
};
const int SONG_COUNT = 8;           // Anzahl der Titel in der Liste

#define SONG_MIN_SEC 90             // kürzeste Zufalls-Länge (1:30)
#define SONG_MAX_SEC 300            // längste Zufalls-Länge (5:00)

bool isPlaying = false;             // true = Musik läuft gerade
int  songIndex = 0;                 // aktueller Song in der Liste
int  totalSec   = 0;                // Gesamtlänge des Songs (Zufall)
int  elapsedSec = 0;                // bereits abgespielte Sekunden
unsigned long lastTick = 0;         // Zeitpunkt des letzten Sekunden-Schritts

// ---------- Display-Objekt anlegen ------------------------------------------
// -1 = kein Reset-Pin (viele Module haben nur VCC/GND/SDA/SCL).
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- Funktions-Prototypen --------------------------------------------
// WICHTIG: Die Funktionen stehen weiter unten (nach loop()). Normalerweise
// erzeugt die Arduino IDE diese Prototypen automatisch – das klappt aber nicht
// immer zuverlässig. Deshalb schreiben wir sie explizit hin, damit der Sketch
// in JEDER IDE kompiliert.
void drawPlayer();
void setupButtons();
void updateButtons();
void handleButtons();
void startSong();
void nextSong();
void prevSong();
void updatePlayback();
void printTime(int sec, int x, int y);

// ---------- Zustand: Taster -------------------------------------------------
// Entprell-Stati pro Taster (0=UP, 1=DOWN, 2=LEFT, 3=RIGHT, 4=OK)
bool stableState[BTN_COUNT];        // aktueller stabiler Zustand (HIGH = nicht gedrückt)
bool lastRaw[BTN_COUNT];            // letzter gelesener Rohwert
unsigned long debounceTime[BTN_COUNT]; // Zeitpunkt der letzten Änderung
bool pressed[BTN_COUNT];            // Flanke: TRUE nur für EINEN loop-Durchlauf

/* ============================================================================
 * Icons (7x9) – gleiche Skip-Icons wie im Lopaka-Layout ("Layout vom Display"),
 * plus ein Play-Dreieck, das angezeigt wird, wenn die Musik pausiert ist.
 * 1 Bit pro Pixel, MSB zuerst.
 * ========================================================================== */
const unsigned char PROGMEM icon_next_7x9[] = {  // "nächster"
  0xA0, 0xD0, 0xD8, 0xDC, 0xDE, 0xDC, 0xD8, 0xD0, 0xA0
};
const unsigned char PROGMEM icon_back_7x9[] = {  // "zurück"
  0x0A, 0x16, 0x36, 0x76, 0xF6, 0x76, 0x36, 0x16, 0x0A
};
const unsigned char PROGMEM icon_play_7x9[] = {  // Play-Dreieck (bei Pause)
  0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0
};

/* ============================================================================
 * setup()  – läuft einmal beim Start.
 * ========================================================================== */
void setup()
{
  Serial.begin(115200);            // Serielle Ausgabe für Fehlermeldungen
  delay(100);                      // Kurz warten, damit der ESP32 hochfährt

  Wire.begin(PIN_SDA, PIN_SCL);    // I2C starten (Pins explizit angeben!)

  // Display initialisieren.
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 nicht gefunden – Kabel/Adresse pruefen!");
    while (true) {
      delay(1000);
    }
  }

  setupButtons();                  // Taster-Pins konfigurieren (INPUT_PULLUP)

  // Zufallsgenerator unterschiedlich "anwerfen" (sonst immer gleiche Längen):
  // Auf dem ESP32 gibt es dafür esp_random(); auf anderen Boards nehmen wir
  // einen unbeschalteten Analog-Pin.
#if defined(ESP32)
  randomSeed(esp_random());
#else
  randomSeed(analogRead(0));
#endif

  startSong();                     // ersten Song mit Zufalls-Länge laden
  drawPlayer();                    // Player-Screen anzeigen

  Serial.println("MP3-Test bereit!");
}

/* ============================================================================
 * loop() – läuft immer wieder von oben nach unten durch.
 * ========================================================================== */
void loop()
{
  updateButtons();                 // Taster einlesen + entprellen
  handleButtons();                 // Tastendrücke umsetzen
  updatePlayback();                // Spielzeit weiterschalten (wenn am Spielen)
  delay(10);                       // kurze Pause spart CPU
}

/* ============================================================================
 * Taster einlesen mit Entprellung.
 * Ergebnis: pressed[i] ist nur für EINEN loop-Durchlauf TRUE (eine "Flanke").
 * ========================================================================== */
void updateButtons()
{
  for (int i = 0; i < BTN_COUNT; i++) {
    pressed[i] = false;                            // Flanke zurücksetzen
    bool raw = digitalRead(buttonPins[i]);         // LOW = gedrückt

    if (raw != lastRaw[i]) {
      debounceTime[i] = millis();
    }
    lastRaw[i] = raw;

    if ((millis() - debounceTime[i]) > DEBOUNCE_MS) {
      if (raw != stableState[i]) {
        stableState[i] = raw;
        if (raw == LOW) {
          pressed[i] = true;       // nur "losgelassen -> gedrückt" zählt
        }
      }
    }
  }
}

// Konfiguriert alle Taster-Pins.
void setupButtons()
{
  for (int i = 0; i < BTN_COUNT; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    stableState[i] = HIGH;
    lastRaw[i] = HIGH;
    pressed[i] = false;
  }
}

/* ============================================================================
 * Tastendrücke in Aktionen umsetzen.
 * ========================================================================== */
void handleButtons()
{
  if (pressed[IDX_OK]) {                 // Play/Pause
    isPlaying = !isPlaying;
    lastTick = millis();                 // Zeit neu starten, damit nichts "nachspringt"
    drawPlayer();
  }
  if (pressed[IDX_RIGHT]) { nextSong(); }  // nächster Song
  if (pressed[IDX_LEFT])  { prevSong(); }  // vorheriger Song
  if (pressed[IDX_UP]) {                  // lauter
    if (volume < VOLUME_MAX) volume++;
    drawPlayer();
  }
  if (pressed[IDX_DOWN]) {                // leiser
    if (volume > 0) volume--;
    drawPlayer();
  }
}

/* ============================================================================
 * Spielzeit weiterschalten: jede Sekunde +1, Fortschrittsbalken füllt sich.
 * Läuft nur, wenn isPlaying == true. Am Song-Ende -> automatisch nächster.
 * ========================================================================== */
void updatePlayback()
{
  if (!isPlaying) return;

  unsigned long now = millis();
  if (now - lastTick >= 1000) {
    lastTick = now;
    elapsedSec++;

    if (elapsedSec >= totalSec) {
      nextSong();                          // Song fertig -> automatisch weiter
    } else {
      drawPlayer();                        // nur neu zeichnen, wenn sich was ändert
    }
  }
}

/* ============================================================================
 * Song anlegen: zufällige Gesamtlänge, Zeit auf 0.
 * ========================================================================== */
void startSong()
{
  totalSec   = random(SONG_MIN_SEC, SONG_MAX_SEC);
  elapsedSec = 0;
  lastTick   = millis();
  drawPlayer();
}

// Springt zum nächsten Song in der Liste (mit Zufalls-Länge).
void nextSong()
{
  songIndex = (songIndex + 1) % SONG_COUNT;
  startSong();
}

// Springt zum vorherigen Song in der Liste (mit Zufalls-Länge).
void prevSong()
{
  songIndex = (songIndex - 1 + SONG_COUNT) % SONG_COUNT;
  startSong();
}

/* ============================================================================
 * Player-Screen – dein Lopaka-Layout auf dem SSD1306-OLED:
 *   Titel oben, Fortschrittsbalken, Zeiten, Play/Pause + Skip-Icons unten.
 * ========================================================================== */
void drawPlayer()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Titel (oben, zentriert – egal wie lang der Titel ist)
  // 6 Pixel pro Zeichen bei Schriftgröße 1; Titel wird mittig platziert.
  const char* title = songTitles[songIndex];
  int titleLen = strlen(title) * 6;
  int titleX = (SCREEN_WIDTH - titleLen) / 2;
  if (titleX < 0) titleX = 0;              // nie links aus dem Bild raus
  display.setCursor(titleX, 2);
  display.print(title);

  // Lautstärke-Anzeige (unten links): "VOL" + Balken
  display.setCursor(8, 57);
  display.print("VOL");
  display.drawRect(30, 57, 32, 6, SSD1306_WHITE);
  display.fillRect(31, 58, (volume * 30) / VOLUME_MAX, 4, SSD1306_WHITE);

  // Fortschrittsbalken: Rahmen + Füllung (0..118 Pixel breit)
  display.drawRect(3, 35, 120, 4, SSD1306_WHITE);
  int progressWidth = (totalSec > 0) ? (long)elapsedSec * 118 / totalSec : 0;
  display.fillRect(4, 36, progressWidth, 2, SSD1306_WHITE);

  // Zeiten: bisherige Spielzeit (links) und Gesamtlänge (rechts)
  printTime(elapsedSec, 14, 47);
  printTime(totalSec, 96, 47);

  // Mittleres Symbol: Pause (zwei Balken) wenn am Spielen,
  // sonst Play-Dreieck (Pause-Zustand)
  if (isPlaying) {
    display.fillRect(61, 46, 3, 9, SSD1306_WHITE);
    display.fillRect(66, 46, 3, 9, SSD1306_WHITE);
  } else {
    display.drawBitmap(61, 46, icon_play_7x9, 7, 9, SSD1306_WHITE);
  }

  // Skip-Icons (7x9): zurück links, nächster rechts vom Pause-Symbol
  display.drawBitmap(52, 46, icon_back_7x9, 7, 9, SSD1306_WHITE);
  display.drawBitmap(71, 46, icon_next_7x9, 7, 9, SSD1306_WHITE);

  display.display();
}

/* ============================================================================
 * Zeit als "M:SS" ausgeben (z. B. 165 Sekunden -> "2:45").
 * ========================================================================== */
void printTime(int sec, int x, int y)
{
  int m = sec / 60;
  int s = sec % 60;

  display.setCursor(x, y);
  display.print(m);
  display.print(":");
  if (s < 10) {
    display.print("0");
  }
  display.print(s);
}
