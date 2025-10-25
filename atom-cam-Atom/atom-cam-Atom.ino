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
#include <unordered_map>
#include "WiFi.h"
// wifiの設定は別ファイルで定義しています。
// Arduinoフォルダにwifi_settingフォルダ、
// そのフォルダにM5_wifi.hを置いてください
#include "..\wifi_setting\M5_wifi.h"

#define LEDIR_PIN 47
#define POWER_GPIO_NUM 18

// 切り出す画像のサイズ
const int crop_width = 128;
const int crop_height = 128;

const int icon_width = 64;
const int icon_height = 64;

camera_fb_t* fb;
// 画像切り出し用のバッファ、ローカル変数で定義すると
// スタックオーバーする。allocしてもいいが、面倒なのでグローバル変数にする
uint8_t crop_data[crop_width][crop_height];

// 写真撮影128x128とアイコン64x64の操作に使うキャンバス
M5Canvas canvas0;
M5Canvas canvas1;

const char *ssid = M5_DEV_SSID;
const char *pass = M5_PASSWORD;
#define USEPORT 8888
int status = WL_IDLE_STATUS;
WiFiServer server(USEPORT); // 80番ポート(http)
WiFiClient client;
const unsigned int CHUNK_SIZE = 1024;

// Atomic TFCard Base のピン配置に合わせて定義
#define SCK_PIN   7
#define MISO_PIN  8
#define MOSI_PIN  6
#define CS_PIN    5

// カラーパレットとメニューの定義
std::vector<std::vector<uint32_t>> vcolor_palette;

#define MENU_MAX 4
#define PALLETE_MAX 10

int iMenuIconCnt = 0;
int iPaletteCnt = 0;

// ファイル名に使用する変数定義
uint32_t keyOnTime = 0;             // キースイッチを操作した時間
int filecounter = 1;
std::vector<String> paletteFile;

// M5 StickC Plus2とのI2C通信の定義
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
  CMD_GET_HEARTBEAT,
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
  .frame_size = FRAMESIZE_128X128,
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

/**
 * @brief I2Cマスターからのデータ受信ハンドラ
 * @details STX/ETXフレーミングでコマンドを受信し、対応する処理を設定
 * @param len 受信データ長
 */
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
    // 写真撮影
    cmdType = CMD_SHOT;
    dataState = STATE_PROCESSING;
  } else if(i2cRecieve.indexOf("GetPhoto") != -1 && i2cRecieve.compareTo("GetPhotoList") != 0 ) {
    // 画像表示
    String strFileName = i2cRecieve;
    strFileName.replace("GetPhoto", "");
    strFileName.trim();
    photoName = strFileName;
    cmdType = CMD_GET_PHOTO;
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
    // アイコン取得
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
  } else if(i2cRecieve.indexOf("HeartBeat") != -1) {
    // I2Cの接続確認
    cmdType = CMD_GET_HEARTBEAT;
    dataState = STATE_PROCESSING;
  } else if(i2cRecieve.compareTo("GetPhotoList") == 0) {
    // 画像一覧取得
    cmdType = CMD_GET_PHOTO_LIST;
    dataState = STATE_PROCESSING;
  } else {
    cmdType = CMD_NONE;
  }
}

/**
 * @brief I2Cマスターからのデータ要求ハンドラ
 * @details 処理状態に応じて適切なレスポンスを送信
 */
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

/**
 * @brief RGB565形式をRGB888形式に変換
 * @param color RGB565形式の色データ
 * @return RGB888形式の色データ
 */
uint32_t rgb565_to_rgb888(uint16_t color) {
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;
  r = (r * 255 + 15) / 31;
  g = (g * 255 + 31) / 63;
  b = (b * 255 + 15) / 31;
  return (r << 16) | (g << 8) | b;
}

/**
 * @brief カラーパレット内で最も近い色を検索
 * @details RGB空間でのユークリッド距離を計算して最適な色を選択
 * @param originalColor 元の色（RGB888形式）
 * @return 最も近い色とそのインデックスのペア
 */
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

/**
 * @brief 撮影した写真をカラーパレットインデックスに変換
 * @details 中央クロップ、ディザリング処理、90度回転を適用
 * @return パレットインデックスの配列
 */
