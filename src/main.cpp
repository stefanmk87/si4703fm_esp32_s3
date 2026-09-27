#include <Arduino.h>
#include <Wire.h>
#include <SI470X.h>
#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include <string.h>
#include <time.h>

#define RESET_PIN 6       
#define ESP32_I2C_SDA 4
#define ESP32_I2C_SCL 5

// Авторот препорачува 40ms или помалку за polling метод
#define MAX_DELAY_RDS 40   
long rds_elapsed = millis();

SI470X rx;
TFT_eSPI tft;

constexpr uint8_t SEEK_UP_BUTTON_PIN = 14;
constexpr uint8_t SEEK_DOWN_BUTTON_PIN = 15;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 35;

struct DebouncedButton
{
  uint8_t pin;
  bool lastReading;
  bool stableState;
  unsigned long changedAt;
};

DebouncedButton seekUpButton = {SEEK_UP_BUTTON_PIN, HIGH, HIGH, 0};
DebouncedButton seekDownButton = {SEEK_DOWN_BUTTON_PIN, HIGH, HIGH, 0};

#define SCAN_STATION_RSSI_THRESHOLD 40
char psAssembly[9] = {};
char psVoteCandidate[9] = {};
char lastRdsText[9] = {};
uint8_t psSegmentMask = 0;
uint8_t psRepeatCount = 0;
char currentRadioText[65] = {};
char radioTextAssembly[65] = {};
uint16_t radioTextSegmentMask = 0;
uint16_t currentProgramId = 0;
uint8_t currentPty = 0;
uint8_t ptyCandidate = 0;
uint8_t ptyCandidateCount = 0;
bool currentTp = false;
bool currentTa = false;
bool currentMusicSpeech = false;
bool currentRdsMetadataValid = false;
bool renderedRdsMetadataValid = false;
uint16_t renderedProgramId = 0;
uint8_t renderedPty = 0;
bool renderedTp = false;
bool renderedTa = false;
bool renderedMusicSpeech = false;
char currentRdsClock[6] = "--:--";
char renderedRdsClock[6] = {};
bool radioTextVersionB = false;
bool radioTextFlagAB = false;
bool radioTextFlagKnown = false;
bool radioTextHasEnd = false;
uint8_t radioTextEndSegment = 0xFF;
bool fileSystemReady = false;
char scanDateTime[17] = "NOT SET";
time_t scanDateTimeBase = 0;
unsigned long scanDateTimeSetAt = 0;
bool scanDateTimeIsSet = false;
uint16_t lastDisplayFrequency = 0xFFFF;
int lastDisplayRssi = -1;
bool lastDisplayStereo = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastSignalRenderAt = 0;
float filteredRssi = 0.0f;
bool rssiFilterInitialized = false;
bool stereoCandidate = false;
bool stereoCandidateValid = false;
unsigned long stereoCandidateSince = 0;

