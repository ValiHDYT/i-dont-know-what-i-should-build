/* ============================================================================
 * ESP32 MP3 Player.ino – MP3-Player mit SSD1306 OLED (128x64) + ESP32
 * ----------------------------------------------------------------------------
 * 6 Taster: D-Pad (UP/DOWN/LEFT/RIGHT/OK) + HOME
 * iPod-ähnliches Menüsystem:
 *   HOME  -> immer zum Hauptmenü
 *   OK    -> Auswahl bestätigen / Zurück (in Unter-Screens)
 *   UP/DOWN -> Menü-Punkt auswählen
 *   LEFT/RIGHT -> im Player: Skip / in Settings: Lautstärke
 *
 * Menü: [Player] [Songs] [Playlists] [Settings]
 *
 * Anschluss (I2C, 4 Drähte):
 *   OLED VCC -> 3.3V | GND -> GND | SDA -> GPIO21 | SCL -> GPIO22
 *
 * Taster (je 2 Drähte, INPUT_PULLUP, LOW = gedrückt):
 *   UP=13 | DOWN=12 | LEFT=14 | RIGHT=27 | OK=26
 *   HOME=32
 *
 * Potentiometer (3 Drähte):
 *   links -> 3.3V | Mitte -> GPIO35 | rechts -> GND
 * ========================================================================== */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>     // strlen()
#include <stdio.h>      // sprintf()
#include <stdlib.h>     // abs()

// ---------- Display ---------------------------------------------------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C
#define PIN_SDA 21
#define PIN_SCL 22

// ---------- Taster-Pins (6 Taster) ------------------------------------------
#define BTN_UP    13
#define BTN_DOWN  12   // Strapping-Pin: bei Boot-Problemen auf 25 ändern!
#define BTN_LEFT  14
#define BTN_RIGHT 27
#define BTN_OK    26
#define BTN_HOME  32   // HOME-Taster: Immer zum Menü

#define BTN_COUNT 6
const int buttonPins[BTN_COUNT] = {
  BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_OK, BTN_HOME
};
enum {
  IDX_UP = 0, IDX_DOWN = 1, IDX_LEFT = 2, IDX_RIGHT = 3,
  IDX_OK = 4, IDX_HOME = 5
};

#define DEBOUNCE_MS 50

// ---------- Display ---------------------------------------------------------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- Funktions-Prototypen --------------------------------------------
void drawPlayer();
void drawMenu();
void drawSongs();
void drawPlaylists();
void drawSettings();
void handleMenu();
void handlePlayer();
void handleSongs();
void handlePlaylists();
void handleSettings();
void setupButtons();
void updateButtons();
void updatePotVolume();
void printTime(int sec, int x, int y);

// ---------- Zustand: Taster -------------------------------------------------
bool stableState[BTN_COUNT];
bool lastRaw[BTN_COUNT];
unsigned long debounceTime[BTN_COUNT];
bool pressed[BTN_COUNT];

// ---------- Lautstärke (Potentiometer an GPIO35) ----------------------------
#define VOLUME_MAX 30
#define PIN_POT    35
#define POT_READ_MS 50
#define POT_HYST_RAW 60
int volume = 15;
unsigned long lastPotRead = 0;
int lastPotRaw = -10000;

// ---------- Zustandsmaschine ------------------------------------------------
#define STATE_MENU      0
#define STATE_PLAYER    1
#define STATE_SONGS     2
#define STATE_PLAYLISTS 3
#define STATE_SETTINGS  4

int currentState = STATE_MENU;
int menuPos = 0;

// ---------- Menü-Items ------------------------------------------------------
const int MENU_COUNT = 4;
const char* menuLabels[] = { "Player", "Songs", "Playlists", "Settings" };
const int menuTargets[] = { STATE_PLAYER, STATE_SONGS, STATE_PLAYLISTS, STATE_SETTINGS };

// ---------- Songs (Platzhalter – wird durch SD-Karte ersetzt) ---------------
const int SONG_COUNT = 0;              // 0 = keine Songs geladen

// ---------- Player -----------------------------------------------------------
bool isPlaying = false;
int  songIndex = 0;
int  totalSec = 0;
int  elapsedSec = 0;
unsigned long lastTick = 0;

// ---------- Songs -----------------------------------------------------------
int songsSel = 0;
int songsOffset = 0;
#define SONGS_COUNT (SONG_COUNT + 1)   // Songs + "Zurueck"

// ---------- Playlists --------------------------------------------------------
int playlistsSel = 0;
#define PLAYLISTS_COUNT 2              // Keine Playlist + "Zurueck"
const char* playlistLabels[] = { "No playlists yet", "Zurueck" };