std::vector<uint8_t> originalToPalette() {
  std::vector<uint8_t> v;
  const int bayer4x4[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 }
  };
  // 元画像はRGB565 (1ピクセル2バイト)
  //uint16_t *src_pixels = (uint16_t*)fb->buf;
  uint8_t *src_pixels = (uint8_t*)fb->buf;

  int src_width = fb->width;
  int src_height = fb->height;

  /* --- 中央クロップと減色処理 --- */
  /* 中央を切り出すための開始座標を計算 */
  int crop_start_x = (src_width - crop_width) / 2;
  int crop_start_y = (src_height - crop_height) / 2;

  std::unordered_map<uint32_t, uint8_t> chashedColor;
  memset(crop_data, 0, sizeof(uint8_t) * crop_width * crop_height);
  for (int y = 0; y < crop_height; y++) {
    //for (int x = 0; x < crop_width; x++) {
      for (int x = 0; x < crop_width * 2; x +=2 ) {
      // 元画像から対応するピクセル座標を計算
      int src_x = crop_start_x + x;
      int src_y = crop_start_y + y;
      
      // 元画像のピクセル色(RGB565)を取得
      uint16_t src_color_565 = (src_pixels[src_y * src_width * 2 + src_x] << 8) | src_pixels[src_y * src_width * 2 + src_x+1];
      
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

      uint8_t idx;
      if( chashedColor.find(dst_color_888) != chashedColor.end() ) {
        idx = chashedColor[dst_color_888];
      } else {
        std::pair<uint32_t, uint8_t> pair = findClosestColor(dst_color_888);
        idx = pair.second;
        chashedColor[dst_color_888] = idx;
      }
      
      crop_data[y][x/2] = idx;
    }
  }
  chashedColor.clear();

  // カメラUSBポート下側の画像が撮れるので、90度回転させる
  v.resize(crop_height * crop_width);
  for (int y = 0; y < crop_height; y++) {
    for (int x = 0; x < crop_width; x++) {
      //v.push_back(crop_data[x][crop_height -1 -y]);
      v.at(y * crop_width + x) = crop_data[x][crop_height -1 -y];
    }
  }

  return v;
}

/**
 * @brief アイコン画像をカラーパレットインデックスに変換
 * @details SDカードからPNG画像を読み込み、パレット色に変換
 * @return パレットインデックスの配列
 */
std::vector<uint8_t> IconToPalette() {
  std::vector<uint8_t> v;
  char png_path[64];
  memset(png_path, 0, 64);
  sprintf(png_path, "/ICON/%d.png", iconIdx);

  // canvas1.drawBmpFile(SD, png_path, 0, 0); // はヘッダ回りでコンパイルエラーになったので、地道に算出
  // 1. ファイルを開く
  File pngFile = SD.open(png_path, "r");
  if (!pngFile) {
    Serial.printf("Error: Failed to open %s for reading.\n", png_path);
    return v;
  }
  // 2. バッファ獲得
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

  canvas1.createSprite(icon_width, icon_height); // png読み込み用スプライト
  canvas1.drawPng(png_data_buffer, png_data_size, 0, 0);
  heap_caps_free(png_data_buffer);

  std::unordered_map<uint32_t, uint8_t> chashedColor;
  
  v.resize(icon_height * icon_width);
  for (int y = 0; y < icon_height; y++) {
    for (int x = 0; x < icon_width; x++) {
      // 元画像から対応するピクセル座標を計算
      // 元画像のピクセル色(RGB565)を取得
      //uint16_t src_color_565 = pixel_buffer[y * icon_width + x];
      
      // RGB888に変換して、パレットの最も近い色を探す
      //uint32_t src_color_888 = rgb565_to_rgb888(src_color_565);
      RGBColor src_rgb = canvas1.readPixelRGB(x,y);
      uint32_t src_color_888 = src_rgb.RGB888();

      uint8_t idx;
      if(chashedColor.find(src_color_888) != chashedColor.end()) {
        idx = chashedColor[src_color_888];
      } else {
        std::pair<uint32_t, uint8_t> pair = findClosestColor(src_color_888);
        idx = pair.second;
        chashedColor[src_color_888] = idx;
      }
      v.at(y * icon_width + x) = idx;

      //v.push_backpair.second);
    }
  }
  chashedColor.clear();
  canvas1.deleteSprite();  // png読み込み用スプライト解放

  return v;
}

