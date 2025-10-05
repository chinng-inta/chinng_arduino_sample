#include <M5StickCPlus2.h>
//#include <M5Unified.h>
#include "WiFi.h"
#include "..\wifi_setting\M5_wifi.h"

const uint8_t BTN1_PIN        = 37;                   // 物理ボタンA（G37）の定義
// --- PWM設定用の定数 ---
const uint8_t FAN_PWM_PIN     = 26;                   // ファン回転数制御用PWM制御  PWM control for fan speed control.
const uint8_t FAN_SENSOR_PIN  = 36;                    // ファン回転数計算用GPOの定義
const int FAN_PWM_FREQ = 25000;    // PWM周波数 (Hz)
const int PWM_CHANNEL1 = 1;    // PWMチャンネル (0-15)
const int PWM_RESOLUTION = 8; // PWMの解像度 (8bit = 0-255段階)

int btn1_cur_value = 0;                           // 物理ボタンAのステータス格納
int btn1_last_value = 0;                          // 物理ボタンAの前回ステータス格納

// ファン回転数取得用定義
unsigned long lastPulse_T;                        // 前回の割込み時の時間を格納
unsigned long pulse_Interval=0;                   // パルスのインターバルを格納
uint16_t rpm;                                     // １分中の回転数を格納

int giCnt = 0;
bool gbInc = true;

const char ssid[] = M5_SSID;
const char pass[] = M5_PASSWORD;
const int port = 8888;

// ------------------------------------------------------------
// FAN回転パルス（立下り）検出用の関数 Function for FAN rotation pulse (falling) detection.
// Fan_Rotate_Sence()
// https://karakuri-musha.com/inside-technology/arduino-m5stickc-05-fan-rotation-control-rpm/
// から借用
// ------------------------------------------------------------
void Fan_Rotate_Sence() {
  unsigned long cur = micros();                   // 割込み発生時の時間を格納
  unsigned long dif = cur - lastPulse_T;          // 前回の割込み時間との差分
  pulse_Interval = (pulse_Interval - (pulse_Interval >> 2)) + (dif >> 2); // インターバルの計算
  lastPulse_T = cur;                              // 前回の割込み時間の更新
}

void FanTask( void* arg ) {
  while(1) {
    if( gbInc ) {
      giCnt = giCnt + 10;
      if( giCnt >= 100) {
        gbInc = false;
        giCnt = 100;
      } 
    } else {
      giCnt = giCnt - 10;
      if( giCnt <= 0) {
        gbInc = true;
        giCnt = 0;
      } 
    }

    uint8_t dutyCycle = 255 * ((double)(giCnt / 100.0));
    ledcWrite(FAN_PWM_PIN, dutyCycle);
    Serial.println(dutyCycle);
    if(giCnt == 0) {
      do {
        delay(500);
      } while( rpm >= 318 );
    } else {
      int cnt = 0;
      int max = 3 * 2;
      if(giCnt == 100) {
        max = 10 * 2;
      }
      do {
        delay(500);
      } while( ++cnt < max );
    }
  }
}

void display_output(auto val, int x = 0, int y = 0, int Size = 1){
  //　プログラムタイトル表示部分
  StickCP2.Display.setTextSize(Size);                          // テキストサイズの指定
  StickCP2.Display.setTextColor(WHITE, BLACK);              // テキストカラーの設定
  StickCP2.Display.setCursor(x, y*20, Size);                      // カーソル位置とフォントの設定
  String str = String(val);
  StickCP2.Display.print(str);           // ディスプレイに表示（プログラム名）
}