// ---------- Settings ---------------------------------------------------------
int settingsSel = 0;
#define SETTINGS_COUNT 4
const char* settingsLabels[] = { "Volume", "Brightness", "EQ", "Zurueck" };

// ---------- Menü-Icons (7x9, PROGMEM) ---------------------------------------
const unsigned char PROGMEM icon_menu_player[] = {
  0x00, 0x20, 0x30, 0x38, 0x3C, 0x38, 0x30, 0x20, 0x00
};
const unsigned char PROGMEM icon_menu_songs[] = {
  0x00, 0x04, 0x04, 0x04, 0x7C, 0x44, 0x44, 0x7C, 0x00
};
const unsigned char PROGMEM icon_menu_playlists[] = {
  0x00, 0x7E, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00
};
const unsigned char PROGMEM icon_menu_settings[] = {
  0x00, 0x6C, 0x38, 0xFE, 0x38, 0x6C, 0x00, 0x00, 0x00
};
const unsigned char* menuIcons[] = {
  icon_menu_player, icon_menu_songs, icon_menu_playlists, icon_menu_settings
};

// ---------- Player-Icons (7x9) ----------------------------------------------
const unsigned char PROGMEM icon_next_7x9[] = {
  0xA0, 0xD0, 0xD8, 0xDC, 0xDE, 0xDC, 0xD8, 0xD0, 0xA0
};
const unsigned char PROGMEM icon_back_7x9[] = {
  0x0A, 0x16, 0x36, 0x76, 0xF6, 0x76, 0x36, 0x16, 0x0A
};
const unsigned char PROGMEM icon_play_7x9[] = {
  0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0
};

/* ============================================================================
 * setup()
 * ========================================================================== */
void setup()
{
  Serial.begin(115200);
  delay(100);
  Wire.begin(PIN_SDA, PIN_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 nicht gefunden!");
    while (true) { delay(1000); }
  }

  setupButtons();
  updatePotVolume();
  currentState = STATE_MENU;
  drawMenu();
  Serial.println("ESP32 MP3 Player bereit!");
}

/* ============================================================================
 * loop()
 * ========================================================================== */
void loop()
{
  updateButtons();
  updatePotVolume();

  // HOME-Taster: Immer zum Hauptmenü
  if (pressed[IDX_HOME]) {
    currentState = STATE_MENU;
    menuPos = 0;
    songsSel = 0;
    songsOffset = 0;
    playlistsSel = 0;
    settingsSel = 0;
    drawMenu();
    delay(10);
    return;
  }

  switch (currentState) {
    case STATE_MENU:      handleMenu();      break;
    case STATE_PLAYER:    handlePlayer();    break;
    case STATE_SONGS:     handleSongs();     break;
    case STATE_PLAYLISTS: handlePlaylists(); break;
    case STATE_SETTINGS:  handleSettings();  break;
  }

  delay(10);
}

/* ============================================================================
 * Menü-Handhabung
 * ========================================================================== */
void handleMenu()
{
  if (pressed[IDX_UP]) {
    menuPos = (menuPos - 1 + MENU_COUNT) % MENU_COUNT;
    drawMenu();
  }
  if (pressed[IDX_DOWN]) {
    menuPos = (menuPos + 1) % MENU_COUNT;
    drawMenu();
  }
  if (pressed[IDX_OK]) {
    currentState = menuTargets[menuPos];
    if (currentState == STATE_PLAYER) {
      drawPlayer();
    } else if (currentState == STATE_SONGS) {
      songsSel = 0;
      songsOffset = 0;
      drawSongs();
    } else if (currentState == STATE_PLAYLISTS) {
      playlistsSel = 0;
      drawPlaylists();
    } else if (currentState == STATE_SETTINGS) {
      settingsSel = 0;
      drawSettings();
    }
  }
}

/* ============================================================================
 * Player-Handhabung (Platzhalter – wird durch DFPlayer/SD ersetzt)
 * ========================================================================== */
void handlePlayer()
{
  if (pressed[IDX_OK]) {
    isPlaying = !isPlaying;
    lastTick = millis();
    drawPlayer();
  }
  // LEFT/RIGHT: Skip (noch leer – wird durch MP3-Modul ersetzt)
  if (pressed[IDX_LEFT])  { /* prevSong() */ drawPlayer(); }
  if (pressed[IDX_RIGHT]) { /* nextSong() */ drawPlayer(); }
  if (pressed[IDX_UP])    { if (volume < VOLUME_MAX) volume++; drawPlayer(); }
  if (pressed[IDX_DOWN])  { if (volume > 0) volume--; drawPlayer(); }
}

