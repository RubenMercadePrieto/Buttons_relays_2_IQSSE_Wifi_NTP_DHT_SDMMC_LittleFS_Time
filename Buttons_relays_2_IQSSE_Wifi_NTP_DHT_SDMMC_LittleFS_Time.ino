//-------------- Olimex ESP32-EVB ------------------------
//-------------- PROGMEM IQS School of Eng Logo ------------------------
#include "IQSSE.c"

//-------------- MOD-LCD2.8RTP Screen configuration ------------------------
#include "Board_Pinout.h"
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "Wire.h"
#include "Adafruit_STMPE610.h"
// https://github.com/OLIMEX/MOD-LCD2.8RTP/tree/master/SOFTWARE
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

//-------------- Wifi configuration ------------------------
// place in a separate file if data sensitive
#include <WiFi.h>
const char* ssid     = "ESP32_wifi";
const char* password = "HolaHolaHola";
//-------------- Time server ------------------------
// Must have wifi initially to access NTP server and read time from internet
#include "time.h"
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 0;
struct tm timeinfoStart;
struct tm timeinfoNow;

//-------------- Internal Time RTC ------------------------
// This ESP32Time library is used in order to keep time, after initial configuration with NTP,
// even if there is no wifi.
#include <ESP32Time.h>
ESP32Time rtc;
// https://vasanza.blogspot.com/2021/08/esp32-sincronizar-rtc-interno-con.html

// Function to get time from NTP and to asign it locally to RTC
void printLocalTime()
{
  if (!getLocalTime(&timeinfoStart)) {
    Serial.println("Failed to obtain time");
    return;
  }
  rtc.setTimeStruct(timeinfoStart);
  Serial.println(&timeinfoStart, "%D, %H:%M:%S");
  timeinfoNow = timeinfoStart;
}

//-------------- DHT11 ------------------------
// whatever sensor you can use
#include "DHTesp.h"
int dhtPin = 18;
DHTesp dht;
TempAndHumidity newDHTValues;

//-------------- Touch screen ------------------------
// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 290
#define TS_MINY 285
#define TS_MAXX 7520
#define TS_MAXY 7510
#define TS_I2C_ADDRESS 0x4d

Adafruit_STMPE610 ts = Adafruit_STMPE610();

//-------------- Relay pins ------------------------
#define RELAY1  32
#define RELAY2  33

//-------------- microSD Card - SDMMC & LittleFS ------------------------
// ESP32 memory must be formatted first with LittleFS
/*
   Connect the SD card to the following pins:

   SD Card | ESP32   4-bit SD bus
   https://www.instructables.com/Select-SD-Interface-for-ESP32/
   https://www.olimex.com/forum/index.php?topic=7086.0
      D2/RES?             12    Not in Olimex diagram
      D3                  13    Not in Olimex diagram
      CMD/DI              15     Ok
      VSS                 GND
      VDD                 3.3V
      CLK                 14     Ok
      VSS                 GND
      D0/DAT0/MISO?       2       Ok   (add 1K pull up after flashing)
      D1/RES?             4     Not in Olimex diagram
*/
#define FS_NO_GLOBALS //allow LittleFS to coexist with SD card, define BEFORE including FS.h
#include "FS.h"
#include "LITTLEFS.h"
#include "SD_MMC.h"
#define SPIFFS LITTLEFS
#include "SDMMC_func.h"   //auxiliary file with all necessary functions for files

//-------------- Global variables ------------------------
unsigned long TimePressed, NTPLastUpdate, TimeNow, TimeDHT;
int ButtonPressed = 0;
bool Relay1ON = false;
bool Relay2ON = false;