void drawStationName(const char *stationName)
{
  tft.fillRect(8, 112, tft.width() - 16, 31, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString(stationName, tft.width() / 2, 127, 4);
}

void drawRdsMetadata();
void drawSectionDivider(int y);

void resetStationName()
{
  memset(psAssembly, 0, sizeof(psAssembly));
  memset(psVoteCandidate, 0, sizeof(psVoteCandidate));
  lastRdsText[0] = '\0';
  psSegmentMask = 0;
  psRepeatCount = 0;
  memset(radioTextAssembly, 0, sizeof(radioTextAssembly));
  currentRadioText[0] = '\0';
  radioTextSegmentMask = 0;
  radioTextFlagKnown = false;
  radioTextHasEnd = false;
  radioTextEndSegment = 0xFF;
  currentRdsMetadataValid = false;
  renderedRdsMetadataValid = false;
  currentMusicSpeech = false;
  strcpy(currentRdsClock, "--:--");
  tft.fillRect(8, 112, tft.width() - 16, 31, TFT_BLACK);
  tft.fillRect(10, 145, tft.width() - 20, 42, TFT_BLACK);
  drawSectionDivider(159);
  drawRdsMetadata();
}

void drawRadioDisplay(bool force = false)
{
  if (!force && millis() - lastDisplayUpdate < 250)
    return;
  lastDisplayUpdate = millis();

  uint16_t frequency = rx.getFrequency();
  int rawRssi = rx.getRssi();
  bool stereo = rx.isStereo();
  if (!rssiFilterInitialized)
  {
    filteredRssi = rawRssi;
    rssiFilterInitialized = true;
  }
  else
  {
    filteredRssi += 0.22f * (rawRssi - filteredRssi);
  }
  int rssi = static_cast<int>(filteredRssi + 0.5f);

  if (!stereoCandidateValid || stereo != stereoCandidate)
  {
    stereoCandidate = stereo;
    stereoCandidateSince = millis();
    stereoCandidateValid = true;
  }

  if (force || frequency != lastDisplayFrequency)
  {
    lastDisplayFrequency = frequency;
    tft.fillRect(0, 31, tft.width(), 78, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawFloat(frequency / 100.0f, 1, tft.width() / 2, 70, 7);
  }

    bool signalChanged = force || lastDisplayRssi < 0 ||
      (rssi != lastDisplayRssi &&
       (abs(rssi - lastDisplayRssi) >= 2 || millis() - lastSignalRenderAt >= 1800));
    bool stereoChanged = force ||
      (stereoCandidate != lastDisplayStereo && millis() - stereoCandidateSince >= 500);

  if (signalChanged)
  {
    lastDisplayRssi = rssi;
    lastSignalRenderAt = millis();
    tft.fillRect(78, 190, 118, 21, TFT_BLACK);
    tft.setTextColor(rssi >= 45 ? TFT_GREEN : (rssi >= 30 ? TFT_YELLOW : TFT_ORANGE), TFT_BLACK);
    char signalText[20];
    snprintf(signalText, sizeof(signalText), "%d dBuV", rssi);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(signalText, 82, 201, 2);
  }

  if (stereoChanged)
  {
    lastDisplayStereo = stereoCandidate;
    tft.fillRect(220, 190, 88, 21, TFT_BLACK);
    tft.setTextColor(stereoCandidate ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(stereoCandidate ? "STEREO" : "MONO", 308, 201, 2);
  }
}

void drawSectionDivider(int y)
{
  tft.drawFastHLine(12, y, tft.width() - 24, TFT_DARKGREY);
}

void initializeRadioDisplay()
{
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("FM RADIO", 12, 8, 2);
  drawSectionDivider(27);
  drawSectionDivider(109);
  drawSectionDivider(143);
  drawSectionDivider(159);
  drawSectionDivider(188);
  drawSectionDivider(214);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("SIGNAL", 12, 201, 2);
  tft.drawString("STEREO", 220, 201, 2);
}

const char *ptyNames[32] = {
  "None", "News", "Current Affairs", "Information", "Sport", "Education",
  "Drama", "Culture", "Science", "Varied", "Pop Music", "Rock Music",
  "Easy Listening", "Light Classical", "Serious Classical", "Other Music",
  "Weather", "Finance", "Children's", "Social Affairs", "Religion",
  "Phone In", "Travel", "Leisure", "Jazz", "Country", "National Music",
  "Oldies", "Folk", "Documentary", "Alarm Test", "Alarm"
};

char cleanPsCharacter(uint8_t value)
{
  if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
      (value >= '0' && value <= '9') || value == ' ')
    return static_cast<char>(value);
  return ' ';
}

void drawRdsMetadata()
{
  if (!currentRdsMetadataValid)
  {
    if (renderedRdsMetadataValid)
      tft.fillRect(10, 160, tft.width() - 20, 26, TFT_BLACK);
    renderedRdsMetadataValid = false;
    return;
  }

  char programIdText[12];
  char ptyText[28];
  char tpText[8];
  char taText[8];
  char musicSpeechText[16];
  char clockText[12];
  snprintf(programIdText, sizeof(programIdText), "PI:%04X", currentProgramId);
  snprintf(ptyText, sizeof(ptyText), "PTY:%s", ptyNames[currentPty]);
  snprintf(tpText, sizeof(tpText), "TP:%c", currentTp ? 'Y' : 'N');
  snprintf(taText, sizeof(taText), "TA:%c", currentTa ? 'Y' : 'N');
  snprintf(musicSpeechText, sizeof(musicSpeechText), "M/S:%s",
           currentMusicSpeech ? "MUSIC" : "SPEECH");
  snprintf(clockText, sizeof(clockText), "CT:%s", currentRdsClock);
  bool renderAll = !renderedRdsMetadataValid;
  if (renderAll || renderedProgramId != currentProgramId)
  {
    tft.fillRect(12, 160, 75, 14, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(programIdText, 12, 168, 1);
  }
  if (renderAll || renderedPty != currentPty)
  {
    tft.fillRect(96, 160, 212, 14, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(ptyText, 96, 168, 1);
  }
  if (renderAll || renderedTp != currentTp)
  {
    tft.fillRect(12, 172, 46, 14, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(tpText, 12, 180, 1);
  }
  if (renderAll || renderedTa != currentTa)
  {
    tft.fillRect(72, 172, 46, 14, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(taText, 72, 180, 1);
  }
  if (renderAll || renderedMusicSpeech != currentMusicSpeech)
  {
    tft.fillRect(132, 172, 96, 14, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(musicSpeechText, 132, 180, 1);
  }
  if (renderAll || strcmp(renderedRdsClock, currentRdsClock) != 0)
  {
    tft.fillRect(232, 172, 76, 14, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(clockText, 232, 180, 1);
  }
  renderedProgramId = currentProgramId;
  renderedPty = currentPty;
  renderedTp = currentTp;
  renderedTa = currentTa;
  renderedMusicSpeech = currentMusicSpeech;
  memcpy(renderedRdsClock, currentRdsClock, sizeof(renderedRdsClock));
  renderedRdsMetadataValid = true;
}

void drawRadioText(const char *text)
{
  tft.fillRect(10, 145, tft.width() - 20, 13, TFT_BLACK);
  if (text[0] == '\0')
    return;

  constexpr size_t displayBufferSize = 65;
  const int maxTextWidth = tft.width() - 40;
  char displayText[displayBufferSize];
  size_t textLength = strlen(text);
  size_t visibleLength = textLength;

  while (visibleLength > 3)
  {
    memcpy(displayText, text, visibleLength);
    displayText[visibleLength] = '\0';
    if (tft.textWidth(displayText, 1) <= maxTextWidth)
      break;
    visibleLength--;
  }

  if (visibleLength < textLength)
  {
    while (visibleLength > 0)
    {
      memcpy(displayText, text, visibleLength);
      memcpy(displayText + visibleLength, "...", 4);
      if (tft.textWidth(displayText, 1) <= maxTextWidth)
        break;
      visibleLength--;
    }
    if (visibleLength == 0)
      strcpy(displayText, "...");
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(displayText, tft.width() / 2, 151, 1);
}

void acceptCompletePs()
{
  psAssembly[8] = '\0';
  for (int8_t i = 7; i >= 0 && psAssembly[i] == ' '; i--)
    psAssembly[i] = '\0';

  uint8_t alphanumericCount = 0;
  for (uint8_t i = 0; psAssembly[i] != '\0'; i++)
    if (psAssembly[i] != ' ')
      alphanumericCount++;
  if (alphanumericCount < 3)
    return;

  if (strcmp(psAssembly, psVoteCandidate) == 0)
  {
    if (psRepeatCount < 3)
      psRepeatCount++;
  }
  else
  {
    memcpy(psVoteCandidate, psAssembly, sizeof(psVoteCandidate));
    psRepeatCount = 1;
  }

  if (psRepeatCount >= 2 && strcmp(psAssembly, lastRdsText) != 0)
  {
    memcpy(lastRdsText, psAssembly, sizeof(lastRdsText));
    drawStationName(lastRdsText);
    Serial.printf("[RDS PS]: %s\r\n", lastRdsText);
  }
}

void processRadioText(uint16_t blockB, uint16_t blockC, uint16_t blockD)
{
  bool versionB = (blockB & 0x0800) != 0;
  bool textFlagAB = (blockB & 0x0010) != 0;
  if (!radioTextFlagKnown || radioTextVersionB != versionB || radioTextFlagAB != textFlagAB)
  {
    memset(radioTextAssembly, 0, sizeof(radioTextAssembly));
    currentRadioText[0] = '\0';
    radioTextSegmentMask = 0;
    radioTextHasEnd = false;
    radioTextEndSegment = 0xFF;
    radioTextVersionB = versionB;
    radioTextFlagAB = textFlagAB;
    radioTextFlagKnown = true;
    drawRadioText("");
  }

  uint8_t segment = blockB & 0x0F;
  uint8_t charactersPerGroup = versionB ? 2 : 4;
  uint8_t startIndex = segment * charactersPerGroup;
  uint8_t groupCharacters[4] = {
    static_cast<uint8_t>(blockC >> 8),
    static_cast<uint8_t>(blockC & 0xFF),
    static_cast<uint8_t>(blockD >> 8),
    static_cast<uint8_t>(blockD & 0xFF)
  };

  uint8_t firstCharacter = versionB ? 2 : 0;
  for (uint8_t i = firstCharacter; i < 4; i++)
  {
    uint8_t position = startIndex + i - firstCharacter;
    uint8_t value = groupCharacters[i];
    if (value == 13)
    {
      radioTextAssembly[position] = '\0';
      radioTextHasEnd = true;
      radioTextEndSegment = segment;
      break;
    }
    radioTextAssembly[position] = (value >= 32 && value <= 126)
                                      ? static_cast<char>(value)
                                      : ' ';
  }

  radioTextSegmentMask |= static_cast<uint16_t>(1U << segment);
  uint16_t requiredSegments = 0xFFFF;
  if (radioTextHasEnd)
    requiredSegments = static_cast<uint16_t>((1U << (radioTextEndSegment + 1)) - 1);

  if ((radioTextSegmentMask & requiredSegments) != requiredSegments)
    return;

  char cleanText[65] = {};
  uint8_t textLimit = versionB ? 32 : 64;
  uint8_t outputIndex = 0;
  while (outputIndex < textLimit && radioTextAssembly[outputIndex] != '\0')
  {
    uint8_t value = static_cast<uint8_t>(radioTextAssembly[outputIndex]);
    if (value >= 32 && value <= 126)
      cleanText[outputIndex] = static_cast<char>(value);
    outputIndex++;
  }
  while (outputIndex > 0 && cleanText[outputIndex - 1] == ' ')
    cleanText[--outputIndex] = '\0';

  if (outputIndex >= 3 && strcmp(cleanText, currentRadioText) != 0)
  {
    memcpy(currentRadioText, cleanText, sizeof(currentRadioText));
    drawRadioText(currentRadioText);
  }
}

void processRdsGroup()
{
  if (!rx.getRdsReady())
    return;

  rx.getRdsStatus();
  uint16_t status = rx.getShadownRegister(0x0A);
  uint16_t readChannel = rx.getShadownRegister(0x0B);
  uint16_t blockA = rx.getShadownRegister(0x0C);
  uint16_t blockB = rx.getShadownRegister(0x0D);
  uint8_t groupType = (blockB >> 12) & 0x0F;
  uint8_t blockAErrors = (status >> 9) & 0x03;
  uint8_t blockBErrors = (readChannel >> 12) & 0x03;
  uint8_t blockCErrors = (readChannel >> 10) & 0x03;
  uint8_t blockDErrors = (readChannel >> 14) & 0x03;

  if (blockAErrors < 2 && blockBErrors < 2)
  {
    uint8_t incomingPty = (blockB >> 5) & 0x1F;
    bool stationChanged = !currentRdsMetadataValid || currentProgramId != blockA;
    if (stationChanged)
    {
      currentProgramId = blockA;
      currentPty = incomingPty;
      ptyCandidate = incomingPty;
      ptyCandidateCount = 0;
    }
    else if (incomingPty == currentPty)
    {
      ptyCandidateCount = 0;
    }
    else if (incomingPty == ptyCandidate)
    {
      if (ptyCandidateCount < 3)
        ptyCandidateCount++;
      if (ptyCandidateCount >= 3)
      {
        currentPty = incomingPty;
        ptyCandidateCount = 0;
      }
    }
    else
    {
      ptyCandidate = incomingPty;
      ptyCandidateCount = 1;
    }

    currentTp = (blockB & 0x0400) != 0;
    currentRdsMetadataValid = true;
    if (groupType == 0)
    {
      currentTa = (blockB & 0x0010) != 0;
      currentMusicSpeech = (blockB & 0x0008) != 0;
    }
    if (groupType == 4 && ((blockB >> 11) & 0x01) == 0 &&
        blockCErrors < 2 && blockDErrors < 2)
    {
      char *clockText = rx.getRdsLocalTime();
      if (clockText != nullptr && strlen(clockText) >= 5)
      {
        memcpy(currentRdsClock, clockText, 5);
        currentRdsClock[5] = '\0';
      }
    }
    drawRdsMetadata();
  }

  if (groupType == 0 && blockBErrors < 2 && blockDErrors < 2)
  {
    uint8_t segment = blockB & 0x03;
    uint16_t blockD = rx.getShadownRegister(0x0F);
    psAssembly[segment * 2] = cleanPsCharacter(blockD >> 8);
    psAssembly[segment * 2 + 1] = cleanPsCharacter(blockD & 0xFF);
    psSegmentMask |= 1U << segment;

    if (psSegmentMask == 0x0F)
    {
      psSegmentMask = 0;
      acceptCompletePs();
    }
  }

  if (groupType == 2 && blockBErrors < 2 && blockCErrors < 2 && blockDErrors < 2)
    processRadioText(blockB, rx.getShadownRegister(0x0E), rx.getShadownRegister(0x0F));
}

void showHelp()
{
  Serial.println("\n==================================================");
  Serial.println("Type U to increase and D to decrease the frequency");
  Serial.println("Type S or s to seek station Up or Down");
  Serial.println("GPIO14 to GND: Seek UP | GPIO15 to GND: Seek DOWN");
  Serial.println("Type + or - to volume Up or Down");
  Serial.println("Type F to scan with low-pass filter");
  Serial.println("Type N to scan without low-pass filter");
  Serial.println("Type TYYYY-MM-DD HH:MM to set local scan date/time");
  Serial.println("Type L to print saved scan files");
  Serial.println("Type 0 to show current status");
  Serial.println("Type ? to this help.");
  Serial.println("==================================================");
}

void showStatus()
{
  char aux[100];
  sprintf(aux,"\nTuned: %u MHz | RSSI: %3u dbuV | Vol: %2u | Stereo: %s", rx.getFrequency(), rx.getRssi(), rx.getVolume(), (rx.isStereo()) ? "Yes" : "No" );
  Serial.println(aux);
}

void pollSeekButton(DebouncedButton &button, uint8_t direction, const char *label)
{
  bool reading = digitalRead(button.pin);
  if (reading != button.lastReading)
  {
    button.lastReading = reading;
    button.changedAt = millis();
  }

  if (millis() - button.changedAt < BUTTON_DEBOUNCE_MS || reading == button.stableState)
    return;

  button.stableState = reading;
  if (button.stableState == LOW)
  {
    Serial.printf("Seeking %s...\r\n", label);
    rx.seek(SI470X_SEEK_WRAP, direction);
    resetStationName();
    showStatus();
  }
}

void setScanDateTime()
{
  String input = Serial.readStringUntil('\n');
  input.trim();

  int year, month, day, hour, minute;
  char extra;
  if (sscanf(input.c_str(), "%4d-%2d-%2d %2d:%2d%c",
             &year, &month, &day, &hour, &minute, &extra) != 5 ||
      year < 2000 || year > 2099 || month < 1 || month > 12 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59)
  {
    Serial.println("Invalid date/time. Send: TYYYY-MM-DD HH:MM");
    return;
  }

  const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t maxDay = daysPerMonth[month - 1];
  bool leapYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leapYear)
    maxDay = 29;
  if (day < 1 || day > maxDay)
  {
    Serial.println("Invalid calendar day.");
    return;
  }

  snprintf(scanDateTime, sizeof(scanDateTime), "%04d-%02d-%02d %02d:%02d",
           year, month, day, hour, minute);
  struct tm enteredTime = {};
  enteredTime.tm_year = year - 1900;
  enteredTime.tm_mon = month - 1;
  enteredTime.tm_mday = day;
  enteredTime.tm_hour = hour;
  enteredTime.tm_min = minute;
  enteredTime.tm_isdst = -1;
  scanDateTimeBase = mktime(&enteredTime);
  scanDateTimeSetAt = millis();
  scanDateTimeIsSet = scanDateTimeBase >= 0;
  Serial.printf("Scan date/time set (local): %s\r\n", scanDateTime);
}

void scanBand(const char *path, const char *filterMode)
{
  if (!fileSystemReady)
  {
    Serial.println("SPIFFS is not available; scan was not saved.");
    return;
  }

  const uint16_t originalFrequency = rx.getFrequency();
  if (SPIFFS.exists(path))
  {
    SPIFFS.remove(path);
  }

  File scanFile = SPIFFS.open(path, FILE_WRITE);
  if (!scanFile)
  {
    Serial.println("Could not create scan file.");
    return;
  }

  char scanTimestamp[17] = "NOT SET";
  if (scanDateTimeIsSet)
  {
    time_t scanTime = scanDateTimeBase + (millis() - scanDateTimeSetAt) / 1000;
    struct tm scanTimeParts;
    if (localtime_r(&scanTime, &scanTimeParts) != NULL)
      strftime(scanTimestamp, sizeof(scanTimestamp), "%Y-%m-%d %H:%M", &scanTimeParts);
  }

  scanFile.printf("FM BAND SCAN | LOW-PASS FILTER: %s\r\n", filterMode);
  scanFile.printf("Scan date/time (local): %s\r\n", scanTimestamp);
  scanFile.printf("Candidate rule: AFC valid and RSSI >= %d dBuV\r\n",
                  SCAN_STATION_RSSI_THRESHOLD);
  scanFile.println("+------------+------------+--------+-----------+-----------------+");
  scanFile.println("| Freq MHz   | RSSI dBuV  | Stereo | AFC valid | Station cand.   |");
  scanFile.println("+------------+------------+--------+-----------+-----------------+");

  Serial.printf("Starting full FM scan (%s), saving to %s\r\n", filterMode, path);
  uint16_t stationCandidates = 0;
  uint16_t point = 0;

  for (uint16_t frequency = 8750; frequency <= 10800; frequency += 10)
  {
    // The library's setSpace() does not update its internal spacing field.
    rx.setChannel((frequency - 8750) / 10);
    int rssi = rx.getRssi();
    bool stereo = rx.isStereo();
    rx.getStatus();
    bool afcValid = (rx.getShadownRegister(0x0A) & 0x1000) == 0;
    bool stationCandidate = afcValid && rssi >= SCAN_STATION_RSSI_THRESHOLD;

        char rssiText[16];
        snprintf(rssiText, sizeof(rssiText), "%d dBuV", rssi);
        scanFile.printf("| %7u.%02u | %10s | %-6s | %-9s | %-15s |\r\n",
            frequency / 100, frequency % 100, rssiText,
            stereo ? "YES" : "NO",
            afcValid ? "YES" : "NO",
            stationCandidate ? "YES" : "NO");

    if (stationCandidate)
      stationCandidates++;
    point++;

    if ((point % 20) == 0)
    {
      scanFile.flush();
      Serial.printf("Scan progress: %u/206 points\r\n", point);
    }
  }

  scanFile.println("+------------+------------+--------+-----------+-----------------+");
  scanFile.printf("Total points: %u | Station candidates: %u | RSSI threshold: %d dBuV\r\n",
                  point, stationCandidates, SCAN_STATION_RSSI_THRESHOLD);
  scanFile.close();
  rx.setChannel((originalFrequency - 8750) / 10);

  Serial.printf("Scan complete: %u points, %u candidate points. Saved: %s\r\n",
                point, stationCandidates, path);
}

void printScanFile(const char *path)
{
  File scanFile = SPIFFS.open(path, FILE_READ);
  if (!scanFile)
  {
    Serial.printf("Missing file: %s\r\n", path);
    return;
  }

  Serial.printf("\r\n--- %s ---\r\n", path);
  uint8_t buffer[128];
  while (scanFile.available())
  {
    size_t bytesRead = scanFile.read(buffer, sizeof(buffer));
    if (bytesRead > 0)
      Serial.write(buffer, bytesRead);
  }
  scanFile.close();
  Serial.println("\r\n--- end ---");
}

void printSavedScans()
{
  if (!fileSystemReady)
  {
    Serial.println("SPIFFS is not available.");
    return;
  }

  printScanFile("/fm_filter_on.txt");
  printScanFile("/fm_filter_off.txt");
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) ;

  pinMode(SEEK_UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SEEK_DOWN_BUTTON_PIN, INPUT_PULLUP);

  initializeRadioDisplay();

    Serial.println("Иницијализација на I2C и SI470X...");
    fileSystemReady = SPIFFS.begin(true);
    Serial.println(fileSystemReady ? "SPIFFS ready." : "SPIFFS mount failed.");
    Wire.setPins(ESP32_I2C_SDA, ESP32_I2C_SCL);
    
    rx.setup(RESET_PIN, ESP32_I2C_SDA);
    rx.setVolume(14);

    delay(500);

    rx.setRds(true);
    rx.setRdsMode(0); 
    rx.setSeekThreshold(18);
    rx.setSpace(1);
    rx.setBand(0);

    rx.getAllRegisters();
    uint16_t register06 = rx.getShadownRegister(0x06);
    rx.setShadownRegister(0x06, (register06 & 0xFF0F) | (3 << 4));
    rx.setAllRegisters();

    rx.setFrequency(10630); // Директно на твојата силна станица 92.9 MHz
    drawRadioDisplay(true);

    showHelp();
    showStatus();
}

void loop()
{
  pollSeekButton(seekUpButton, SI470X_SEEK_UP, "UP");
  pollSeekButton(seekDownButton, SI470X_SEEK_DOWN, "DOWN");

  // 1. Контрола преку Serial Monitor
  if (Serial.available() > 0)
  {
    char key = Serial.read();
    switch (key)
    {
    case '+': rx.setVolumeUp(); break;
    case '-': rx.setVolumeDown(); break;
    case 'U': case 'u': rx.setFrequencyUp(); resetStationName(); break;
    case 'D': case 'd': rx.setFrequencyDown(); resetStationName(); break;
    case 'S': Serial.println("Seeking UP..."); rx.seek(SI470X_SEEK_WRAP, SI470X_SEEK_UP); resetStationName(); break;
    case 's': Serial.println("Seeking DOWN..."); rx.seek(SI470X_SEEK_WRAP, SI470X_SEEK_DOWN); resetStationName(); break;
    case 'F': case 'f': scanBand("/fm_filter_on.txt", "ON"); break;
    case 'N': case 'n': scanBand("/fm_filter_off.txt", "OFF"); break;
    case 'T': case 't': setScanDateTime(); break;
    case 'L': case 'l': printSavedScans(); break;
    case '0': showStatus(); break;
    case '?': showHelp(); break;
    default: break;
    }
    delay(50);
    showStatus();
  } 

  if ((millis() - rds_elapsed) >= MAX_DELAY_RDS)
  {
    processRdsGroup();
    rds_elapsed = millis();
  }
  drawRadioDisplay();
  delay(5);
}
//завршено