/* ============================================================================
 * Songs-Handhabung (Platzhalter)
 * ========================================================================== */
void handleSongs()
{
  if (pressed[IDX_UP]) {
    songsSel = (songsSel - 1 + SONGS_COUNT) % SONGS_COUNT;
    drawSongs();
  }
  if (pressed[IDX_DOWN]) {
    songsSel = (songsSel + 1) % SONGS_COUNT;
    drawSongs();
  }
  if (pressed[IDX_OK]) {
    if (songsSel == SONGS_COUNT - 1) {
      currentState = STATE_MENU;
      drawMenu();
    }
  }
}

/* ============================================================================
 * Playlists-Handhabung (Platzhalter)
 * ========================================================================== */
void handlePlaylists()
{
  if (pressed[IDX_UP]) {
    playlistsSel = (playlistsSel - 1 + PLAYLISTS_COUNT) % PLAYLISTS_COUNT;
    drawPlaylists();
  }
  if (pressed[IDX_DOWN]) {
    playlistsSel = (playlistsSel + 1) % PLAYLISTS_COUNT;
    drawPlaylists();
  }
  if (pressed[IDX_OK]) {
    if (playlistsSel == PLAYLISTS_COUNT - 1) {
      currentState = STATE_MENU;
      drawMenu();
    }
  }
}

/* ============================================================================
 * Settings-Handhabung
 * ========================================================================== */
void handleSettings()
{
  if (pressed[IDX_UP]) {
    settingsSel = (settingsSel - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
    drawSettings();
  }
  if (pressed[IDX_DOWN]) {
    settingsSel = (settingsSel + 1) % SETTINGS_COUNT;
    drawSettings();
  }
  if (pressed[IDX_OK]) {
    if (settingsSel == SETTINGS_COUNT - 1) {
      currentState = STATE_MENU;
      drawMenu();
    } else {
      drawSettings();
    }
  }
  if (settingsSel == 0) {
    if (pressed[IDX_LEFT])  { if (volume > 0) volume--; drawSettings(); }
    if (pressed[IDX_RIGHT]) { if (volume < VOLUME_MAX) volume++; drawSettings(); }
  }
}

/* ============================================================================
 * Zeichnen: Hauptmenü
 * ========================================================================== */
void drawMenu()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(34, 2);
  display.print("MP3 Player");

  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 17 + i * 11;

    if (i == menuPos) {
      display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.drawBitmap(8, y, menuIcons[i], 7, 9, (i == menuPos) ? SSD1306_BLACK : SSD1306_WHITE);

    display.setCursor(22, y);
    display.print(menuLabels[i]);

    if (i == menuPos) {
      display.setCursor(118, y);
      display.print(">");
    }
  }  display.setTextColor(SSD1306_WHITE);
  display.display();
}

/* ============================================================================
 * Zeichnen: Player-Screen
 * ========================================================================== */
void drawPlayer()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Titel (Platzhalter)
  display.setCursor(34, 2);
  display.print("No Song");

  // Fortschrittsbalken (leer)
  display.drawRect(3, 35, 120, 4, SSD1306_WHITE);

  // Zeiten (leer)
  display.setCursor(14, 47);
  display.print("0:00");
  display.setCursor(96, 47);
  display.print("0:00");

  // Play/Pause-Symbol
  if (isPlaying) {
    display.fillRect(61, 46, 3, 9, SSD1306_WHITE);
    display.fillRect(66, 46, 3, 9, SSD1306_WHITE);
  } else {
    display.drawBitmap(61, 46, icon_play_7x9, 7, 9, SSD1306_WHITE);
  }

  // Skip-Icons
  display.drawBitmap(52, 46, icon_back_7x9, 7, 9, SSD1306_WHITE);
  display.drawBitmap(71, 46, icon_next_7x9, 7, 9, SSD1306_WHITE);

  // Lautstärke-Anzeige
  display.setCursor(8, 57);
  display.print("VOL");
  display.drawRect(30, 57, 32, 6, SSD1306_WHITE);
  display.fillRect(31, 58, (volume * 30) / VOLUME_MAX, 4, SSD1306_WHITE);

  display.display();
}

/* ============================================================================
 * Zeichnen: Songs-Screen (Platzhalter)
 * ========================================================================== */