/**
 * @brief 保存された画像をカラーパレットインデックスに変換
 * @details SDカードからBMP画像を読み込み、中央クロップして変換
 * @param fileName 変換対象の画像ファイル名
 * @return パレットインデックスの配列
 */
std::vector<uint8_t> SelPhotoToPalette( String fileName ) {
  std::vector<uint8_t> v;
  char bmp_path[128];
  memset(bmp_path, 0, 128);
  sprintf(bmp_path, "/Original/%s", fileName.c_str());
  Serial.printf("%s\n", bmp_path);

  // canvas1.drawBmpFile(SD, bmp_path, 0, 0); // はヘッダ回りでコンパイルエラーになったので、地道に算出
  // 1. ファイルを開く
  File bmpFile = SD.open(bmp_path, "r");
  if (!bmpFile) {
    Serial.printf("Error: Failed to open %s for reading.\n", bmp_path);
    return v;
  }

  // 2. バッファ獲得
  size_t bmp_data_size = bmpFile.size();
  uint8_t* bmp_data_buffer = (uint8_t*)heap_caps_malloc(bmp_data_size, MALLOC_CAP_SPIRAM);

  if (!bmp_data_buffer) {
    Serial.println("Failed to allocate buffer for PNG data");
    bmpFile.close();
    return v;
  }

  // 3. ファイルの内容を全てバッファに読み込む
  bmpFile.read(bmp_data_buffer, bmp_data_size);
  bmpFile.close(); // ファイルはすぐに閉じる

  // 4. bitmapヘッダから、画像の切り抜き範囲を算出
  int src_width, src_height;
  lgfx::bitmap_header_t bmpheader;
  memcpy(&bmpheader, bmp_data_buffer, sizeof(lgfx::bitmap_header_t));
  src_width = bmpheader.biWidth;
  src_height = abs(bmpheader.biHeight);

  int crop_start_x = (src_width - crop_width) / 2;
  int crop_start_y = (src_height - crop_height) / 2;
  Serial.printf("src_width: %d, src_height: %d\n", src_width, src_height);
  Serial.printf("crop_start_x: %d, crop_start_y: %d\n", crop_start_x, crop_start_y);

  canvas1.createSprite(crop_width, crop_height); // bmp読み込み用スプライト
  canvas1.drawBmp(bmp_data_buffer, bmp_data_size, 0, 0, crop_width, crop_height, crop_start_x, crop_start_y, 1.0, 1.0, top_left);
  heap_caps_free(bmp_data_buffer);

  std::unordered_map<uint32_t, uint8_t> chashedColor;
  memset(crop_data, 0, sizeof(uint8_t) * crop_width * crop_height);
  for (int y = 0; y < crop_height; y++) {
    for (int x = 0; x < crop_width; x++) {
      // 元画像から対応するピクセル座標を計算
      // 元画像のピクセル色(RGB565)を取得
      //uint16_t src_color_565 = pixel_buffer[y * icon_width + x];
      
      // RGB888に変換して、パレットの最も近い色を探す
      //uint32_t src_color_888 = rgb565_to_rgb888(src_color_565);
      RGBColor src_rgb = canvas1.readPixelRGB(x,y);
      uint32_t src_color_888 = src_rgb.RGB888();

      uint8_t idx;
      if(chashedColor.find(src_color_888) != chashedColor.end()) {
        idx = chashedColor[src_color_888];
      } else {
        std::pair<uint32_t, uint8_t> pair = findClosestColor(src_color_888);
        idx = pair.second;
        chashedColor[src_color_888] = idx;
      }
      
      crop_data[y][x] = idx;
    }
  }
  chashedColor.clear();
  canvas1.deleteSprite();  // png読み込み用スプライト解放

  v.resize(crop_height * crop_width);
  // オリジナル画像はカメラUSBポート下側の画像なので、90度回転させる
  for (int y = 0; y < crop_height; y++) {
    for (int x = 0; x < crop_width; x++) {
      //v.push_back(crop_data[x][crop_height -1 -y]);
      v.at(y * crop_width + x) = crop_data[x][crop_height -1 -y];
    }
  }

  return v;
}

