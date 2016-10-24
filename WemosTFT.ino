/***************************************************
 ****************************************************/
//#define DISP_1_44 // The ST7735 1.44" display
#define DISP_2_2  // The ILI9340C 2.2" display

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#ifdef DISP_1_44
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library
#elif defined(DISP_2_2)
#include <TFT_ILI93XX.h>
#endif
#include <SPI.h>

#define DEBUG_TEST

const char* ssid = AN_SSID;
const char* password = A_PASSWORD;
const char* mqtt_server = A_SERVER;

// For the breakout, you can use any 2 or 3 pins
// These pins will also work for the 1.8" TFT shield
#define TFT_CS     D4
#define TFT_DC     D2
#define TFT_RST    0  // you can also connect this to the Arduino reset
                      // in which case, set this #define pin to 0!

#ifdef DISP_1_44
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);
#define NR_TEMP 3 // Room for 3 temps
#define NR_LBLS NR_TEMP
#endif
#ifdef DISP_2_2
#include "_fonts/Terminal_9.c"
#include "_fonts/akashi20.c"
TFT_ILI93XX tft = TFT_ILI93XX(TFT_CS, TFT_DC);
#define setTextSize(n)  setTextScale(n)
#define NR_TEMP 4 // Room for 4 temps
#define NR_LBLS 7
#endif

WiFiClient espClient;
PubSubClient client(espClient);

#define THIST 80
#define TDYN  30
#define NO_TEMP 0x7E

typedef struct {
  float   curr;
  float   min;
  float   max;
  int8_t  hist[THIST];
} TEMP_T;

TEMP_T temp[NR_TEMP];
uint16_t ltime = 0;
uint16_t sec = 0;
int pro=0;

typedef struct {
  char *txt;
  int color;
} LBL_T;

LBL_T lbl[NR_LBLS] = {
  { "Annexet", YELLOW }
  ,{ "Inne", YELLOW }
  ,{ "Ute", YELLOW }
#ifdef DISP_2_2
  ,{"Uterummet",YELLOW}
  ,{"Dammpumpen",GREEN}
  ,{"Garaget",GREEN}
  ,{"Meddelande",RED}
#endif
};

void setup(void) {
  Serial.begin(9600);
#ifdef DISP_2_2
  uint8_t errorCode = 0;
  tft.begin(false);
  delay(30);
  //the following it's mainly for Teensy
  //it will help you to understand if you have choosed the
  //wrong combination of pins!
  errorCode = tft.getErrorCode();
  if (errorCode != 0) {
    Serial.print("Init error! ");
    if (bitRead(errorCode, 0)) Serial.print("MOSI or SCLK pin mismach!\n");
    if (bitRead(errorCode, 1)) Serial.print("CS or DC pin mismach!\n");
  }
  else {
    Serial.println("Hello! ILI9340C 2.2\"");
    tft.setTextColor(YELLOW);
    tft.setTextScale(2);
  }
#endif

#ifdef DISP_1_44
  Serial.println("Hello! ST7735 TFT 1.44\"");
  // Use this initializer (uncomment) if you're using a 1.44" TFT
  tft.initR(INITR_144GREENTAB);   // initialize a ST7735S chip, black tab
#endif

  Serial.println("Initialized");

  tft.setRotation(0);
  
  uint16_t time = millis();
  tft.fillScreen(BLACK);
  time = millis() - time;

  Serial.println(time, DEC);
  delay(500);

  tft.fillScreen(BLACK);
  tft.setCursor(10, 60);
  tft.setTextColor(YELLOW);
  tft.setTextSize(1);
#ifdef DEBUG_TEST
  tft.println("Debug mode!");
  delay(2000);
#else
  tft.println("Searching WiFi!");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    tft.fillScreen(BLACK);
    tft.setCursor(10, 60);
    tft.setTextColor(RED);
    tft.println("No WiFi - reboot!");
#ifdef RX_DEBUG
    Serial.println("Connection Failed! Rebooting...");
#endif
    delay(5000);
    ESP.restart();
  }