void drawSongs()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(34, 2);
  display.print("Songs");
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  // Nur "Zurueck" anzeigen wenn keine Songs
  if (SONG_COUNT == 0) {
    int y = 24;
    display.setCursor(10, y);
    display.print("Keine Songs geladen");
    // Zurueck
    y = 38;
    if (songsSel == 0) {
      display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(10, y);
    display.print("Zurueck");
    if (songsSel == 0) {
      display.setCursor(118, y);
      display.print(">");
    }
  } else {
    int maxVisible = 4;
    if (songsSel < songsOffset) songsOffset = songsSel;
    if (songsSel >= songsOffset + maxVisible) songsOffset = songsSel - maxVisible + 1;

    for (int v = 0; v < maxVisible; v++) {
      int i = songsOffset + v;
      if (i >= SONGS_COUNT) break;
      int y = 16 + v * 10;
      bool isSelected = (i == songsSel);

      if (isSelected) {
        display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(10, y);
      if (i < SONG_COUNT) {
        // TODO: SD-Karten-Songliste hier einfuegen
        display.print("Song ");
        display.print(i + 1);
      } else {
        display.print("Zurueck");
      }

      if (isSelected) {
        display.setCursor(118, y);
        display.print(">");
      }
    }

    display.setTextColor(SSD1306_WHITE);
    if (songsOffset > 0) {
      display.setCursor(120, 16);
      display.print("^");
    }
    if (songsOffset + maxVisible < SONGS_COUNT) {
      display.setCursor(120, 52);
      display.print("v");
    }
  }

  display.display();
}

/* ============================================================================
 * Zeichnen: Playlists-Screen (Platzhalter)
 * ========================================================================== */
void drawPlaylists()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(25, 2);
  display.print("Playlists");
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  for (int i = 0; i < PLAYLISTS_COUNT; i++) {
    int y = 18 + i * 12;
    bool isSelected = (i == playlistsSel);

    if (isSelected) {
      display.fillRect(0, y - 1, 128, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(10, y);
    display.print(playlistLabels[i]);

    if (isSelected) {
      display.setCursor(118, y);
      display.print(">");
    }
  }

  display.setTextColor(SSD1306_WHITE);
  display.display();
}

/* ============================================================================
 * Zeichnen: Settings-Screen
 * ========================================================================== */
void drawSettings()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(28, 2);
  display.print("Settings");
  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = 16 + i * 10;

    if (i == settingsSel) {
      display.fillRect(0, y - 1, 128, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(10, y);
    display.print(settingsLabels[i]);

    if (i == 0) {
      display.setCursor(90, y);
      char buf[8];
      sprintf(buf, "%d/%d", volume, VOLUME_MAX);
      display.print(buf);
    } else if (i == 1) {
      display.setCursor(90, y);
      display.print("100%");
    } else if (i == 2) {
      display.setCursor(90, y);
      display.print("Normal");
    }

    if (i == settingsSel) {
      display.setCursor(118, y);
      display.print(">");
    }
  }

  display.setTextColor(SSD1306_WHITE);
  display.display();
}

/* ============================================================================
 * Zeit als "M:SS" ausgeben
 * ========================================================================== */
void printTime(int sec, int x, int y)
{
  int m = sec / 60;
  int s = sec % 60;
  display.setCursor(x, y);
  display.print(m);
  display.print(":");
  if (s < 10) display.print("0");
  display.print(s);
}

/* ============================================================================
 * Taster entprellen
 * ========================================================================== */
void updateButtons()
{
  for (int i = 0; i < BTN_COUNT; i++) {
    pressed[i] = false;
    bool raw = digitalRead(buttonPins[i]);

    if (raw != lastRaw[i]) {
      debounceTime[i] = millis();
    }
    lastRaw[i] = raw;

    if ((millis() - debounceTime[i]) > DEBOUNCE_MS) {
      if (raw != stableState[i]) {
        stableState[i] = raw;
        if (raw == LOW) {
          pressed[i] = true;
        }
      }
    }
  }
}

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
 * Potentiometer lesen (8er-Mittelung + Hysterese)
 * ========================================================================== */
void updatePotVolume()
{
  if (millis() - lastPotRead < POT_READ_MS) return;
  lastPotRead = millis();

  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(PIN_POT);
  }
  int raw = sum / 8;

  if (abs(raw - lastPotRaw) <= POT_HYST_RAW) return;
  lastPotRaw = raw;

  int newVol = map(raw, 0, 4095, 0, VOLUME_MAX);
  if (newVol != volume) {
    volume = newVol;
    if (currentState == STATE_PLAYER) drawPlayer();
    if (currentState == STATE_SETTINGS) drawSettings();
  }
}