/**
 * @brief 画像データをWiFi経由で送信
 * @details パレットインデックスを圧縮してJSON形式で送信
 * @param v 送信する画像のパレットインデックス配列
 */
void sendImage(std::vector<uint8_t> v) {
  client = server.available();
  if (!client) {
    Serial.println("client not found");
    return;
  }

  // 画像パレットは0x0～0xf(0～15)で4bitしか使用しない
  // データサイズ圧縮のため、32bit変数に8px分データを詰める
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
  //client.println(jsonString);
  
  for (int i = 0; i < jsonString.length(); i += CHUNK_SIZE) {
      int len = min(CHUNK_SIZE, jsonString.length() - i);
      client.print(jsonString.substring(i, i + len));
      delay(10);  // 受信側の処理時間を確保
  }
  client.println("");
  Serial.printf("Sent JSON\n");
}

/**
 * @brief ファイル一覧をWiFi経由で送信
 * @details 保存された画像ファイルのリストをJSON形式で送信
 */
void SendFileList() {
  client = server.available();
  if (!client) {
    Serial.println("client not found");
    return;
  }

  DynamicJsonDocument doc(paletteFile.size()); // 念のため少し余裕を持たせる
  doc["Type"] = "FILELIST";
  JsonArray dataArray = doc.createNestedArray("files");
  for(int i = 0 ; i < paletteFile.size() ; i++) {
    String fileName = paletteFile[i];
    fileName.replace("/Original/", "");
    dataArray.add(fileName);
  }

  String jsonString;
  jsonString.reserve(paletteFile.size()); 
  int jsonLen = serializeJson(doc, jsonString);
  Serial.printf("Sending JSON (%dbytes)...\n", jsonLen);
  // client.println(jsonString);
  for (int i = 0; i < jsonString.length(); i += CHUNK_SIZE) {
      int len = min(CHUNK_SIZE, jsonString.length() - i);
      client.print(jsonString.substring(i, i + len));
      delay(10);  // 受信側の処理時間を確保
  }
  client.println("");
  Serial.printf("Sent JSON\n");
}

/**
 * @brief カメラの初期化
 * @details ESP32カメラの設定と初期化を実行
 * @return 成功時true、失敗時false
 */
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

  // 将来的にM5 StickC Plus2側からカメラ設定を操作できるようにするため、
  // 取得可能な項目すべて表示してみた
#if 0
  s->set_brightness(s, 0); // -2 から 2
  // ホワイトバランス
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 0);
  s->set_wb_mode(s, 0);

  // 露出
  s->set_ae_level(s, 1);
  s->set_aec_value(s, 0);
  s->set_aec2(s, 1);

  // ゲイン
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  
  s->set_raw_gma(s, 1);
  
  s = esp_camera_sensor_get();
  camera_status_t *status = &s->status;
  Serial.printf("framesize: %d\n",status->framesize);
  Serial.printf("scale: %d\n",status->scale);
  Serial.printf("binning: %d\n",status->binning);
  Serial.printf("quality: %d\n",status->quality);
  Serial.printf("brightness: %d\n",status->brightness);
  Serial.printf("contrast: %d\n",status->contrast);
  Serial.printf("saturation: %d\n",status->saturation);
  Serial.printf("sharpness: %d\n",status->sharpness);
  Serial.printf("sharpness: %d\n",status->sharpness);
  Serial.printf("denoise: %d\n",status->denoise);
  Serial.printf("special_effect: %d\n",status->special_effect);
  Serial.printf("wb_mode: %d\n",status->wb_mode);
  Serial.printf("awb: %d\n",status->awb);
  Serial.printf("awb_gain: %d\n",status->awb_gain);
  Serial.printf("aec: %d\n",status->aec);
  Serial.printf("aec2: %d\n",status->aec2);
  Serial.printf("ae_level: %d\n",status->ae_level);
  Serial.printf("aec_value: %d\n",status->aec_value);
  Serial.printf("agc: %d\n",status->agc);
  Serial.printf("agc_gain: %d\n",status->agc_gain);
  Serial.printf("gainceiling: %d\n",status->gainceiling);
  Serial.printf("bpc: %d\n",status->bpc);
  Serial.printf("wpc: %d\n",status->wpc);
  Serial.printf("raw_gma: %d\n",status->raw_gma);
  Serial.printf("lenc: %d\n",status->lenc);
  Serial.printf("hmirror: %d\n",status->hmirror);
  Serial.printf("vflip: %d\n",status->vflip);
  Serial.printf("dcw: %d\n",status->dcw);
  Serial.printf("colorbar: %d\n",status->colorbar);
