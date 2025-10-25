#include <M5StickCPlus2.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include <M5StackMenuSystem.h>
#include "WiFi.h"
#include "UNIT_MiniJoyC.h"

// wifiの設定は別ファイルで定義しています。
// Arduinoフォルダにwifi_settingフォルダ、
// そのフォルダにM5_wifi.hを置いてください
#include "..\wifi_setting\M5_wifi.h"

const char *ssid = M5_DEV_SSID;
const char *pass = M5_PASSWORD;
int status = WL_IDLE_STATUS;
WiFiClient client;

M5Canvas canvas(&StickCP2.Display);


// ジョイコンの操作の定義
enum JoyKeyDerection {
  DIRECTION_NNEUTRAL = 0,
  DIRECTION_UP,
  DIRECTION_LEFT,
  DIRECTION_DOWN,
  DIRECTION_RIGHT,
};

#define DIRECTION_THRESHOLD 75
#define SAMPLE_TIMES 100
#define POS_X 0
#define POS_Y 1
UNIT_JOYC sensor;
volatile uint8_t currentDirection = DIRECTION_NNEUTRAL;
uint8_t prevDirection = DIRECTION_NNEUTRAL;

#define MENU_MAX 4
#define PALLETE_MAX 10

// M5 Atom S3R CAMとのI2C通信の定義
#define ATOM_ADDR 0x4B
#define STX 0x02
#define ETX 0x03

// M5 Atom S3R CAMの設定
struct s_atomInfo {
  String ssid;
  uint16_t port;
  uint8_t menuCnt;
  uint8_t paletteCnt;
};
struct s_atomInfo ATOMINFO;

enum e_MenuMode {
  e_Camera = 0,
  e_Photo,
  e_Color,
  e_WifiStatus,
  e_I2CStatus,
  e_Setting,
  e_MenuMax,
};

int g_palletIdx = 0;
int g_iMenuIdx = e_Camera;
std::vector<std::vector<uint8_t>> vMenuColor;
std::vector<uint8_t> vColorIdx;

int btnA_cur_value = 0;   // ボタンAのステータス
int btnA_last_value = 0;  // ボタンAの前回ステータス

int btnB_cur_value = 0;   // ボタンBのステータス
int btnB_last_value = 0;  // ボタンBの前回ステータス

/**
 * @brief ジョイスティックコントローラーのタスク関数
 * @details ジョイスティックの中央位置をキャリブレーションし、継続的に方向を監視する
 * @param arg タスクパラメータ（未使用）
 */
void JoyCTask(void* arg ) {
  int16_t adc_mid_x, adc_mid_y;
  uint16_t adc_cal_data[6];

  int16_t adc_x, adc_y;
  int32_t sum_x = 0;
  int32_t sum_y = 0;
  uint32_t start_time = millis();
  uint32_t cur_time = millis();
  uint16_t cal_times     = 0;

  // 10秒間ジョイコンの中央をサンプリング
  while(1) {
    sum_x += sensor.getADCValue(POS_X);
    sum_y += sensor.getADCValue(POS_Y);
    if(++cal_times >= SAMPLE_TIMES) {
      break;
    }
    cur_time = millis();
    if(cur_time - start_time > 10 * 1000) {
      break;
    }
    delay(100);
  }

  if(cal_times > 0) {
    adc_mid_x = sum_x / cal_times;
    adc_mid_y = sum_y / cal_times;
  }

  sensor.setOneCalValue(4, adc_mid_x);
  sensor.setOneCalValue(5, adc_mid_y);

  while(1) {
    int8_t pos_x    = sensor.getPOSValue(POS_X, _8bit);
    int8_t pos_y    = sensor.getPOSValue(POS_Y, _8bit);
    if( pos_y < -DIRECTION_THRESHOLD) {
      currentDirection = DIRECTION_RIGHT;
    } else if( pos_y > DIRECTION_THRESHOLD) {
      currentDirection = DIRECTION_LEFT;
    } else if( pos_x < - DIRECTION_THRESHOLD) {
      currentDirection = DIRECTION_DOWN;
    } else if( pos_x > DIRECTION_THRESHOLD) {
      currentDirection = DIRECTION_UP;
    } else {
      currentDirection = DIRECTION_NNEUTRAL;
    }
    delay(100);
  }
}

/**
 * @brief I2C通信でデータを受信する関数
 * @details STX/ETXフレーミングを使用してAtomデバイスからデータを受信
 * @param sendData 送信するコマンドデータ
 * @param len 受信バッファの最大長
 * @return 受信したデータ文字列
 */
