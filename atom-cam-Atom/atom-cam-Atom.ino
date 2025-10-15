//#include <M5AtomS3.h>
#include <M5Unified.h>
#include <Arduino.h>
#include "camera_pins.h"
#include <esp_camera.h>
#include <SPI.h>
#include "FS.h"
#include "SD.h"
#include <Wire.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include "WiFi.h"
#include "..\wifi_setting\M5_wifi.h"

//#define LED_PIN 2
#define POWER_GPIO_NUM 18

// 切り出す画像のサイズ
const int crop_width = 128;
const int crop_height = 128;

const int icon_width = 64;
const int icon_height = 64;

camera_fb_t* fb;
uint8_t crop_data[crop_width][crop_height];

M5Canvas canvas0;
M5Canvas canvas1;

const char *ssid = M5_DEV_SSID;
const char *pass = M5_PASSWORD;
#define USEPORT 8888
int status = WL_IDLE_STATUS;
WiFiServer server(USEPORT); // 80番ポート(http)
WiFiClient client;

// Atomic TFCard Base のピン配置に合わせて定義
//#define SCK_PIN   5
//#define MISO_PIN  7
//#define MOSI_PIN  6
//#define CS_PIN    8
#define SCK_PIN   7
#define MISO_PIN  8
#define MOSI_PIN  6
#define CS_PIN    39

std::vector<std::vector<uint32_t>> vcolor_palette;

#define MENU_MAX 4
#define PALLETE_MAX 10

int iMenuIconCnt = 0;
int iPaletteCnt = 0;

uint32_t keyOnTime = 0;             // キースイッチを操作した時間
int filecounter = 1;

#define ATOM_ADDR 0x4B
#define STX 0x02
#define ETX 0x03

// I2Cマスターからの受信
enum CommandType {
  CMD_NONE = 0,
  CMD_GET_ATOMINFO,
  CMD_GET_PALETTE,
  CMD_GET_ICON,
  CMD_SHOT,
  CMD_GET_PHOTO,
  CMD_GET_PHOTO_LIST,
};
volatile uint8_t cmdType = CMD_NONE;
volatile uint8_t paletteIdx = 0;
volatile uint8_t iconIdx = 0;
String  photoName = "";

enum SlaveState {
  STATE_IDLE,
  STATE_PROCESSING,
  STATE_READY_TO_SEND
};

volatile uint8_t dataState = STATE_IDLE;
String i2CSendData = "";
camera_config_t camera_config = {
  .pin_pwdn     = PWDN_GPIO_NUM,
  .pin_reset    = RESET_GPIO_NUM,
  .pin_xclk     = XCLK_GPIO_NUM,
  .pin_sscb_sda = SIOD_GPIO_NUM,
  .pin_sscb_scl = SIOC_GPIO_NUM,
  .pin_d7       = Y9_GPIO_NUM,
  .pin_d6       = Y8_GPIO_NUM,
  .pin_d5       = Y7_GPIO_NUM,
  .pin_d4       = Y6_GPIO_NUM,
  .pin_d3       = Y5_GPIO_NUM,
  .pin_d2       = Y4_GPIO_NUM,
  .pin_d1       = Y3_GPIO_NUM,
  .pin_d0       = Y2_GPIO_NUM,

  .pin_vsync = VSYNC_GPIO_NUM,
  .pin_href  = HREF_GPIO_NUM,
  .pin_pclk  = PCLK_GPIO_NUM,

  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,

  .pixel_format = PIXFORMAT_RGB565,
  .frame_size = FRAMESIZE_QCIF,
  // FRAMESIZE_96X96,    // 96x96
  // FRAMESIZE_QQVGA,    // 160x120
  // FRAMESIZE_128X128,  // 128x128
  // FRAMESIZE_QCIF,     // 176x144
  // FRAMESIZE_HQVGA,    // 240x176
  // FRAMESIZE_240X240,  // 240x240
  // FRAMESIZE_QVGA,     // 320x240

  .jpeg_quality = 0,
  .fb_count = 2,
  .fb_location = CAMERA_FB_IN_PSRAM,
  .grab_mode = CAMERA_GRAB_LATEST,
  .sccb_i2c_port =1,
};