#endif
  return true;
}

/**
 * @brief カメラから画像を取得
 * @details フレームバッファに画像データを取得
 * @return 成功時true、失敗時false
 */
bool CameraGet() {
  fb = esp_camera_fb_get();
  if (!fb) {
    return false;
  }
  return true;
}

/**
 * @brief カメラフレームバッファを解放
 * @details 使用済みフレームバッファのメモリを解放
 * @return 成功時true、失敗時false
 */
bool CameraFree() {
  if (fb) {
    esp_camera_fb_return(fb);
    return true;
  }
  return false;
}

/**
 * @brief 撮影した元画像をBMP形式でSDカードに保存
 * @details フレームバッファの内容をBMPファイルとして保存し、ファイル一覧に追加
 */
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

    String strFileName = filename;
    strFileName.replace("/Original/", "");
    // 撮影したした画像名を画像一覧に追加
    paletteFile.push_back(strFileName);
  } else {
    Serial.printf("Failed to save %s\n", filename);
  }
}

/**
 * @brief パレット変換後の画像をBMP形式でSDカードに保存
 * @details パレットインデックスからRGB色に変換してBMPファイルを作成
 * @param v パレットインデックスの配列
 */
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

/**
 * @brief ファイルとディレクトリを再帰的に一覧取得
 * @details 指定されたディレクトリ内のファイル一覧を高速に取得
 * @param fs ファイルシステムオブジェクト
 * @param dirname 検索対象ディレクトリ名
 * @param levels 再帰レベル（表示用インデント）
 * @param bDisp デバッグ表示フラグ（デフォルト: false）
 * @return ファイルパスのベクター
 */
std::vector<String> listFiles(fs::FS &fs, const char * dirname, int levels, bool bDisp = false) {
  std::vector<String> vFile;
  if(bDisp) Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return vFile;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return vFile;
  }

  // TFCard Readerのサンプルでは、ファイルオープンするためファイル一覧取得に時間がかかる
  // getNextFileNameの場合、ファイルオープンしないので高速にファイル一覧を取得できる
  // https://qiita.com/Fujix/items/6213bb42700a24aaa11e
  bool isDir;
  String fileName = root.getNextFileName(&isDir);

  while (fileName.length() > 0) {
    if(bDisp) {
      for (int i = 0; i < levels; i++) {
        Serial.print("  "); // インデント
      }
    }
    if (isDir) {
      String subDir = fileName;
//      subDir += "/";
//      subDir += fileName;
      if(bDisp) {
        Serial.print("DIR : ");
        Serial.println(subDir);
      }
      // 再帰的にサブディレクトリも表示
      std::vector<String> vFile2 = listFiles(fs, subDir.c_str(), levels + 1, bDisp);
      vFile.insert(vFile.end(), vFile2.begin(), vFile2.end());
    } else {
      if(bDisp) {
        Serial.print("FILE: ");
        Serial.println(fileName);
      }
      vFile.push_back(fileName);
    }
    fileName = root.getNextFileName(&isDir);
  }
  return vFile;
}

/**
 * @brief カラーパレット設定をJSONファイルから読み込み
 * @details SDカードから設定ファイルを読み込み、パレット情報とファイル一覧を初期化
 */