void setup() {
  // シリアルコンソールの開始　Start serial console.
  Serial.begin(9600);                           // シリアルコンソール開始
  while(!Serial) delay(10);
  delay(500);                                     // 処理待ち
  // put your setup code here, to run once:
  auto cfg = M5.config();
  StickCP2.begin(cfg);

  Serial.println("Wifi Connection Start");
  // Wi-Fi接続の開始
  WiFi.begin(ssid, pass);

  // 接続が確立するまで待機
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.println("Wifi Connection Complete");

  // 出力GPIOの設定
  pinMode(FAN_PWM_PIN, OUTPUT);                       // PWM出力用ピンG26の設定

  // 入力GPIOの設定
  pinMode(FAN_SENSOR_PIN, INPUT);                     // ファン回転数計算のための信号用ピンG36の設定

  ledcAttachChannel(FAN_PWM_PIN, FAN_PWM_FREQ, PWM_RESOLUTION, PWM_CHANNEL1);
  ledcAttach(FAN_PWM_PIN, FAN_PWM_FREQ, PWM_RESOLUTION);
  uint8_t dutyCycle = giCnt;
  ledcWrite(FAN_PWM_PIN, dutyCycle);

  // FAN回転パルス取得関連の初期化と設定
  lastPulse_T = 0;
  pulse_Interval = 0;
  attachInterrupt(FAN_SENSOR_PIN, Fan_Rotate_Sence, FALLING);  // FAN_SENSOR用GPIO（G36）が立ち下がった（FALLING）ときに関数を実行

  StickCP2.Display.setRotation(3);                          // 画面の向きを変更（右横向き）Change screen orientation (left landscape orientation).
  StickCP2.Display.clear();
  StickCP2.Display.setBrightness(255);
  display_output("Incliment", 2, 0, 1);
  String sPar = "Parcentage: " + String(giCnt) + "%";
  display_output(sPar, 2, 1, 1);
  IPAddress ip = WiFi.localIP();
  String sIP = "My IP Address: " + ip.toString();
  Serial.println(sIP);
  display_output(sIP, 2, 5, 1);
  
}

void loop() {
  StickCP2.update();                // 本体ボタン状態更新
  StickCP2.Display.clear();
//  M5.Lcd.fillScreen(BLACK);                       // 画面の塗りつぶし　Screen fill.

  // Aボタン押したら、インクリメント、デクリメント切り替え
  btn1_cur_value = StickCP2.BtnA.isPressed();
  if(btn1_cur_value != btn1_last_value){
    if(btn1_cur_value==0){
        gbInc ^= 0x1;
    }
    else {
    }
    StickCP2.Display.setBrightness(255);
    btn1_last_value = btn1_cur_value;
  }

  // put your main code here, to run repeatedly:
  if(gbInc) {
    display_output("Incliment", 2, 0, 1);
  } else {
    display_output("Decliment", 2, 0, 1);
  }
  String sPar = "Parcentage: " + String(giCnt) + "";
  display_output(sPar, 2, 1, 1);
  Serial.println(sPar);

  uint8_t dutyCycle = 255 * ((double)(giCnt / 100.0));
  display_output("DutyVal: " + String(dutyCycle), 2, 2, 1);
  auto getRPM = []() {
    if (pulse_Interval != 0) {                      // インターバルが0以外の時
      rpm = 60000000 / (pulse_Interval * 2);        // 1分あたりの回転数（RPM）を求める
      display_output("RPM: " + String(rpm) + "rpm", 2, 3, 1);
    } 
    else {                                          // インターバルが0の時（0除算の回避）
      display_output("RPM: 0rpm", 2, 3, 1);
    }
    
    int lev = StickCP2.Power.getBatteryLevel();
    String sPow = "Battry: " + String(lev) + "";
    display_output(sPow, 2, 4, 1);
  };
  getRPM();
  // IPAddress ip = WiFi.localIP();
  // String sIP = "My IP Address: " + ip.toString();
  // display_output(sIP, 2, 5, 1);
  // int n = WiFi.scanNetworks();
  // for (int i = 0; i < n ; i++ ) {
  //   String rssid = WiFi.SSID(i);
  //   display_output(rssid, 2, 6, 1);
  //   delay(2000);
  // }
  delay(1000);
}