String recieveI2C(String sendData, int len) {
  String receivedData = "";
  bool bExt = false;
  do {
    bool inPacket = false;
    Wire.requestFrom( ATOM_ADDR, len );
    while ( Wire.available() ) {
      char c = (char)Wire.read();
      if (inPacket) {
        if (c == ETX) {
          // 受信完了
          inPacket = false;
          bExt = true;
          break;
        } else {
          receivedData += c;
        }
      } else if( c == STX ) {
        // 受信開始
        inPacket = true;
      }
    }
    if( receivedData == "busy" ) {
      receivedData = "";
      delay(200);
    }
    if( receivedData == "NoCommand" ) {
      Wire.beginTransmission(ATOM_ADDR);
      Wire.write(STX);
      Wire.print(sendData);
      Wire.write(ETX);
      Wire.endTransmission();
      receivedData = "";
      delay(10);
    }
    if(receivedData.length() == 0) {
      bExt = false;
    }
  } while(!bExt);

  return receivedData;
}

/**
 * @brief M5 Atomデバイスの情報を取得する
 * @details I2C通信でAtomデバイスからSSID、ポート、メニュー数、パレット数を取得
 * @return 成功時0、失敗時負の値
 */
int getAtomInfo() {
  Serial.println("Start getAtomInfo");
  int iRet = -1;

  // 1. I2CでM5 Atomの情報取得要求を送る
  String sendData="GetAtomInfo";
  Wire.beginTransmission(ATOM_ADDR);
  Wire.write(STX);
  Wire.print(sendData);
  Wire.write(ETX);
  Wire.endTransmission();

  // 2. M5 Atomの情報をI2Cで受信
  // Json形式で受信し、デシリアライズ
  delay(200);
  Serial.println("Start recieve getAtomInfo");
  String receivedJson = recieveI2C(sendData, 256);
  Serial.println("End recieve getAtomInfo");

  Serial.println(receivedJson);
  int len = receivedJson.length();
  Serial.println(len);
  if(len > 0) {
    //StaticJsonDocument<256> doc;
    DynamicJsonDocument doc(len*2);
    DeserializationError error = deserializeJson(doc, receivedJson);

    if( error ) {
      Serial.println("Fialed to deseriallize Atom Json");
      iRet = -2;
    } else {
      ATOMINFO.ssid = doc["ssid"].as<const char*>();
      ATOMINFO.port = doc["port"].as<uint16_t>();
      ATOMINFO.menuCnt = doc["menuCnt"].as<uint8_t>();
      ATOMINFO.paletteCnt = doc["paletteCnt"].as<uint8_t>();
      Serial.print(ATOMINFO.ssid);
      Serial.print(": ");
      Serial.println(ATOMINFO.port);
      Serial.print(ATOMINFO.menuCnt);
      Serial.print(", ");
      Serial.println(ATOMINFO.paletteCnt);
      iRet = 0;
    }
  } else {
    Serial.println("Fialed to Recive Atom Info");
  }
  Serial.println("End getAtomInfo");
  return iRet;
}

/**
 * @brief カラーパレットを画面に表示する
 * @details 指定されたパレットインデックスのカラー情報を16進数で表示
 * @param palletIdx 表示するパレットのインデックス
 */
void showPalette(int palletIdx) {
  RGBColor* cl1 = canvas.getPalette();
  uint32_t ccnt = canvas.getPaletteCount();
  StickCP2.Display.printf("palette%d\n", palletIdx);
  StickCP2.Display.println(ccnt);
  for(int i = 0 ; i < ccnt ; i++) {
      char c[16] = {0};
      sprintf(c, "%06x", cl1[i]);
      StickCP2.Display.print(c);
      if(i%5==4) {
          StickCP2.Display.println("");
      } else {
          StickCP2.Display.print(",");
      }
  }
}

/**
 * @brief M5 Atomからカラーパレット情報を受信する
 * @details I2C通信でパレットデータを取得し、カンバスに設定する
 * @param palletIdx 取得するパレットのインデックス（デフォルト: 0）
 * @return 成功時true、失敗時false
 */