void jsonLoad() {
  String filename = "/setting/color.json";
  int iconCnt = 0;
  bool bRead = false;
  bool bSDbegin = false;
  uint8_t* readBuf = NULL;

  Serial.println("SD.begin");
  uint8_t cardType = SD.cardType();
  if(cardType != CARD_NONE){
    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    // ついでに撮影した画像一覧とアイコン用の画像の数もカウントする
    Serial.println("file check");
//    listFiles(SD, "/", 0, true);
    paletteFile = listFiles(SD, "/Original/", 0, false);
    filecounter = paletteFile.size();
    std::vector<String> vIcon = listFiles(SD, "/ICON/", 0);
    iMenuIconCnt = vIcon.size();

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
  } else {
    Serial.println("No SD card attached");
  }
  
GETSETTING_FAIL:
  // カラーパレットが読み込めなかったときはデフォルトの値を使う
  if(!bRead) {
    uint32_t default_colors[16] = {
        0x000000, 0x808080, 0xc0c0c0, 0xffffff,
        0xff0000, 0xffff00, 0x00ff00, 0x00ffff,
        0x0000ff, 0xff00ff, 0x800000, 0x808000,
        0x008000, 0x008080, 0x000080, 0x800080
    };
    
    std::vector<uint32_t> v;
    for(int i = 0 ; i < 16 ; i++ ) {
      v.push_back(default_colors[i]);
    }
    vcolor_palette.push_back(v);
  }

  iPaletteCnt = vcolor_palette.size();
  if(readBuf) free(readBuf);
  return;
}

/**
 * @brief Arduino初期化関数
 * @details カメラ、WiFi、SDカード、I2C通信の初期化を実行
 */
void setup() {
  // put your setup code here, to run once:
  Serial.begin(19200);                           // シリアルコンソール開始
  while(!Serial) delay(10);
  delay(500);

  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);  

  //auto cfg = M5.config();
  //M5.begin(cfg);
  M5.begin();

  // カメラ初期化
  pinMode(POWER_GPIO_NUM, OUTPUT);
  digitalWrite(POWER_GPIO_NUM, LOW);
  Wire1.end(); // M5.begin()の中でWire1初期化されているっぽいため、endする
  delay(500);

  Serial.println("PSRAM Check");
  if (psramFound()) {
    camera_config.pixel_format = PIXFORMAT_RGB565;
    camera_config.fb_location = CAMERA_FB_IN_PSRAM;
    camera_config.fb_count = 2;
  } else {
    delay(500);
  }

  Serial.println("CameraBegin");

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

  // SDカード初期化
  // fork元の環境と違い一度ではSD初期化成功しなかった
  Serial.println("SPI.begin");
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);
  while (false == SD.begin(CS_PIN, SPI, 1000000UL)) {
    Serial.println("SD Wait...");
    delay(500);
  }

  paletteFile.clear();
  jsonLoad();
  SD.end();

  // M5 StickC Plus2とのI2C通信の設定
  Wire.begin(ATOM_ADDR);
  Wire.onReceive( onReceived );
  Wire.onRequest( onRequest );
  delay(500);

  canvas0.createSprite(crop_width, crop_height);
  Serial.println("Setup End");
}

/**
 * @brief Arduinoメインループ関数
 * @details I2Cコマンドに応じて画像処理、データ送信などを実行
 */
