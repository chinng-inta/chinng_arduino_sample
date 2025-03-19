#include <esp_camera.h>
#include <FastLED.h>
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>

#define KEY_PIN 1
#define LED_PIN 2
#define POWER_GPIO_NUM 18

CRGB LED[1];
camera_fb_t* fb;

//SDカード保存用
char filename[64];
int filecounter = 1;
M5Canvas canvas0;
uint8_t graydata[240 * 176];  //HQVGAが今のところ最適

//最大8色のカラーパレット
uint32_t ColorPalettes[8][8] = {
  { // パレット0 slso8
    0x0D2B45, 0x203C56, 0x544E68, 0x8D697A, 0xD08159, 0xFFAA5E, 0xFFD4A3, 0xFFECD6 },
  { // パレット1 都市伝説解体センター風
    0x000000, 0x000B22, 0x112B43, 0x437290, 0x437290, 0xE0D8D1, 0xE0D8D1, 0xFFFFFF },
  { // パレット2 ファミレスを享受せよ風
    0x010101, 0x33669F, 0x33669F, 0x33669F, 0x498DB7, 0x498DB7, 0xFBE379, 0xFBE379 },
  { // パレット3 gothic-bit
    0x0E0E12, 0x1A1A24, 0x333346, 0x535373, 0x8080A4, 0xA6A6BF, 0xC1C1D2, 0xE6E6EC },
  { // パレット4 noire-truth
    0x1E1C32, 0x1E1C32, 0x1E1C32, 0x1E1C32, 0xC6BAAC, 0xC6BAAC, 0xC6BAAC, 0xC6BAAC },
  { // パレット5 2BIT DEMIBOY
    0x252525, 0x252525, 0x4B564D, 0x4B564D, 0x9AA57C, 0x9AA57C, 0xE0E9C4, 0xE0E9C4 },
  { // パレット6 deep-maze
    0x001D2A, 0x085562, 0x009A98, 0x00BE91, 0x38D88E, 0x9AF089, 0xF2FF66, 0xF2FF66 },
  { // パレット7 night-rain
    0x000000, 0x012036, 0x3A7BAA, 0x7D8FAE, 0xA1B4C1, 0xF0B9B9, 0xFFD159, 0xFFFFFF },
};

int currentPalettelndex = 0;  // 現在のパレットのインデックス
int maxPalettelndex = 8;      //パレット総数

//TailBATを使用しているとき、消費電流が45mA以下だと電源がシャットダウンしてしまう対策
uint32_t LED_ON_DURATION = 120000;  // LED 点灯時間 (ミリ秒)
uint32_t keyOnTime = 0;             //キースイッチを操作した時間

camera_config_t camera_config = {
  .pin_pwdn = -1,
  .pin_reset = -1,
  .pin_xclk = 21,
  .pin_sscb_sda = 12,
  .pin_sscb_scl = 9,
  .pin_d7 = 13,
  .pin_d6 = 11,
  .pin_d5 = 17,
  .pin_d4 = 4,
  .pin_d3 = 48,
  .pin_d2 = 46,
  .pin_d1 = 42,
  .pin_d0 = 3,

  .pin_vsync = 10,
  .pin_href = 14,
  .pin_pclk = 40,

  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,

  .pixel_format = PIXFORMAT_RGB565,
  .frame_size = FRAMESIZE_HQVGA,
  // FRAMESIZE_96X96,    // 96x96
  // FRAMESIZE_QQVGA,    // 160x120
  // FRAMESIZE_QCIF,     // 176x144
  // FRAMESIZE_HQVGA,    // 240x176
  // FRAMESIZE_240X240,  // 240x240
  // FRAMESIZE_QVGA,     // 320x240

  .jpeg_quality = 0,
  .fb_count = 2,
  .fb_location = CAMERA_FB_IN_PSRAM,
  .grab_mode = CAMERA_GRAB_LATEST,
  .sccb_i2c_port = 0,
};

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
  sprintf(filename, "/%010d_%04d_Original.bmp", keyOnTime, filecounter);
  File file = SD.open(filename, "w");
  if (file) {
    uint8_t* out_bmp = NULL;
    size_t out_bmp_len = 0;
    frame2bmp(fb, &out_bmp, &out_bmp_len);
    file.write(out_bmp, out_bmp_len);
    file.close();
    free(out_bmp);
  } else {
    LED[0] = CRGB::Red;
    FastLED.show();  //Error!
  }
}