void setup() {
  // TODO: Power Up UEXT if 32U4

  delay(1000);
  Serial.begin (115200);

  //-------------- Wifi connection ------------------------
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  //-------------- Get local time ------------------------
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();

  //-------------- Initialize DHT sensor ------------------------
  dht.setup(dhtPin, DHTesp::DHT11);
  Serial.println("DHT initiated");
  if (dht.getStatus() != 0) {
    Serial.println("DHT11 error status: " + String(dht.getStatusString()));
  }

  Serial.print ("Demo started");


  //-------------- Initialize touch screen ------------------------
  Wire.begin();
  pinMode(TFT_DC, OUTPUT);
  // read diagnostics (optional but can help debug problems)
  //uint8_t x = tft.readcommand8(ILI9341_RDMODE);
  delay(1000);
  // Touch screen start
  ts.begin(TS_I2C_ADDRESS);

  //-------------- Define Relay pins as outputs ------------------------
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);


  TimePressed = millis();
  NTPLastUpdate = TimePressed;
  TimeDHT = TimePressed;

  //Get initial DHT data
  getTemperature();

  // ----------- Initialize SD MMC --------------------------
  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    return;
  }

  Serial.print("SD_MMC Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);

  // ----------- Example Functions for SD MMC --------------------------
  listDir(SD_MMC, "/", 0);
  //  createDir(SD_MMC, "/mydir");
  //  listDir(SD_MMC, "/", 0);
  //  removeDir(SD_MMC, "/mydir");
  //  listDir(SD_MMC, "/", 2);
  //  writeFile(SD_MMC, "/hello.txt", "Hello ");
  //  appendFile(SD_MMC, "/hello.txt", "World!\n");
  //  readFile(SD_MMC, "/hello.txt");
  //  deleteFile(SD_MMC, "/foo.txt");
  //  renameFile(SD_MMC, "/hello.txt", "/foo.txt");
  //  readFile(SD_MMC, "/foo.txt");
  //  testFileIO(SD_MMC, "/test.txt");
  Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));

  // ----------- Initialize LittleFS --------------------------
  Serial.println("Testing Little FS");
  if (!LITTLEFS.begin()) {
    Serial.println("LITTLEFS Mount Failed");
    return;
  }
  // ----------- Example Functions LittleFS --------------------------
  Serial.println("----list Dir LittleFS 1----");
  listDir(LITTLEFS, "/", 1);
  //  Serial.println("----remove old dir----");
  //  removeDir(LITTLEFS, "/mydir");
  //  Serial.println("----create a new dir----");
  //  createDir(LITTLEFS, "/mydir");
  //  Serial.println("----remove the new dir----");
  //  removeDir(LITTLEFS, "/mydir");
  //  Serial.println("----create the new again----");
  //  createDir(LITTLEFS, "/mydir");
  //  Serial.println("----create and work with file----");
  //  writeFile(LITTLEFS, "/mydir/hello.txt", "Hello ");
  //  appendFile(LITTLEFS, "/mydir/hello.txt", "World!\n");
  //  Serial.println("----list 2----");
  //  listDir(LITTLEFS, "/", 1);
  //  Serial.println("----attempt to remove dir w/ file----");
  //  removeDir(LITTLEFS, "/mydir");
  //  Serial.println("----remove dir after deleting file----");
  //  deleteFile(LITTLEFS, "/mydir/hello.txt");
  //  removeDir(LITTLEFS, "/mydir");
  //  Serial.println("----list 3----");
  //  listDir(LITTLEFS, "/", 1);

  // Test to copy from LittleFS a SDMMC
  //https://www.reddit.com/r/esp32/comments/b9l8kb/copying_file_sd_spiffs_arduino_ide/
  //  Serial.println("Copy from LittleFS to SDMMC");
  //  writeFile(LITTLEFS, "/ruben.txt", "Hello ");
  //  appendFile(LITTLEFS, "/ruben.txt", "World! Test!\n");
  //  fs::File sourceFile = LITTLEFS.open("/ruben.txt");
  //  fs::File destFile = SD_MMC.open("/rubenSD.txt", FILE_APPEND);
  //  static uint8_t buf[512];
  //  while ( sourceFile.read( buf, 512) ) {
  //    destFile.write( buf, 512 );
  //  }
  //  destFile.close();
  //  sourceFile.close();
  //  listDir(SD_MMC, "/", 2);
  //  readFile(SD_MMC, "/rubenSD.txt");

  // ----------- Delete old DHT data file --------------------------
  if (LITTLEFS.remove("/dataDHT.csv")) {
    Serial.println("Old dataDHT.csv file deleted");
  }
  // ----------- Create new DHT data file --------------------------
  fs::File datafile = LITTLEFS.open("/dataDHT.csv", FILE_APPEND);
  // save the date, time, temperature and humidity, comma separated
  datafile.println("AAPL_date,AAPL_time,AAPL_Temp,AAPL_Hum");
  datafile.close();
  readFile(LITTLEFS, "/dataDHT.csv");   //check that file was created succesfully
  SD_MMC.end(); //close SD properly, in order that no crashes later on
  Serial.println("Stoping SD MMC");
  Serial.println( "LittleFS & SD MMC Test complete" );

  // ----------- Initialize TFT screen --------------------------
  // Screen can only start after closing SDMMC, above
  tft.begin();
}