void onReceived( int len ) {
  bool inPacket = false;
  String i2cRecieve = "";
  while( Wire.available() ){
    char c = (char)Wire.read();
    if (inPacket) {
      if (c == ETX) {
        // 受信完了
        inPacket = false;
        break;
      } else {
        i2cRecieve += c;
      }
    } else if( c == STX ) {
      // 受信開始
      inPacket = true;
    }
  }

  // データ処理中はコマンド受信しても無視する
  if( dataState == STATE_PROCESSING ) {
    return;
  }

  if(i2cRecieve.compareTo("doShot") == 0 ){
    cmdType = CMD_SHOT;
    dataState = STATE_PROCESSING;
  
  } else if(i2cRecieve.indexOf("GetPalette") != -1) {
    // カラーパレット取得
    String strIdx = i2cRecieve;
    strIdx.replace("GetPalette", "");
    strIdx.trim();
    int iIdx = strIdx.toInt();
    if( iIdx > iPaletteCnt ) iIdx = 0;
    cmdType = CMD_GET_PALETTE;
    paletteIdx = iIdx;
    dataState = STATE_PROCESSING;
  } else if(i2cRecieve.indexOf("GetIcon") != -1) {
    // カラーパレット取得
    String strIdx = i2cRecieve;
    strIdx.replace("GetIcon", "");
    strIdx.trim();
    int iIdx = strIdx.toInt();
    if( iIdx > iMenuIconCnt ) iIdx = 0;
    cmdType = CMD_GET_ICON;
    iconIdx = iIdx;
    dataState = STATE_PROCESSING;
  } else if(i2cRecieve.compareTo("GetAtomInfo") == 0) {
    // AtomS3Rの情報取得
    cmdType = CMD_GET_ATOMINFO;
    dataState = STATE_PROCESSING;
  } else {
    cmdType = CMD_NONE;
  }
}

void onRequest() {
  auto sendI2C = [](String s) {
    Wire.write(STX);
    Wire.print(s);
    Wire.write(ETX);
  };
  if(dataState == STATE_READY_TO_SEND) {
    String sendData = i2CSendData;
    sendI2C(sendData);
    cmdType = CMD_NONE;
    dataState = STATE_IDLE;
    i2CSendData = "";
  } else {
    if(cmdType == CMD_NONE) {
      sendI2C("NoCommand");
    } else {
      sendI2C("busy");
    } 
  }
}

uint32_t rgb565_to_rgb888(uint16_t color) {
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;
  r = (r * 255 + 15) / 31;
  g = (g * 255 + 31) / 63;
  b = (b * 255 + 15) / 31;
  return (r << 16) | (g << 8) | b;
}

// パレット内で最も近い色を探す
std::pair<uint32_t, uint8_t> findClosestColor(uint32_t originalColor) {
  uint8_t r1 = (originalColor >> 16) & 0xff;
  uint8_t g1 = (originalColor >> 8) & 0xff;
  uint8_t b1 = originalColor & 0xFF;

  uint32_t closestColor = vcolor_palette[paletteIdx][0];
  uint8_t closestIndex = 0;
  long minDistance = -1;

  for (int i = 0; i < vcolor_palette[paletteIdx].size(); i++) {
    uint8_t r2 = (vcolor_palette[paletteIdx][i] >> 16) & 0xff;
    uint8_t g2 = (vcolor_palette[paletteIdx][i] >> 8) & 0xff;
    uint8_t b2 = vcolor_palette[paletteIdx][i] & 0xff;
    
    // RGB空間でのユークリッド距離の2乗を計算
    long distance = pow(r1 - r2, 2) + pow(g1 - g2, 2) + pow(b1 - b2, 2);

    if (minDistance==-1 || distance < minDistance) {
      minDistance = distance;
      closestColor = vcolor_palette[paletteIdx][i];
      closestIndex = i;
    }
  }
  return std::make_pair(closestColor, closestIndex);
}