void loop() {
  if( dataState == STATE_READY_TO_SEND) {
    delay(100);
    return;
  }
  // put your main code here, to run repeatedly:
  // M5 StickC Plus2からI2C通信を受信した場合のイベントドリブンで動作する
  if(cmdType == CMD_GET_ATOMINFO) {
    // M5 Atomの情報取得
    Serial.println("CMD_GET_ATOMINFO");
    StaticJsonDocument<512> doc;
    IPAddress myIP = WiFi.softAPIP();
    doc["ssid"] = myIP;
    doc["port"] = USEPORT;
    doc["menuCnt"] = iMenuIconCnt;
    doc["paletteCnt"] = iPaletteCnt;

    // JsonデータをI2Cでそのまま送信
    String jsonString;
    serializeJson(doc, jsonString);
    i2CSendData = jsonString;
    dataState = STATE_READY_TO_SEND;
  } else if(cmdType == CMD_GET_PALETTE) {
    // カラーパレット取得
    // 6桁x16+1(,)x16の112文字I2Cで送信
    // (jsonにするとI2Cの受信バッファあふれる)
    Serial.println("CMD_GET_PALETTE");
    i2CSendData="";
    for( int i = 0 ; i < vcolor_palette[paletteIdx].size() ; i++ ) {
      char c[16] = {0};
      sprintf(c, "%06x,", vcolor_palette[paletteIdx][i]);
      i2CSendData += c;
    }
    dataState = STATE_READY_TO_SEND;
  } else if (cmdType == CMD_SHOT) {
    // 写真撮影
    keyOnTime = millis();  // 最後にkey操作した時間
    Serial.println("CMD_SHOT");
    SD.end();  // 念のため一旦END
    delay(100);
    while (false == SD.begin(CS_PIN, SPI, 1000000UL) ) {
      Serial.println("SD Wait...");
      delay(500);
    }
    CameraGet();  // 撮影
    saveToSD_OriginalBMP();  // 変換前の画像保存
    
    //saveGraylevel_fromFb();
    // 撮影した画像をカラーパレットのインデックスに変換
    std::vector<uint8_t> v = originalToPalette();
    
    Serial.print("len: ");
    Serial.println(v.size());

    // I2Cで画像のサイズを送信
    // 画像データjsonのバッファはどんぶり勘定で取得
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
    
    // Wifiで画像データ送信
    sendImage(v);

    // カラーパレットで変換した画像を保存
    saveToSD_ConvertBMP(v);

    CameraFree();   // フレームバッファを解放
    filecounter++;
    SD.end();
  } else if (cmdType == CMD_GET_ICON) {
    // アイコン画像の取得
    Serial.println("CMD_ICON");
    SD.end();  // 念のため一旦END
    delay(100);
    while (false == SD.begin(CS_PIN, SPI, 1000000UL) ) {
      Serial.println("SD Wait...");
      delay(500);
    }
    
    // アイコンをカラーパレットのインデックスに変換
    std::vector<uint8_t> v = IconToPalette();
    Serial.print("len: ");
    Serial.println(v.size());

    // I2Cで画像のサイズを送信
    char c[32];
    memset(c, 0, 32);
    sprintf(c, "%d", v.size());
    i2CSendData = c;
    dataState = STATE_READY_TO_SEND;
    SD.end();

    Serial.println("Length send wait");
    while(dataState == STATE_READY_TO_SEND) {
      delay(10);
    }
    Serial.println("Length sent");
    
    // Wifiで画像データ送信
    sendImage(v);
  } else if(cmdType == CMD_GET_HEARTBEAT) {
    // I2Cのハートビート応答、応答データは適当に返す
    Serial.println("CMD_GET_HEARTBEAT");
    i2CSendData="0";
    dataState = STATE_READY_TO_SEND;
  } else if(cmdType == CMD_GET_PHOTO_LIST) {
    // 画像一覧取得
    Serial.println("CMD_GET_PHOTO_LIST");

    // 画像一覧の送信データサイズをI2Cで送信
    char c[32];
    memset(c, 0, 32);
    sprintf(c, "%d", paletteFile.size());
    i2CSendData = c;
    dataState = STATE_READY_TO_SEND;

    Serial.println("Length send wait");
    while(dataState == STATE_READY_TO_SEND) {
      delay(10);
    }
    Serial.println("Length sent");

    SendFileList(); // 画像一覧をwifiで送信
  } else if (cmdType == CMD_GET_PHOTO) {
    // 画像表示
    Serial.println("CMD_GET_PHOTO");
    SD.end();  // 念のため一旦END
    delay(100);
    while (false == SD.begin(CS_PIN, SPI, 1000000UL)) {
      Serial.println("SD Wait...");
      delay(500);
    }

    // 画像データをSDカードから読み込んで、カラーパレットのインデックスに変換
    String fileName = photoName;
    std::vector<uint8_t> v = SelPhotoToPalette(fileName);
    Serial.print("len: ");
    Serial.println(v.size());
    photoName = "";
    SD.end();

    // I2Cで画像のサイズを送信
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
    
    // Wifiで画像データ送信
    sendImage(v);
  } else {
    //Serial.println("CMD_UNKNOWN");
  }
  delay(10);
}