void loop(void) {
  TimeNow = millis();
  // Clear Screen
  tft.fillScreen(ILI9341_BLACK);
  //Print IQS School of Engineering Logo on top of the screen
  tft.drawRGBBitmap(0, 0, (uint16_t *)IQSSEBitmap, IQSSE_WIDTH, IQSSE_HEIGHT);
  // Write additional information over the logo, looks cool and dont waste space
  tft.setTextColor(ILI9341_BLACK);  tft.setTextSize(1);
  tft.setCursor(80, 00);
  tft.println("Prof. Ruben Mercade-Prieto");
  tft.setCursor(80, 10);
  tft.print("Wifi: "); tft.println(ssid);
  tft.setCursor(80, 20);
  tft.print("IP: "); tft.print(WiFi.localIP());
  tft.setCursor(80, 30);
  tft.print("Start: "); tft.print(&timeinfoStart, "%D, %H:%M:S");
  tft.setCursor(80, 40);
  tft.print("Now: "); tft.print(rtc.getTime("%D, %H:%M:%S"));

  if (TimeNow - TimeDHT > 10000) {
    getTemperature();
    TimeDHT = TimeNow;
  }
  // Printing DHT data on the screen.
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(2);
  tft.setCursor(10, 90); tft.print("Temp.: ");
  tft.setTextColor(ILI9341_YELLOW); tft.print(newDHTValues.temperature); tft.print("C");
  tft.setTextColor(ILI9341_WHITE); tft.setCursor(10, 110); tft.print("Hum. : ");
  tft.setTextColor(ILI9341_YELLOW); tft.print(newDHTValues.humidity); tft.print("%");

  // Printing the 4 buttons
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(2);
  tft.fillRoundRect(0, 200, 120, 50, 8, ILI9341_BLUE);
  tft.setCursor(10, 220);
  tft.println("Read File");
  tft.drawRoundRect(0, 200, 120, 50, 8, ILI9341_WHITE);

  tft.fillRoundRect(120, 200, 120, 50, 8, ILI9341_CYAN);
  tft.setCursor(130, 220);
  tft.println("Save SD");
  tft.drawRoundRect(120, 200, 120, 50, 8, ILI9341_WHITE);

  // The appearance for the Relay buttons depends if the relays are activated or not
  if (Relay1ON == false) {
    tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(2);
    tft.fillRoundRect(0, 250, 120, 50, 8, ILI9341_RED);
    tft.setCursor(10, 270);
    tft.println("Relay 1");
    tft.drawRoundRect(0, 250, 120, 50, 8, ILI9341_WHITE);
  }
  else if (Relay1ON == true) {
    tft.setTextColor(ILI9341_RED);  tft.setTextSize(2);
    //  tft.fillRoundRect(20, 250, 100, 50, 8, ILI9341_RED);
    tft.setCursor(10, 270);
    tft.println("Relay 1");
    tft.drawRoundRect(0, 250, 120, 50, 8, ILI9341_RED);
  }

  if (Relay2ON == false) {
    tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(2);
    tft.fillRoundRect(120, 250, 120, 50, 8, ILI9341_GREEN);
    tft.setCursor(130, 270);
    tft.println("Relay 2");
    tft.drawRoundRect(120, 250, 120, 50, 8, ILI9341_WHITE);
  }
  else if (Relay2ON == true) {
    tft.setTextColor(ILI9341_GREEN);  tft.setTextSize(2);
    //  tft.fillRoundRect(120, 250, 100, 50, 8, ILI9341_GREEN);
    tft.setCursor(130, 270);
    tft.println("Relay 2");
    tft.drawRoundRect(120, 250, 120, 50, 8, ILI9341_GREEN);
  }

  ButtonPressed = Get_Button();
  // Program will only continue to this point when the screen is touched. If not touched, it will stay within the function Get_button()
  // Define what to do if any of the 4 buttons is pressed
  if (ButtonPressed == 2)  {
    tft.fillScreen(ILI9341_GREEN);
    tft.setCursor(10, 100);
    tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(3);
    if (Relay2ON == false) {
      tft.println("Relay 2 ON!");
      Relay2ON = true;
      digitalWrite(RELAY2, HIGH);
      delay(500);
    }
    else {
      digitalWrite(RELAY2, LOW);
      Relay2ON = false;
      tft.println("Relay 2 OFF!");
      delay(500);
    }
  }
  else if (ButtonPressed == 1) {
    tft.fillScreen(ILI9341_RED);
    tft.setCursor(10, 100);
    tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(3);
    if (Relay1ON == false) {
      tft.println("Relay 1 ON!");
      Relay1ON = true;
      digitalWrite(RELAY1, HIGH);
      delay(500);
    }
    else {
      digitalWrite(RELAY1, LOW);
      Relay1ON = false;
      tft.println("Relay 1 OFF!");
      delay(500);
    }
  }
  else if (ButtonPressed == 4) {
    // reads the LittleFS file with the DHT data, displayed in serial and in the screen
    readFile(LITTLEFS, "/dataDHT.csv");
    readFileTFTScreen(LITTLEFS, "/dataDHT.csv");
    delay(5000);
  }
  else if (ButtonPressed == 3) {
    tft.fillScreen(ILI9341_CYAN);
    tft.setCursor(0, 100);
    tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(2);
    tft.println("Saving Data to SD!");
    SaveSDcard();
    delay(1000);
    // restart tft screen after saving data with SDMMC
    tft.begin();
  }


  delay(100); // leave some time for other stuff?
}