std::vector<uint8_t> originalToPalette() {
  std::vector<uint8_t> v;
  const int bayer4x4[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 }
  };
  // 元画像はRGB565 (1ピクセル2バイト)
  uint16_t *src_pixels = (uint16_t*)fb->buf;

  int src_width = fb->width;
  int src_height = fb->height;

  /* --- 中央クロップと減色処理 --- */
  /* 中央を切り出すための開始座標を計算 */
  int crop_start_x = (src_width - crop_width) / 2;
  int crop_start_y = (src_height - crop_height) / 2;

  memset(crop_data, 0, sizeof(uint8_t) * 128 * 128);
  canvas0.pushImage(0, 0, src_width, src_height, (uint16_t*)src_pixels);
  for (int y = 0; y < crop_height; y++) {
    for (int x = 0; x < crop_width; x++) {
      // 元画像から対応するピクセル座標を計算
      int src_x = crop_start_x + x;
      int src_y = crop_start_y + y;
      
      // 元画像のピクセル色(RGB565)を取得
      uint16_t src_color_565 = src_pixels[src_y * src_width + src_x];
      
      // RGB888に変換して、パレットの最も近い色を探す
      // uint32_t src_color_888 = rgb565_to_rgb888(src_color_565);
      uint32_t src_color_888 = canvas0.color16to24(src_color_565);
      int r,g,b;
      r = (src_color_888 >> 16) & 0xff;
      g = (src_color_888 >> 8) & 0xff;
      b = (src_color_888) & 0xff;

      int threshold = bayer4x4[y%4][x%4];
      r = constrain(r+threshold, 0, 255);
      g = constrain(g+threshold, 0, 255);
      b = constrain(b+threshold, 0, 255);
      uint32_t dst_color_888 = (r << 16) | (g << 8) | b;

      std::pair<uint32_t, uint8_t> pair = findClosestColor(dst_color_888);
      crop_data[y][x] = pair.second;
    }
  }
  canvas0.clearDisplay(BLACK);
  // カメラUSBポート下側の画像が撮れるので、90度回転させる
  for (int y = 0; y < crop_height; y++) {
    for (int x = 0; x < crop_width; x++) {
      v.push_back(crop_data[x][crop_height -1 -y]);
    }
  }
  return v;
}

std::vector<uint8_t> IconToPalette() {
  std::vector<uint8_t> v;
  char png_path[64];
  memset(png_path, 0, 64);
  sprintf(png_path, "/ICON/%d.png", iconIdx);
#if 1
  File pngFile = SD.open(png_path, "r"); // まずファイルを開く
  if (!pngFile) {
    Serial.printf("Error: Failed to open %s for reading.\n", png_path);
    return v;
  }
  size_t png_data_size = pngFile.size();
  uint8_t* png_data_buffer = (uint8_t*)heap_caps_malloc(png_data_size, MALLOC_CAP_SPIRAM);
  
  if (!png_data_buffer) {
    Serial.println("Failed to allocate buffer for PNG data");
    pngFile.close();
    return v;
  }

  // 3. ファイルの内容を全てバッファに読み込む
  pngFile.read(png_data_buffer, png_data_size);
  pngFile.close(); // ファイルはすぐに閉じる

  uint16_t* pixel_buffer = nullptr;
  pixel_buffer = (uint16_t*)heap_caps_malloc(icon_width * icon_height * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!pixel_buffer) {
    Serial.println("Failed to allocate pixel buffer");
    heap_caps_free(png_data_buffer);
    pngFile.close();
    return v;
  }

  canvas1.createSprite(icon_width, icon_height); // png読み込み用スプライト
  canvas1.drawPng(png_data_buffer, png_data_size, 0, 0);
  heap_caps_free(png_data_buffer);
#else
  uint16_t* pixel_buffer = nullptr;
  pixel_buffer = (uint16_t*)heap_caps_malloc(icon_width * icon_height * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!pixel_buffer) {
    Serial.println("Failed to allocate pixel buffer");
    return v;
  }

  canvas1.createSprite(icon_width, icon_height); // png読み込み用スプライト
  canvas1.drawBmpFile(SD, png_path, 0, 0);
#endif
  canvas1.readRect(0, 0, icon_width, icon_height, pixel_buffer); // ピクセルデータに変換

  for (int y = 0; y < icon_height; y++) {
    for (int x = 0; x < icon_width; x++) {
      // 元画像から対応するピクセル座標を計算
      // 元画像のピクセル色(RGB565)を取得
      uint16_t src_color_565 = pixel_buffer[y * icon_width + x];
      
      // RGB888に変換して、パレットの最も近い色を探す
      //uint32_t src_color_888 = rgb565_to_rgb888(src_color_565);
      uint32_t src_color_888 = canvas1.color16to24(src_color_565);

      std::pair<uint32_t, uint8_t> pair = findClosestColor(src_color_888);
      v.push_back(pair.second);
    }
  }
  canvas1.deleteSprite();  // png読み込み用スプライト解放
  heap_caps_free(pixel_buffer);
  return v;
}