bool getPallete(int palletIdx = 0) {
    String sendData = "GetPalette"+String(palletIdx);
    StickCP2.Display.setCursor(0, 0, 1);
    StickCP2.Display.clear();
    StickCP2.Display.print(sendData);
    StickCP2.Display.println("");

    // 1. I2Cでカラーパレットの要求を送る
    Wire.flush();
    Wire.beginTransmission(ATOM_ADDR);
    Wire.write(STX);
    Wire.print(sendData);
    Wire.write(ETX);
    Wire.endTransmission();

    // 2. I2Cでカラーパレットを受信する
    delay(100);
    Serial.println("Start recieve paletteData");
    String receivedData = recieveI2C(sendData, 256);
    Serial.println("End recieve paletteData");
    Serial.printf("Recive size: %d\n", receivedData.length());
    Serial.println(receivedData);
    auto Str2U32 = [](String s) {
      uint32_t retNum = (uint32_t)-1;
      if(s.length() > 0) {    
          const char* c_str_ptr = s.c_str();
          char* endptr;
          uint32_t num = strtoul(c_str_ptr, &endptr, 16);
          if (c_str_ptr != endptr && *endptr == '\0') {
              retNum = num;
          }
      }
      return retNum;
    };

    // 3. ","で区切られたカラーパレットをvectorに追加する
    std::vector<uint32_t> vcolor_palette;
    while(receivedData.indexOf(',') != -1) {
        int idx = receivedData.indexOf(',');
        String s = receivedData.substring(0, idx);
        int num = Str2U32(s);
        if( num != -1 ) {
          vcolor_palette.push_back(num);
        }
        receivedData = receivedData.substring(idx+1);
    }
    if(receivedData.length() > 0) {
      int idx = receivedData.length();
      String s = receivedData.substring(0, idx);
      int num = Str2U32(s);
      if( num != -1 ) {
        vcolor_palette.push_back(num);
      }
    }

    StickCP2.Display.printf("Recive size: %d\n", vcolor_palette.size());
    if(vcolor_palette.size() == 0) {
      return false;
    }

    // 4. カラーパレットを登録
    uint32_t *cl = (uint32_t *)malloc(sizeof(uint32_t) * vcolor_palette.size());
    for(int i = 0 ; i < vcolor_palette.size() ; i++) {
        char c[16] = {0};
        cl[i] = vcolor_palette[i];

        sprintf(c, "%06x", vcolor_palette[i]);
        StickCP2.Display.print(c);
        if(i%5==4) {
            StickCP2.Display.println("");
        } else {
            StickCP2.Display.print(",");
        }
    }
    StickCP2.Display.println("");

    canvas.createPalette(cl, vcolor_palette.size());
    free(cl);
    vcolor_palette.clear();

    showPalette(palletIdx);
    return true;
}

/**
 * @brief M5 Atomから画像データを受信する
 * @details I2CとWiFi通信を組み合わせて画像データを取得し、ピクセル配列として返す
 * @param sendData 送信するコマンド文字列
 * @param iShot 撮影モード（0: 通常, 1: 撮影, 2: 表示）
 * @return 画像ピクセルデータのベクター
 */
std::vector<uint8_t> getImage(String sendData, int iShot = 0) {
  auto resetLcd = []() {
      StickCP2.Display.clear();
      StickCP2.Display.setCursor(0, 0, 1);
  };
  std::vector<uint8_t> v;
  resetLcd();
  Serial.print("Start ");
  Serial.println(sendData);

  uint32_t prevT = millis();
  auto serialPrintTime = [&prevT](String str = "") {
    uint32_t curr = millis();
    Serial.printf("%s: %dms\n", str.c_str(), curr - prevT);
    prevT = curr;
  };

  // 画像データはサイズが大きいため、wifiで受信する
  IPAddress srvIP;
  srvIP.fromString(ATOMINFO.ssid);
  uint16_t srvPort = ATOMINFO.port;
  if (!client.connect(srvIP, srvPort)) {
    Serial.println("Connection failed.");
    Serial.print(srvIP);
    Serial.print(": ");
    Serial.println(srvPort);
    return v;
  }

  // 1. 画像取得要求をI2Cで送る
  int iRet = -1;
  Wire.beginTransmission(ATOM_ADDR);
  Wire.write(STX);
  Wire.print(sendData);
  Wire.write(ETX);
  Wire.endTransmission();
  serialPrintTime("Send I2C");

  // 2. 画像サイズをI2Cで受け取る
  delay(100);
  Serial.println("Start recieve jsonLength");
  int jsonLen = 0;
  do {
    String receivedData = recieveI2C(sendData, 256);
    jsonLen = receivedData.toInt();
  } while(jsonLen == 0);
  Serial.println("End recieve jsonLength");
  Serial.printf("len: %d\n", jsonLen);
  serialPrintTime("recieve jsonLength");

  // 3. 受信した画像サイズで、Json用のバッファを確保
  String json_data;
  json_data.reserve(jsonLen); 
  json_data = "";
  int i = 0;

  // 4. Wifiで画像データを受信
  while (client.connected()) {
    if( i++ % 60 == 0 ) {
        resetLcd();
        StickCP2.Display.print(sendData);
    }
    if (client.available()) {
      //json_data = client.readStringUntil('\n');
      json_data += client.readString();
    }
    //if(json_data.length() > 0) {
    if(json_data.indexOf('\n') != -1) {
      break;
    }
    delay(10);
    StickCP2.Display.print(".");
  }
  client.stop();
  Serial.printf("Image Recieved %d\n", json_data.length());
  serialPrintTime("Image Recieve");

  DynamicJsonDocument docImage(json_data.length() *2); // 念のため少し余裕を持たせる
  DeserializationError error = deserializeJson(docImage, json_data);
  
  serialPrintTime("deserialize Json");
  // 5. 受信した画像データを表示用のvectorに格納
  if (error) {
      StickCP2.Display.println("JSON parse error!");
  } else {
    const char* messageType = docImage["Type"].as<const char*>();
    //StickCP2.Display.printf("\nType: %s\n", messageType);
    Serial.printf("Type: %s\n", messageType);

    if (strcmp(messageType, "ICON") == 0) {
        JsonArray imageArray = docImage["data"].as<JsonArray>();
        size_t total_pixels = imageArray.size() * 8;
        v.resize(total_pixels); // 事前に必要なメモリを確保し、サイズを設定
        uint8_t* pixel_ptr = v.data(); // ベクターの生ポインタを取得
        for(int i = 0 ; i < imageArray.size() ; i++ ) {
            uint32_t packed_pixels = imageArray[i].as<uint32_t>(); // 32ビットのパックされたピクセル値を一度取得
            for(int j = 0 ; j < 8 ; j++) {
                //int px = ((uint32_t)(imageArray[i]) >> (4*j)) & 0xf;
                //v.push_back(px);
                *pixel_ptr++ = (packed_pixels >> (4*j)) & 0xf; // 生ポインタに直接書き込み
            }
            // if(i==0) {
            //   for(int j = 0 ; j < v.size(); j++) {
            //     Serial.printf("%x,", v[j]);
            //   }
            //   Serial.println("");
            // }
        }
        StickCP2.Display.printf("len: %d\n", v.size());
        Serial.printf("len: %d\n", v.size());
    }
    M5.Display.setTextColor(WHITE);
  }
  json_data.clear();//JSONオブジェクトの領域をメモリから解放
  serialPrintTime("Parse Json");
      
  StickCP2.Display.println(sendData + " recieved");
  StickCP2.Display.print(v.size());
  StickCP2.Display.print(", ");

  return v;
}