// Function to control the touch screen and the buttons
int Get_Button(void) {
  int result = 0;   //value to be returned by the function

  TS_Point p;
  while (1) { //important while. meanwhile screen not touched, it will loop here indefinetelly
    delay(50);

    p = ts.getPoint();    //get data from the touch screen
    TimeNow = millis();
    // Get, store in memory, and print in screen new DHT data every 10 s, even if no touching
    if (TimeNow - TimeDHT > 10000) {
      getTemperature();
      TimeDHT = TimeNow;
      tft.fillRect(0, 90, 240, 40, ILI9341_BLACK); //deleate previous DHT data from screen
      tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(2);
      tft.setCursor(10, 90); tft.print("Temp.: "); //Print new DHT data to screen
      tft.setTextColor(ILI9341_YELLOW); tft.print(newDHTValues.temperature); tft.print("C");
      tft.setTextColor(ILI9341_WHITE); tft.setCursor(10, 110); tft.print("Hum. : ");
      tft.setTextColor(ILI9341_YELLOW); tft.print(newDHTValues.humidity); tft.print("%");
    }

    if (TimeNow > (TimePressed + 3000)) { //provide a time delay to check next touch, otherwise fake double activated button
      if (p.z < 10 || p.z > 140) {     // =0 should be sufficient for no touch. sometimes it give a signal p.z 255 when not touching
        //    Serial.println("No touch");
      }
      else if (p.z != 129) {    //for a first touch, p.z = 128. A value of 129 is for continuous touching
        // scale the touch screen coordinates for the TFT screen resolution
        p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
        p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
        p.y = 320 - p.y;

        tft.fillCircle(p.x, p.y, 5, ILI9341_YELLOW);  //show in the screen where you touched

        // If list to identify the location of all the buttons
        if ((p.y > 250) && (p.y < 300) && (p.x > 20) && (p.x < 120)) {
          result = 1;
        }
        else if ((p.y > 250) && (p.y < 300) && (p.x > 120) && (p.x < 220)) {
          result = 2;
        }
        else if ((p.y > 200) && (p.y < 250) && (p.x > 120) && (p.x < 220)) {
          result = 3;
        }
        else if ((p.y > 200) && (p.y < 250) && (p.x > 20) && (p.x < 120)) {
          result = 4;
        }
        else {
          result = 0;
        }
        if (result != 0) {
          TimePressed = millis();
          //          Serial.println(TimePressed);
        }
        Serial.print("X = "); Serial.print(p.x);
        Serial.print("\tY = "); Serial.print(p.y);
        Serial.print("\tPressure = "); Serial.print(p.z);
        Serial.print("\tButton = "); Serial.println(result);
        return result;  //return which button has been pressed

      }
    }
  }
}