void sendImage(std::vector<uint8_t> v) {
  client = server.available();
  if (!client) {
    Serial.println("client not found");
    return;
  }

  DynamicJsonDocument doc(v.size()); // 念のため少し余裕を持たせる
  doc["Type"] = "ICON";
  JsonArray dataArray = doc.createNestedArray("data");
  uint32_t compData=0x0;
  for(int i = 0 ; i < v.size() ; i++) {
      uint8_t shift = i % 8;
      compData |= v[i] << (shift * 4);
      if(shift == 7) {
        dataArray.add(compData);
        compData = 0x0;
      }
  }

  String jsonString;
  jsonString.reserve(v.size()); 
  int jsonLen = serializeJson(doc, jsonString);
  Serial.printf("Sending JSON (%dbytes)...\n", jsonLen);
  client.println(jsonString);

//  client.stop();
//  Serial.println("Client disconnected");
}

bool CameraBegin() {
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    return false;
  }

  //カメラ追加設定
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 0);    //上下反転 0無効 1有効
  s->set_hmirror(s, 0);  //左右反転 0無効 1有効
                         // s->set_colorbar(s, 1); //カラーバー 0無効 1有効
                         // s->set_brightness(s, 1);  // up the brightness just a bit
                         // s->set_saturation(s, 0);  // lower the saturation
  return true;
}

bool CameraGet() {
  fb = esp_camera_fb_get();
  if (!fb) {
    return false;
  }
  return true;
}

bool CameraFree() {
  if (fb) {
    esp_camera_fb_return(fb);
    return true;
  }
  return false;
}

void saveToSD_OriginalBMP() {
  char filename[64];
  memset(filename, 0, 64);
  sprintf(filename, "/Original/%010d_%04d_Original.bmp", keyOnTime, filecounter);
  if (!SD.exists("/Original/")) {
    SD.mkdir("/Original/");
  }
  File file = SD.open(filename, "w");
  if (file) {
    uint8_t* out_bmp = NULL;
    size_t out_bmp_len = 0;
    frame2bmp(fb, &out_bmp, &out_bmp_len);
    file.write(out_bmp, out_bmp_len);
    file.close();
    free(out_bmp);
  } else {
    Serial.printf("Failed to save %s\n", filename);
  }
}

void saveToSD_ConvertBMP(std::vector<uint8_t> v) {
  char filename[64];
  memset(filename, 0, 64);
  sprintf(filename, "/Palette/%010d_%04d_palette%01d.bmp", keyOnTime, filecounter, paletteIdx);
  if (!SD.exists("/Palette/")) {
    SD.mkdir("/Palette/");
  }
  File file = SD.open(filename, "w");
  if (file) {
    int width = crop_width;
    int height = crop_height;
    int rowSize = (3 * width + 3) & ~3;

    lgfx::bitmap_header_t bmpheader;
    bmpheader.bfType = 0x4D42;
    bmpheader.bfSize = rowSize * height + sizeof(bmpheader);
    bmpheader.bfOffBits = sizeof(bmpheader);
    bmpheader.biSize = 40;
    bmpheader.biWidth = width;
    bmpheader.biHeight = height;
    bmpheader.biPlanes = 1;
    bmpheader.biBitCount = 24;
    bmpheader.biCompression = 0;
    bmpheader.biSizeImage = 0;  //以下、MacOS向けに追加
    bmpheader.biXPelsPerMeter = 2835;
    bmpheader.biYPelsPerMeter = 2835;
    bmpheader.biClrUsed = 0;
    bmpheader.biClrImportant = 0;

    file.write((std::uint8_t*)&bmpheader, sizeof(bmpheader));
    std::uint8_t buffer[rowSize];
    memset(&buffer[rowSize - 4], 0, 4);
    for (int y = height - 1; y >= 0; y--) {
      for (int x = 0; x < width; x++) {

        //グレイデータを読み出す
        int i_palette = y * width + x;
        uint8_t colorIdx = v[i_palette];

        //カラーパレットから色を取得
        uint32_t newColor = vcolor_palette[paletteIdx][colorIdx];
        uint8_t r = (newColor >> 16) & 0xFF;
        uint8_t g = (newColor >> 8) & 0xFF;
        uint8_t b = newColor & 0xFF;

        //バッファに書き込み BGRの順になる
        int i_buffer = x * 3;
        buffer[i_buffer] = b;
        buffer[i_buffer + 1] = g;
        buffer[i_buffer + 2] = r;
      }
      file.write(buffer, rowSize);
    }
    file.close();
  } else {
    Serial.printf("Failed to save %s\n", filename);
  }
}