void saveToSD_ConvertBMP() {
  sprintf(filename, "/%010d_%04d_palette%01d.bmp", keyOnTime, filecounter, currentPalettelndex);
  File file = SD.open(filename, "w");
  if (file) {
    int width = fb->width;
    int height = fb->height;
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

    file.write((std::uint8_t*)&bmpheader, sizeof(bmpheader));
    std::uint8_t buffer[rowSize];
    memset(&buffer[rowSize - 4], 0, 4);
    for (int y = height - 1; y >= 0; y--) {
      for (int x = 0; x < width; x++) {

        //グレイデータを読み出す
        int i_gray = y * width + x;
        uint8_t gray = graydata[i_gray];

        //カラーパレットから色を取得
        uint32_t newColor = ColorPalettes[currentPalettelndex][gray];
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
    LED[0] = CRGB::Red;
    FastLED.show();  //Error!
  }
}

void saveGraylevel() {
  uint8_t* fb_data = fb->buf;
  int width = fb->width;
  int height = fb->height;
  int i = 0;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < (width * 2); x = x + 2) {

      //各ピクセルの色を取得
      uint32_t rgb565Color = (fb_data[y * width * 2 + x] << 8) | fb_data[y * width * 2 + x + 1];

      //RGB565からRGB888へ変換
      uint32_t rgb888Color = canvas0.color16to24(rgb565Color);
      uint8_t r = (rgb888Color >> 16) & 0xFF;
      uint8_t g = (rgb888Color >> 8) & 0xFF;
      uint8_t b = rgb888Color & 0xFF;

      //輝度の計算 BT.709の係数を使用
      uint16_t luminance = (uint16_t)(0.2126 * r + 0.7152 * g + 0.0722 * b);

      //輝度を16階調のグレースケールに変換
      uint8_t grayLevel = luminance / 32;  // 256/32 = 8

      //輝度情報を保存
      graydata[i] = grayLevel;
      i++;
    }
  }
}

void setup() {
  M5.begin();
  pinMode(POWER_GPIO_NUM, OUTPUT);
  digitalWrite(POWER_GPIO_NUM, LOW);
  delay(500);

  pinMode(KEY_PIN, INPUT_PULLUP);
  FastLED.addLeds<SK6812, LED_PIN, GRB>(LED, 1);
  LED[0] = CRGB::Red;
  FastLED.setBrightness(200);

  //一度SDカードをマウントして確認
  SPI.begin(7, 8, 6, -1);
  if (!SD.begin(15, SPI, 10000000)) {
    FastLED.show();  //エラー
    delay(500);
    return;
  }
  delay(100);
  SD.end();  //一旦ENDしておく

  if (psramFound()) {
    size_t psram_size = esp_spiram_get_size() / 1048576;
    camera_config.pixel_format = PIXFORMAT_RGB565;
    camera_config.fb_location = CAMERA_FB_IN_PSRAM;
    camera_config.fb_count = 2;
  } else {
    FastLED.show();  //エラー
    delay(500);
  }

  if (!CameraBegin()) {
    FastLED.show();  //エラー
    delay(1000);
    ESP.restart();
  }
  delay(500);

  LED[0] = CRGB::Blue;  //初期化完了
  FastLED.setBrightness(200);
  FastLED.show();
  delay(500);
  LED[0] = CRGB::LimeGreen;
  FastLED.setBrightness(200);
  FastLED.show();
}

void loop() {

  if (!digitalRead(KEY_PIN)) {
    keyOnTime = millis();  //最後にkey操作した時間
    LED[0] = CRGB::Orange;
    FastLED.setBrightness(20);
    FastLED.show();

    CameraGet();  //撮影

    SD.end();  //念のため一旦END
    delay(100);
    SD.begin(15, SPI, 10000000);

    saveToSD_OriginalBMP();  //変換前の画像保存
    saveGraylevel();         //輝度情報の保存

    for (int i = 0; i < maxPalettelndex; i++) {
      currentPalettelndex = i;
      FastLED.setBrightness(i * 20 + 40);  //処理が進むごとに明るくする
      FastLED.show();
      saveToSD_ConvertBMP();  //変換後の画像保存
    }

    CameraFree();   //フレームバッファを解放
    filecounter++;  //連番を更新
    SD.end();

    LED[0] = CRGB::LimeGreen;
    FastLED.setBrightness(200);
    FastLED.show();
  }

  //一定時間操作していないとLEDをOFF 平均消費電流が45mAを下回るとTailBATが40秒後に自動OFFする
  if ((millis() - keyOnTime >= LED_ON_DURATION)) {
    LED[0] = CRGB::Black;
    FastLED.show();
  }
}