#endif//DEBUG_TEST

  ArduinoOTA.setHostname("tempDisplay");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  // Clear screen
  tft.fillScreen(BLACK);
  int xpos = 5;
  int ypos = 7;

  // Draw text labels
//  tft.setTextColor(YELLOW);
//  tft.setTextSize(1);
  for(int i=0; i<NR_LBLS; i++) {
    drawLbl(i, lbl[i].txt, lbl[i].color);
//    tft.setTextColor(lbl[i].color);
//    prtString(xpos, ypos+i*40, 1, lbl[i].txt);
  }
  for(int i=0; i<NR_TEMP; i++) {
    temp[i].curr = 0;
    temp[i].min = 100;
    temp[i].max = -100;
    memset(temp[i].hist,NO_TEMP,THIST);
  }

#ifndef DEBUG_TEST
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  // Once connected, publish an announcement...
  client.publish("tempdisp", "ready");
#endif

#ifdef DEBUG_TEST
  printMsg(0,"On", GREEN);
  printMsg(1,"Open", GREEN);
  printMsg(2,"Timeout from NodeIT", RED);
#endif

  ltime = millis();
}

void drawLbl(int n, char *lbl, int color)
{
  tft.setTextSize(1);
  tft.setTextColor(color);
  prtString(5, 7+n*40, 1, lbl);
}