// ファイルとディレクトリを一覧表示する関数
int listFiles(fs::FS &fs, const char * dirname, int levels) {
  int fileCnt = 0;
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return fileCnt;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return fileCnt;
  }

  File file = root.openNextFile();
  while (file) {
    for (int i = 0; i < levels; i++) {
      Serial.print("  "); // インデント
    }
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println(file.name());
      // 再帰的にサブディレクトリも表示
      fileCnt += listFiles(fs, file.path(), levels + 1);
    } else {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
      fileCnt++;
    }
    file = root.openNextFile();
  }
  return fileCnt;
}

void jsonLoad() {
  String filename = "/setting/color.json";
  int iconCnt = 0;
  bool bRead = false;
  bool bSDbegin = false;
  uint8_t* readBuf = NULL;

#if 1
  Serial.println("SD.begin");
//  if (!SD.begin(CS_PIN, SPI, 1000000UL)) {
  if(1){
    bSDbegin = true;
    delay(10);
    uint8_t cardType = SD.cardType();

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    Serial.println("file check");
    filecounter = listFiles(SD, "/Palette", 0);
    iconCnt = listFiles(SD, "/ICON", 0);
    iMenuIconCnt = iconCnt;
    // ファイルが存在するか確認
    if (!SD.exists(filename)) {
      Serial.println("File not Found");
      goto GETSETTING_FAIL;
    }

    Serial.println("File open");
    // ファイルを開く
    File file = SD.open(filename, FILE_READ);
    if (!file) {
      Serial.println("File open fail");
      goto GETSETTING_FAIL;
    }

    Serial.println("File read");
    int size = file.size();
    readBuf = (uint8_t*)malloc(size);
    file.read(readBuf, size);

    DynamicJsonDocument doc(size+100);
    DeserializationError error = deserializeJson(doc, readBuf);
    if(error) {
      Serial.println("deserializeJson fail");
      goto GETSETTING_FAIL;
    } else {
      Serial.println("deserializeJson OK");
      JsonObject root = doc.as<JsonObject>();
      for (JsonPair pair : root) {
        const char* key = pair.key().c_str(); // キー取得
        Serial.println(key);
        if (pair.value().is<JsonArray>()) {
          JsonArray colorArray = pair.value().as<JsonArray>();
          std::vector<uint32_t> v;
          for(int i = 0 ; i < colorArray.size() ; i++) {
            char c[16] = {0};
            const char* c_str_ptr = colorArray[i].as<const char*>();
            char* endptr;
            uint32_t num = strtoul(c_str_ptr, &endptr, 16);
            if (c_str_ptr != endptr && *endptr == '\0') {
            } else {
                num = 0;
            }
            v.push_back(num);
            Serial.printf("%06x ", num);
          }
          Serial.println("");
          vcolor_palette.push_back(v);
        }
      }
      if(vcolor_palette.size() > 0) {
        bRead = true;
      }
    }
//    SD.end();
  } else {
//    Serial.println("SD.begin fail");
  }
  
#endif

GETSETTING_FAIL:
  if(!bRead) {
    uint32_t default_colors[2][16] = {
      {
        0x000000, 0x808080, 0xc0c0c0, 0xffffff,
        0xff0000, 0xffff00, 0x00ff00, 0x00ffff,
        0x0000ff, 0xff00ff, 0x800000, 0x808000,
        0x008000, 0x008080, 0x000080, 0x800080
      },
      {
        0xFADBC0,0xF8CFAF,0xE6B093,0xD38A6B,
        0x2C3E50,0x3E566B,0x4A6C88,0x7395AE,
        0xE74C3C,0xD94436,0xC0392B,0xA32E22,
        0xF1C40F,0xF39C12,0xE67E22,0xFFFFFF
      },
    };
    
    for(int i = 0 ; i < 2 ; i++) {
      std::vector<uint32_t> v;
      for(int j = 0 ; j < 16 ; j++ ) {
        v.push_back(default_colors[i][j]);

      }
      vcolor_palette.push_back(v);
    }
  }

  iPaletteCnt = vcolor_palette.size();
  if(readBuf) free(readBuf);
  return;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(19200);                           // シリアルコンソール開始
  while(!Serial) delay(10);
  delay(500);

  //auto cfg = M5.config();
  //M5.begin(cfg);
  M5.begin();

  pinMode(POWER_GPIO_NUM, OUTPUT);
  digitalWrite(POWER_GPIO_NUM, LOW);
  Wire1.end();
  delay(500);

  Serial.println("SPI.begin");
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);
  while (false == SD.begin(SD.begin(CS_PIN, SPI, 1000000UL))) {
    Serial.println("SD Wait...");
    delay(500);
  }

  jsonLoad();
  SD.end();

  Serial.println("PSRAM Check");
  if (psramFound()) {
    camera_config.pixel_format = PIXFORMAT_RGB565;
    camera_config.fb_location = CAMERA_FB_IN_PSRAM;
    camera_config.fb_count = 2;
  } else {
    delay(500);
  }

  Serial.println("CameraBegin");
  Wire1.end();

  if (!CameraBegin()) {
    Serial.println("CameraBegin Fail");
    delay(1000);
  } else {
    Serial.println("CameraBegin Success");
  }
  delay(500);

  WiFi.softAP(ssid,pass);
  IPAddress myIP = WiFi.softAPIP();
  Serial.println(myIP);
  server.begin();

  Wire.begin(ATOM_ADDR);
  Wire.onReceive( onReceived );
  Wire.onRequest( onRequest );
  delay(500);

  canvas0.createSprite(176, 144);
  Serial.println("Setup End");
}