/**
 * @brief 画像データを画面に描画する
 * @details ピクセルデータを正方形画像として画面中央に表示し、ボタン説明を追加
 * @param v 画像ピクセルデータのベクター
 * @param iShot 表示モード（0: 通常, 1: 撮影, 2: 表示）
 */
void drawImage(std::vector<uint8_t> v, int iShot = 0) {
  // 画像データは正方形を想定
  uint32_t size = (uint32_t)sqrt((float)v.size());
  int dx = (M5.Lcd.width() / 2) - (size / 2);
  int dy = (M5.Lcd.height() / 2) - (size / 2);
  if(size == 0) {
      size = 64;
  }
  uint32_t prevT = millis();
  auto serialPrintTime = [&prevT](String str = "") {
    uint32_t curr = millis();
    Serial.printf("%s: %dms\n", str.c_str(), curr - prevT);
    prevT = curr;
  };
  
  if(v.size() > 0) {
    try {
      // 一括更新: pushImageを使用してピクセルデータを一度に転送
      int len = v.size();
      uint16_t* pixelBuffer = (uint16_t*)heap_caps_malloc(len * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
      RGBColor* cl1 = canvas.getPalette();
      uint32_t ccnt = canvas.getPaletteCount();
      for(int i = 0; i < v.size(); i++) {
        pixelBuffer[i] = v[i];
      }

      canvas.pushImage(dx, dy, size, size, pixelBuffer);
      heap_caps_free(pixelBuffer);
    } catch(const std::bad_alloc& e) {
      // メモリ確保失敗: フォールバック処理（1ピクセルずつ）
      canvas.startWrite();
      for(int i = 0; i < v.size(); i++) {
        int x = i % size;
        int y = i / size;
        canvas.writePixel(dx+x, dy+y, v[i]);
      }
      canvas.endWrite();
      serialPrintTime("pushImage");
    }
  }
  //serialPrintTime("pushImage");
  
  if(iShot == 1) {
      buttonDiscriptionCanvas(canvas, "A:Shot", "B:Menu");
  } else if(iShot == 2) {
      buttonDiscriptionCanvas(canvas, "A:-", "B:Back");
  } else {
      buttonDiscriptionCanvas(canvas, "A:Select", "B:-");
  }

  // メモリ描画領域を座標を指定して一括表示（スプライト）
  canvas.pushSprite(&StickCP2.Display, 0, 0); 
}

/**
 * @brief 画像撮影モードのメニュー処理
 * @details Aボタンで撮影、Bボタンでメニューに戻る操作を処理
 */
void shotMenu() {
  int btnA_cur = 0;   // ボタンAのステータス
  int btnA_last = 0;  // ボタンAの前回ステータス

  int btnB_cur = 0;   // ボタンBのステータス
  int btnB_last = 0;  // ボタンBの前回ステータス
  StickCP2.Display.clear();
  buttonDiscriptionDiret(StickCP2.Display, "A:Shot", "B:Menu");

  while(1) {
    StickCP2.update();
    btnA_cur = StickCP2.BtnA.isPressed();
    btnB_cur = StickCP2.BtnB.isPressed();
    if( btnA_cur != btnA_last && btnA_cur == 0) {
      vColorIdx = getImage("doShot", 1);
      canvas.fillSprite(BLACK);
      if( vColorIdx.size() > 0 ) {
          drawImage(vColorIdx, 1);
      }
    }

    if( btnB_cur != btnB_last && btnB_cur == 0) {
      break;
    }

    btnA_last = btnA_cur;
    btnB_last = btnB_cur;
    delay(100);
  }
}

/**
 * @brief 保存された画像を表示するメニュー処理
 * @details 画像一覧を取得し、選択した画像を表示する機能を提供
 */
void photoMenu() {
  auto resetLcd = []() {
      StickCP2.Display.clear();
      StickCP2.Display.setCursor(0, 0, 1);
  };
  int btnA_cur = 0;   // ボタンAのステータス
  int btnA_last = 0;  // ボタンAの前回ステータス

  int btnB_cur = 0;   // ボタンBのステータス
  int btnB_last = 0;  // ボタンBの前回ステータス
  StickCP2.Display.clear();
  StickCP2.Display.setCursor(0, 0, 1);

  fileMenu_init();

  // 画像一覧データを取得
  IPAddress srvIP;
  srvIP.fromString(ATOMINFO.ssid);
  uint16_t srvPort = ATOMINFO.port;
  if (!client.connect(srvIP, srvPort)) {
    Serial.println("Connection failed.");
    Serial.print(srvIP);
    Serial.print(": ");
    Serial.println(srvPort);
    return;
  }

  // 1. I2Cで画像一覧を要求
  String sendData = "GetPhotoList";
  Wire.beginTransmission(ATOM_ADDR);
  Wire.write(STX);
  Wire.print(sendData);
  Wire.write(ETX);
  Wire.endTransmission();

  // 2. 画像一覧のデータサイズをI2Cで受信
  delay(500);
  Serial.println("Start recieve jsonLength");
  int jsonLen = 0;
  do {
    String receivedData = recieveI2C(sendData, 64);
    jsonLen = receivedData.toInt();
  } while(jsonLen == 0);
  Serial.println("End recieve jsonLength");
  Serial.printf("len: %d\n", jsonLen);

  // 3. 受信したデータサイズで、画像一覧Json用のバッファを獲得
  String json_data;
  json_data.reserve(jsonLen); 
  json_data = "";
  int i = 0;

  // 4. wifiで画像一覧をJson形式で受信し、デシリアライズ
  while (client.connected()) {
    if( i++ % 60 == 0 ) {
        resetLcd();
        StickCP2.Display.print(sendData);
    }
    if (client.available()) {
      //json_data = client.readStringUntil('\n');
      json_data += client.readString();
    }
    //if(json_data.length() > 0) {
    if(json_data.indexOf('\n') != -1) {
      break;
    }
    delay(10);
    StickCP2.Display.print(".");
  }
  client.stop();

  // 5. 受信した画像一覧をメニューに追加
  Serial.printf("Image Recieved %d\n", json_data.length());
  //Serial.println(json_data);
  StickCP2.Display.println("");
  DynamicJsonDocument docImage(json_data.length() *2); // 念のため少し余裕を持たせる
  DeserializationError error = deserializeJson(docImage, json_data);
  
  if (error) {
      StickCP2.Display.println("JSON parse error!");
  } else {
    const char* messageType = docImage["Type"].as<const char*>();
    //StickCP2.Display.printf("Type: %s\n", messageType);
    Serial.printf("Type: %s\n", messageType);

    if (strcmp(messageType, "FILELIST") == 0) {
        JsonArray fileArray = docImage["files"].as<JsonArray>();
        //Serial.println(fileArray);
        for(int i = 0 ; i < fileArray.size() ; i++ ) {
            const char* c = fileArray[i].as<const char*>();
            fileMenu_add(c);
        }
    }
  }
  json_data.clear();//JSONオブジェクトの領域をメモリから解放

  // 画像一覧のメニュー操作
  while(1) {
    StickCP2.update();
    btnA_cur = StickCP2.BtnA.isPressed();
    btnB_cur = StickCP2.BtnB.isPressed();
    if( btnA_cur != btnA_last && btnA_cur == 0) {
      String fileName = fileMenu_Select();
      Serial.println(fileName);
      String sendData = "GetPhoto "+fileName;
      std::vector<uint8_t> v = getImage(sendData, 2);
      drawImage(v, 2);

      int btnB_cur_sub = 0;   // ボタンBのステータス
      int btnB_last_sub = 0;  // ボタンBの前回ステータス
      while(1) {
        StickCP2.update();
        btnB_cur_sub = StickCP2.BtnB.isPressed();
        if( btnB_cur_sub != btnB_last_sub && btnB_cur_sub == 0) {
           break;
        }
        btnB_last_sub = btnB_cur_sub;
        delay(10);
      }
    }

    if( btnB_cur != btnB_last && btnB_cur == 0) {
      fileMenu_exit();
      break;
    }
    if(currentDirection != prevDirection) {
      if( currentDirection == DIRECTION_UP ) {
        fileMenu_Up();
      }
      if( currentDirection == DIRECTION_DOWN ) {
        fileMenu_Down();
      }
    }
    fileMenu_draw();
    btnA_last = btnA_cur;
    btnB_last = btnB_cur;
    prevDirection = currentDirection;
    delay(100);
  }
}

/**
 * @brief カラーパレット選択・取得メニューの処理
 * @details ジョイスティックでパレット選択、Aボタンで取得、Bボタンで戻る
 */
void ColorMenu() {
  int btnA_cur = 0;   // ボタンAのステータス
  int btnA_last = 0;  // ボタンAの前回ステータス

  int btnB_cur = 0;   // ボタンBのステータス
  int btnB_last = 0;  // ボタンBの前回ステータス
  StickCP2.Display.clear();
  StickCP2.Display.setCursor(0, 0, 1);
  showPalette(g_palletIdx);
  while(1) {
    StickCP2.update();
    btnA_cur = StickCP2.BtnA.isPressed();
    btnB_cur = StickCP2.BtnB.isPressed();
    if( btnA_cur != btnA_last && btnA_cur == 0) {
      getPallete(g_palletIdx);
    }

    if( btnB_cur != btnB_last && btnB_cur == 0) {
      break;
    }
    if(currentDirection != prevDirection) {
      Serial.println(currentDirection);
      if( currentDirection == DIRECTION_UP ) {
         g_palletIdx = (g_palletIdx-1 + ATOMINFO.paletteCnt) % ATOMINFO.paletteCnt;
      }
      if( currentDirection == DIRECTION_DOWN ) {
        g_palletIdx = (g_palletIdx+1) % ATOMINFO.paletteCnt;
      }
      StickCP2.Display.setColor(BLACK);
      StickCP2.Display.fillRect(0, 0, M5.Lcd.width(), StickCP2.Display.fontHeight());
      StickCP2.Display.setColor(WHITE);
      StickCP2.Display.setCursor(0, 0, 1);
      StickCP2.Display.printf("Get palette%d\n", g_palletIdx);
    }
    buttonDiscriptionDiret(StickCP2.Display, "A:Get pulette", "B:Menu");
    btnA_last = btnA_cur;
    btnB_last = btnB_cur;
    prevDirection = currentDirection;
    delay(100);
  }
}

/**
 * @brief I2C通信のテストメニュー
 * @details Atomデバイスとの通信状況をハートビートで確認
 */
void i2cMenu() {
  int btnB_cur = 0;   // ボタンBのステータス
  int btnB_last = 0;  // ボタンBの前回ステータス

  StickCP2.Display.clear();
  StickCP2.Display.setCursor(0, 0, 1);
  StickCP2.Display.printf("I2C\n");
  
  int y = StickCP2.Display.getCursorY();
  StickCP2.Display.setScrollRect(0, y, M5.Lcd.width(), M5.Lcd.height() - y*2, true);

  auto I2C_Heartbeat = [](int i) {
    uint32_t start_time = millis();

    String sendData="HeartBeat"+String(i);
    Wire.flush();
    Wire.beginTransmission(ATOM_ADDR);
    Wire.write(STX);
    Wire.print(sendData);
    Wire.write(ETX);
    Wire.endTransmission();

    delay(100);
    String receivedData = recieveI2C(sendData, 30);
    uint32_t cur_time = millis();
    StickCP2.Display.printf("I2C HeartBeat%d: %dmsec\n", i, (cur_time - start_time));
  }; 

  // 5回ハートビートを送り、応答があるか確認
  for( int HBCnt = 0 ; HBCnt < 5 ; HBCnt++ ) {
    I2C_Heartbeat(HBCnt);
    delay(500);
  }

  while(1) {
    StickCP2.update();
    btnB_cur = StickCP2.BtnB.isPressed();
    buttonDiscriptionDiret(StickCP2.Display, "A:-", "B:Menu");

    if( btnB_cur != btnB_last && btnB_cur == 0) {
      break;
    }

    btnB_last = btnB_cur;
    delay(100);
  }
  StickCP2.Display.setTextScroll(false);
}

/**
 * @brief WiFi接続状況を表示するメニュー
 * @details 自身のIPアドレス、接続先情報、接続状況を表示
 */
void wifiMenu() {
  int btnB_cur = 0;   // ボタンBのステータス
  int btnB_last = 0;  // ボタンBの前回ステータス

  StickCP2.Display.clear();
  StickCP2.Display.setCursor(0, 0, 1);
  StickCP2.Display.printf("WI-FI\n");
  IPAddress ip = WiFi.localIP();
  String sIP = "My IP Address: " + ip.toString();
  StickCP2.Display.println(sIP);
  StickCP2.Display.println("");

  IPAddress srvIP;
  srvIP.fromString(ATOMINFO.ssid);
  uint16_t srvPort = ATOMINFO.port;

  StickCP2.Display.printf("SSID: %s\n", ATOMINFO.ssid);
  StickCP2.Display.printf("PORT: %d\n", srvPort);
  StickCP2.Display.printf("Wifi Status: ");

  int curX = StickCP2.Display.getCursorX();
  int curY = StickCP2.Display.getCursorY();

  bool bConnected = false;
  if (client.connect(srvIP, srvPort)) {
    bConnected = true;
  }
  buttonDiscriptionDiret(StickCP2.Display, "A:-", "B:Menu");

  uint32_t startTime = millis();

  while(1) {
    uint32_t curTime = millis();
    StickCP2.update();
    btnB_cur = StickCP2.BtnB.isPressed();
    if( btnB_cur != btnB_last && btnB_cur == 0) {
      break;
    }
    if( curTime > startTime && curTime - startTime > 1000 ) {
      StickCP2.Display.setCursor(curX, curY);
      String sCon = "Unconnected";
      if(bConnected) {
        client.stop();
        bConnected = false;
        sCon = "Connected";
      }
      StickCP2.Display.println(sCon);
      startTime = (uint32_t) __UINT32_MAX__;
    }

    btnB_last = btnB_cur;
    delay(100);
  }
}

/**
 * @brief メニュー用アイコンを取得する
 * @details 全てのメニューアイコンをAtomデバイスから取得し、初期表示を行う
 */
void getIcon() {
  if( ATOMINFO.menuCnt > 0 ) {
    vMenuColor.clear();
    int y = StickCP2.Display.fontHeight();
    for(int i = 0 ; i < ATOMINFO.menuCnt ; i++) {
      String sendData = "GetIcon"+String(i);
      std::vector<uint8_t> v = getImage(sendData, 0);
      vMenuColor.push_back(v);
      //StickCP2.Display.progressBar(0, M5.Lcd.height() - y, M5.Lcd.width(), y, (uint8_t)((double)i)/ ATOMINFO.menuCnt);
    }
    drawImage(vMenuColor[g_iMenuIdx], 0);
  }
}

/**
 * @brief ボタン操作説明をディスプレイに直接表示
 * @details 画面下部にボタンAとボタンBの操作説明を表示
 * @param display 表示対象のM5GFXオブジェクト
 * @param btnADisc ボタンAの説明文字列
 * @param btnBDisc ボタンBの説明文字列
 */
void buttonDiscriptionDiret(M5GFX display, String btnADisc, String btnBDisc) {
  int y = display.fontHeight();
  int x = display.textWidth(btnADisc);
  display.setCursor(0, M5.Lcd.height() - y, 1);
  display.printf("%s", btnBDisc.c_str());

  display.setCursor(M5.Lcd.width() - x, M5.Lcd.height() - y, 1);
  display.printf("%s", btnADisc.c_str());
}

/**
 * @brief ボタン操作説明をキャンバスに表示
 * @details 画面下部にボタンAとボタンBの操作説明をキャンバスに描画
 * @param display 表示対象のM5Canvasオブジェクト
 * @param btnADisc ボタンAの説明文字列
 * @param btnBDisc ボタンBの説明文字列
 */
void buttonDiscriptionCanvas(M5Canvas display, String btnADisc, String btnBDisc) {
  int y = display.fontHeight();
  int x = display.textWidth(btnADisc);
  display.setCursor(0, M5.Lcd.height() - y, 1);
  display.printf("%s", btnBDisc.c_str());

  display.setCursor(M5.Lcd.width() - x, M5.Lcd.height() - y, 1);
  display.printf("%s", btnADisc.c_str());
}


/**
 * @brief Arduino初期化関数
 * @details デバイスの初期化、I2C通信、WiFi接続、ジョイスティック設定を行う
 */
void setup() {
  // put your setup code here, to run once:
  vColorIdx.clear();
  Serial.begin(19200);                           // シリアルコンソール開始
  while(!Serial) delay(10);
  delay(500);                                     // 処理待ち
  
  auto cfg = M5.config();
  StickCP2.begin(cfg);
  StickCP2.Power.begin();
  StickCP2.Display.setRotation(1);
  StickCP2.Display.setFont(&lgfxJapanGothic_24);
  StickCP2.Display.setTextSize(1); // 画面はUSBポートが右側
  StickCP2.Display.setCursor(0, 0, 1);

  // カラーパレット4bit(16色)
  canvas.setColorDepth(4);
  canvas.createSprite(M5.Lcd.width(), M5.Lcd.height());

  // AtomとのI2C通信確率
  Wire.begin();
  Wire.flush();
  getAtomInfo();

  // ジョイコンの初期化
  Wire1.end();  // Wire複数系統使用する場合は、endしてから接続すると上手くいくらしい
                // https://qiita.com/B-SKY-Lab/items/7f7577384a170ea06ac8
  delay(100);
  StickCP2.Display.println("wait joyc");
  while (!(sensor.begin(&Wire1, JoyC_ADDR, 0, 26, 100000UL))) {
      StickCP2.Display.setCursor(0, 15, 1);
      StickCP2.Display.println("I2C Error!");
      Serial.println("I2C Error!");
      delay(100);
  }
  StickCP2.Display.println("connect joyc");
  xTaskCreatePinnedToCore(JoyCTask, "", 8192, NULL, 1, NULL, 1);

  // Wifiの初期化
  StickCP2.Display.clear();
  StickCP2.Display.println("Attempting to connect to SSID:");
  StickCP2.Display.print(ATOMINFO.ssid);
  status = WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      StickCP2.Display.print(".");
  }
  StickCP2.Display.println("\nConnected to WiFi!");
//  server.begin();
  IPAddress ip = WiFi.localIP();
  String sIP = "My IP Address: " + ip.toString();
  StickCP2.Display.println(sIP);

  // カラーパレットとメニュー用アイコンの取得
  if(getPallete(g_palletIdx) == false){
    getPallete(g_palletIdx);
  }
  getIcon();
}