void callback(char* topic, byte* payload, unsigned int length) {
  char msg[32+1];

  strncpy(msg,(char*)payload,32);
  
#ifdef RX_DEBUG
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(msg);
//  for (int i = 0; i < length; i++) {
//    Serial.print((char)payload[i]);
//  }
//  Serial.println();
#endif

  //mossa/error1
  if( strcmp(topic, "mossa/error1") == 0) {
    printMsg(2,(char*)payload,RED);
    return;
  }
  
  //nodeit/status/temp1
  //0123456789012345678
  int n=-1;
  switch((char)topic[18]) {
    case '1': // Temp 1
      n=0; break; // Annextet
    case '2': // Temp 2
      n=2; break; //Ute
    case '3': // Temp 3
      n=1; break; // Inne
    default:
      return;
  }
  
  String sTemp = String((char*)payload);
  float f = sTemp.toFloat();
  printTemp(n,f);
  pro = 0;
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
#ifdef RX_DEBUG
    Serial.print("Attempting another MQTT connection...");
#endif
    // Attempt to connect
    if (client.connect("aESP8266Client")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("tempdisp", "ready");
      // ... and resubscribe
      client.subscribe("nodeit/status/#");
      client.subscribe("mossa/#");
    } else {
#ifdef RX_DEBUG
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
#endif
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void printMsg(int n, char *msg, int color)
{
  int16_t xpos = 5;
  int16_t ypos = 20 + 40*(NR_TEMP+n);
  // Clear text area
  tft.fillRect(0, ypos-5, tft.width(), 30, BLACK);
  tft.setTextColor(color);
  tft.setFont(&akashi20);
  prtString(xpos, ypos, 1, msg);
  tft.setFont(&defaultFont);
}

void printTemp(int n, float tp)
{
#ifdef DISP_1_44
  int16_t xpos = -5;
#endif
#ifdef DISP_2_2
  int16_t xpos = 5;
#endif
  int16_t ypos = 20 + 40*n;
  char buf[20];
  TEMP_T *pt = &temp[n];

  if( pt->min > tp )
    pt->min = tp;
  if( pt->max < tp )
    pt->max = tp;
  pt->curr = tp;
    
  unsigned int color = rainbow(int(tp+32));

  // Clear text area
  tft.fillRect(0, ypos-5, tft.width(), 30, BLACK);

  tft.setTextColor(color);
  prtFloat(xpos, ypos, 3,tp);
  
#ifdef DISP_1_44
  xpos = tft.getCursorX();
#endif
#ifdef DISP_2_2
  int16_t oy;
  tft.getCursor(xpos,oy);
  xpos += 4;
#endif

  prtString(xpos, ypos-3, 1, "o");
#ifdef DISP_2_2
  prtString(xpos+13, ypos, 3, "C");
#endif

  tft.setTextColor(WHITE);
#ifdef DISP_1_44
  xpos = 97;
#endif
#ifdef DISP_2_2
  xpos = 135;
#endif
  prtFloat(xpos, ypos+1,  1,pt->min);
  prtFloat(xpos, ypos+14, 1,pt->max);

  int i=0;
  int8_t  c;
  for(;i<THIST;i++) {
    int cl = GREEN;
    c = pt->hist[i] = (i == (THIST-1)) ? int8_t(tp) : pt->hist[i+1];
    if( c != NO_TEMP ) {
      if(c > 20)  { c = 20; cl = RED; }
      if(c < -9) { c = -9; cl = WHITE; }
      tft.drawPixel(xpos+20+i, ypos-5+20-c, cl);
    }
  }
}

void prtString(int x, int y, int s, char *p)
{
  tft.setCursor(x, y, REL_X);
  tft.setTextSize(s);
  tft.print(p);
}

void prtFloat(int x, int y, int s, float f)
{
  char buf[20];
  int i = 0;
  if( f > -10.0 ) {
    i = (f<0 || f>=10) ? 1 : 2;
  }
  buf[0] = buf[1] = ' ';
  dtostrf(f,3,1,&buf[i]);
  tft.setFont(&Terminal_9);
  prtString(x,y,s,buf);
  tft.setFont(&defaultFont);
}

int n=0;

void loop() {
  ArduinoOTA.handle();

#ifndef DEBUG_TEST
  if (!client.connected()) {
    reconnect();
  }

  if (client.connected()) {
    client.loop();
  }
#endif

#define INTERVAL 360  // 6 minutes
#define PROG_H        5

  uint16_t time = millis() - ltime;
  if( time > 1000 ) {
    sec++;
    ltime = millis();
  }
  if( sec == (INTERVAL/tft.width()) ) {
    if( pro == 0 )
      tft.fillRect(0, tft.height()-PROG_H, tft.width(), PROG_H, BLACK);
    if( pro < tft.width() ) {
      tft.fillRect(0, tft.height()-PROG_H, pro, PROG_H, GREEN);
      pro++;
    } else {
      tft.fillRect(0, tft.height()-PROG_H, tft.width(), PROG_H, RED);
    }
    sec = 0;
#ifdef DEBUG_TEST
    {
      float t = (random(600)-300)/10.0;
      printTemp(random(NR_TEMP),t);
      if( pro >= tft.width() )
        pro++;
      if( pro > (tft.width()+tft.width()/10) )
        pro = 0;
    }
#endif
  }
}
  
unsigned int rainbow(byte value)
{
  // Value is expected to be in range 0-127
  // The value is converted to a spectrum colour from 0 = blue through to red = blue

  byte red = 0; // Red is the top 5 bits of a 16 bit colour value
  byte green = 0;// Green is the middle 6 bits
  byte blue = 0; // Blue is the bottom 5 bits

  byte quadrant = value/32;

  if (quadrant == 0) {
    blue = (value%32); //31;
    green = 0; //2*(value%32);
    red = 31; //0;
  }
  if (quadrant == 1) {
    blue = 0; //31 - (value%32);
    green = 63;
    red = 31-(value%32); //0;
  }
  if (quadrant == 2) {
    blue = 0;
    green = 63;
    red = value%32;
  }
  if (quadrant == 3) {
    blue = 0;
    green = 63-2*(value%32);
    red = 31;
  }
  return (red<<11) + (green<<5) + blue;
}