// function to get data from the sensor, print it in the Serial console and save in LittleFS file
void getTemperature(void) {
  newDHTValues = dht.getTempAndHumidity();
  Serial.println(" T:" + String(newDHTValues.temperature) + " H:" + String(newDHTValues.humidity));
  fs::File datafile = LITTLEFS.open("/dataDHT.csv", FILE_APPEND);
  if (datafile == true) { // if the file is available, write to it
    TempAndHumidity newValues = dht.getTempAndHumidity();
    // save new DHT data in the local file
    datafile.print(rtc.getTime("%D")); datafile.print(',');
    datafile.print(rtc.getTime("%H:%M:%S")); datafile.print(',');
    datafile.print(String(newValues.temperature)); datafile.print(',');
    datafile.println(String(newValues.humidity)); datafile.close();
    datafile.close();

  }
  else {
    Serial.println("Cannot open LittleFS dataDHT.csv file");
  }
}


// Function to copy data from LittleFS file to microSD as backup
String filename;
char *pathChar;

void SaveSDcard(void) {
  //As SDMMC cannot work together with the TFT screen, it must be started and stopped each time called.
  Serial.println("Starting SD MMC");
  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    return;
  }
  delay(1000);

  // create a filename using the RTC time - new file every minute

  String filename = "/" + rtc.getTime("%Y%m%d_%H%M") + ".csv";
  // need to convert string to char
  if (filename.length() != 0) {
    pathChar = const_cast<char*>(filename.c_str());
  }
  //  Serial.println(pathChar);
  //  Serial.println(filename);
  //  appendFile(SD_MMC, pathChar, "Testing...");
  //  readFile(SD_MMC, pathChar);


  // Copy from LittleFS to SDMMC - improve?
  //https://www.reddit.com/r/esp32/comments/b9l8kb/copying_file_sd_spiffs_arduino_ide/
  Serial.println("Copy DHT data from LittleFS to SDMMC");

  fs::File sourceFile = LITTLEFS.open("/dataDHT.csv");
  //rewrite (instead of append) to avoid problems?
  fs::File destFile = SD_MMC.open(pathChar, FILE_WRITE);
  if (!destFile) {
    Serial.println("Cannot open data SD file");
    return;
  }
  // is this the best way to copy between LittleFS and SD? often some errors, e.g. bad lines
  static uint8_t buf[512];
  while ( sourceFile.read( buf, 512) ) {
    destFile.write( buf, 512 );
  }
  delay(100);
  destFile.close();
  sourceFile.close();
  readFile(SD_MMC, pathChar); //display the copy file, can see any mistakes
  listDir(SD_MMC, "/", 2);
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));
  //Must close SDMMC to use TFT screen later on
  SD_MMC.end();
  Serial.println("Stoping SD MMC");
}