/**
 * @brief Arduinoメインループ関数
 * @details ボタン入力とジョイスティック操作を監視し、メニュー操作を処理
 */
void loop() {
  // put your main code here, to run repeatedly:
  StickCP2.update();                // 本体ボタン状態更新
  btnA_cur_value = StickCP2.BtnA.isPressed();
  btnB_cur_value = StickCP2.BtnB.isPressed();
  if(ATOMINFO.menuCnt == 0) {
    // SDカードが刺さっていないなど、メニュー用アイコンが取得できなかった場合は
    // Aボタンで撮影、Bボタンでカラーパレット変更
    if( btnA_cur_value != btnA_last_value && btnA_cur_value == 0) {
        vColorIdx = getImage("doShot", 1);
        canvas.fillSprite(BLACK);
        if( vColorIdx.size() > 0 ) {
            drawImage(vColorIdx, 1);
        }
    }

    if( btnB_cur_value != btnB_last_value && btnB_cur_value == 0) {
      g_palletIdx++;
      if( g_palletIdx >= ATOMINFO.paletteCnt) {
        g_palletIdx = 0;
      }
      if(getPallete(g_palletIdx) == false){
        getPallete(g_palletIdx);
      }
    }
  } else {
    // アイコンが取得出来た場合は、ジョイコンでメニュー変更
    // Aボタンでメニュー項目に入る
    canvas.fillSprite(BLACK);
    if(currentDirection != prevDirection) {
      Serial.println(currentDirection);
      if( currentDirection == DIRECTION_LEFT ) {
         g_iMenuIdx = (g_iMenuIdx-1 + ATOMINFO.menuCnt) % ATOMINFO.menuCnt;
      }
      if( currentDirection == DIRECTION_RIGHT ) {
        g_iMenuIdx = (g_iMenuIdx+1) % ATOMINFO.menuCnt;
      }
      drawImage(vMenuColor[g_iMenuIdx], 0);
    }

    if( btnA_cur_value != btnA_last_value && btnA_cur_value == 0) {
      if(g_iMenuIdx == e_Color) {
        ColorMenu();
        getIcon();
      } else if(g_iMenuIdx == e_Camera) {
        shotMenu();
        canvas.fillSprite(BLACK);
        drawImage(vMenuColor[g_iMenuIdx], 0);
      } else if(g_iMenuIdx == e_I2CStatus) {
        i2cMenu();
        canvas.fillSprite(BLACK);
        drawImage(vMenuColor[g_iMenuIdx], 0);
      } else if(g_iMenuIdx == e_Photo) {
        photoMenu();
        canvas.fillSprite(BLACK);
        drawImage(vMenuColor[g_iMenuIdx], 0);
      } else if(g_iMenuIdx == e_WifiStatus) {
        wifiMenu();
        canvas.fillSprite(BLACK);
        drawImage(vMenuColor[g_iMenuIdx], 0);
      } else {
        //e_Setting,
      }
    }
  }
  btnA_last_value = btnA_cur_value;
  btnB_last_value = btnB_cur_value;
  prevDirection = currentDirection;
  delay(10);
}