void loop() {
  // put your main code here, to run repeatedly:
  if(cmdType == CMD_GET_ATOMINFO) {
    Serial.println("CMD_GET_ATOMINFO");
    StaticJsonDocument<200> doc;
    IPAddress myIP = WiFi.softAPIP();
    doc["ssid"] = myIP;
    doc["port"] = USEPORT;
    doc["menuCnt"] = iMenuIconCnt;
    doc["paletteCnt"] = iPaletteCnt;

    String jsonString;
    serializeJson(doc, jsonString);
    i2CSendData = jsonString;
    dataState = STATE_READY_TO_SEND;
  } else if(cmdType == CMD_GET_PALETTE) {
    Serial.println("CMD_GET_PALETTE");
    i2CSendData="";
    for( int i = 0 ; i < vcolor_palette[paletteIdx].size() ; i++ ) {
      char c[16] = {0};
      sprintf(c, "%06x,", vcolor_palette[paletteIdx][i]);
      i2CSendData += c;
    }
    dataState = STATE_READY_TO_SEND;
  } else if (cmdType == CMD_SHOT) {
    keyOnTime = millis();  // 最後にkey操作した時間
    Serial.println("CMD_SHOT");
    SD.end();  // 念のため一旦END
    delay(100);
    while (false == SD.begin(SD.begin(CS_PIN, SPI, 1000000UL))) {
      Serial.println("SD Wait...");
      delay(500);
    }
    CameraGet();  // 撮影
    saveToSD_OriginalBMP();  // 変換前の画像保存
    
    std::vector<uint8_t> v = originalToPalette();
    Serial.print("len: ");
    Serial.println(v.size());

    char c[32];
    memset(c, 0, 32);
    sprintf(c, "%d", v.size());
    i2CSendData = c;
    dataState = STATE_READY_TO_SEND;

    Serial.println("Length send wait");
    while(dataState == STATE_READY_TO_SEND) {
      delay(10);
    }
    Serial.println("Length sent");
    
    sendImage(v);
    saveToSD_ConvertBMP(v);

    CameraFree();   // フレームバッファを解放
    filecounter++;
    SD.end();
  } else if (cmdType == CMD_GET_ICON) {
    Serial.println("CMD_ICON");
    SD.end();  // 念のため一旦END
    delay(100);
    while (false == SD.begin(SD.begin(CS_PIN, SPI, 1000000UL))) {
      Serial.println("SD Wait...");
      delay(500);
    }
    
    std::vector<uint8_t> v = IconToPalette();
    Serial.print("len: ");
    Serial.println(v.size());

    char c[32];
    memset(c, 0, 32);
    sprintf(c, "%d", v.size());
    i2CSendData = c;
    dataState = STATE_READY_TO_SEND;

    Serial.println("Length send wait");
    while(dataState == STATE_READY_TO_SEND) {
      delay(10);
    }
    Serial.println("Length sent");
    
    sendImage(v);
    SD.end();
  } else {
    //Serial.println("CMD_UNKNOWN");
    //CMD_GET_PHOTO,
    //CMD_GET_PHOTO_LIST,
  }
  delay(100);
}
