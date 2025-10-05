#include <ArduinoGraphics.h> // LEDマトリクスに文字列表示する際に参照
#include <Arduino_LED_Matrix.h>

// LEDマトリクスのインスタンス作成
ArduinoLEDMatrix matrix;

// LEDマトリクスのサイズを定義
#define MATRIX_WIDTH 12
#define MATRIX_HEIGHT 8

#define UPDATE_INTERVAL 200 // 世代の更新間隔
#define INIT_PROB 20 // ランダム初期化時の生存の割合

// 現在世代と次世代の状態
uint8_t world[MATRIX_HEIGHT][MATRIX_WIDTH];
uint8_t next_world[MATRIX_HEIGHT][MATRIX_WIDTH];

// グライダーのパターン
uint8_t glider[MATRIX_HEIGHT][MATRIX_WIDTH] = {
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

// 宇宙船のパターン
uint8_t spaceShip[MATRIX_HEIGHT][MATRIX_WIDTH] = {
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},  
  {0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
  {0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

uint8_t acorns[MATRIX_HEIGHT][MATRIX_WIDTH] = {
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
  {0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

uint16_t  u16Gen = 0;

// パターン初期化
void initWorld(char c) {
  switch(c) {
    case 'g' :
      memcpy(world, glider, sizeof(world));
      break;
    case 's' :
      memcpy(world, spaceShip, sizeof(world));
      break;
    case 'a' :
      memcpy(world, acorns, sizeof(world));
      break;
  }
  u16Gen = 0;
}

// ランダム初期化
void initWorld() {
  for(int y = 0 ; y < MATRIX_HEIGHT ; y++) {
    for(int x = 0 ; x < MATRIX_WIDTH ; x++) {
      world[y][x]= (random(100) < INIT_PROB) ? 1 : 0;
    }
  }
  u16Gen = 0;
}

// LEDマトリクス制御
void drawWorld() {
  // arduino uno r4のLEDマトリクスは32bitずつセットする
  uint32_t frame[3] = {0};

  for( int i = 0 ; i < MATRIX_HEIGHT * MATRIX_WIDTH ; i++ ) {
    int x = i % MATRIX_WIDTH;
    int y = i / MATRIX_WIDTH;
    int idx = i / 32;
    int bit = i % 32;
    if( world[y][x] == 1 ) {
      frame[idx] |= (1 << (31-bit)); 
    }
  }
  // LEDマトリックスに一括セット
  matrix.loadFrame(frame);
}

// 周囲8マスの生存をカウント
int countNeighbors(int x, int y) {
  int iCnt = 0 ;
  for(int dy = -1 ; dy <= 1; dy++ ) {
    for(int dx = -1 ; dx <= 1; dx++ ) {
      // 自分はカウント対象外
      if (dx == 0 && dy == 0) {
        continue;
      }

      // 端と端がつながる構造で実装
      int nx = (x + dx + MATRIX_WIDTH) % MATRIX_WIDTH;
      int ny = (y + dy + MATRIX_HEIGHT) % MATRIX_HEIGHT;

      iCnt += world[ny][nx];
    }
  }
  return iCnt;
}

// 次世代の計算
bool calculateNextGeneration(int iLiveMin, int iLiveMax, int iBorn) {
  static uint8_t u8Cmp[MATRIX_HEIGHT][MATRIX_WIDTH] = {0};
  bool bContinue = false;
  for( int y = 0 ; y < MATRIX_HEIGHT ; y++ ) {
    for( int x = 0 ; x < MATRIX_WIDTH ; x++ ) {
      int neighbors = countNeighbors(x, y);
      if(world[y][x] == 1) { // 現在、生存の場合
        // 過疎または過密
        if( neighbors < iLiveMin || neighbors > iLiveMax ) {
          next_world[y][x] = 0; // 死
        } else {
          next_world[y][x] = 1; // 生存
        }
      } else { // 現在、死の場合
        if( neighbors == iBorn ) {
          next_world[y][x] = 1; // 誕生
        } else {
          next_world[y][x] = 0; // 死のまま
        }
      } 
    }
  }

  memcpy(world, next_world, sizeof(world));

  if( memcmp( world, u8Cmp, sizeof(world) ) )  bContinue = true;
  return bContinue;
}

void setup() {
  // put your setup code here, to run once:
  // デバッグ用シリアル通信開始
  Serial.begin(9600);
  while(!Serial) delay(10);

  memset(world, 0, sizeof(uint8_t) * MATRIX_HEIGHT * MATRIX_WIDTH );

  // LEDマトリクス初期化
  matrix.begin();

  // 乱数のシードを設定
  long r = 0;
  for ( int i = 0 ; i < 5 ; i++ ){
    r = (r << 1) ^ analogRead(A0);
    Serial.println(r);
  }
  Serial.println(0);
 
  randomSeed(r);

  // ランダム初期化
  initWorld();
}

void loop() {
  // LEDマトリクスに文字列表示用lambda式
  auto drawMatrix = [](char *cMsg, int iSpeed = 100) {
    matrix.beginDraw();
    //matrix.stroke(0xFFFFFF);                    // 色の指定 // LEDの色はないので、無くてもよさそう
    matrix.textScrollSpeed(iSpeed);               // スクロールスピード

    matrix.textFont(Font_5x7);                    // フォントサイズ
    matrix.beginText(2, 1, 0xFFFFFF);             // 文字列表示　x,y,color
    matrix.println(cMsg);                         // 文字列表示
    matrix.endText(SCROLL_LEFT);                  // スクロールの方向

    matrix.endDraw();
  };

  // put your main code here, to run repeatedly:
  drawWorld();
  if( calculateNextGeneration(2, 3, 3) ) {
    u16Gen++;
    // 1つでも生存している場合
    delay(UPDATE_INTERVAL);
  } else {
    // 全て死の場合、新規開始
    drawWorld();
    delay(2500);
    char cMsg[32];
    memset(cMsg, 0, sizeof(cMsg));
    sprintf(cMsg, "Game Over!(%dGen)", u16Gen);
    drawMatrix(cMsg);
    delay(500);
    drawMatrix("Start New Life!", 75);
    initWorld();
  }

  // シリアル入力で新規開始
  // 'r': ランダム配置で開始
  // 'g': グライダーで開始
  // 's': 宇宙船で開始
  if( Serial.available() > 0 ) {
    char c = Serial.read();
    if(c == 'r') {
      drawMatrix("Recieve Restart!", 75);
      initWorld();
    } else if(c == 'g') {
      drawMatrix("Recieve Glider!", 75);
      initWorld(c);
    } else if(c == 's') {
      drawMatrix("Recieve SpaceSpip!", 75);
      initWorld(c);
    } else if(c == 'a') {
      drawMatrix("Recieve Acorns!", 75);
      initWorld(c);
    }
    Serial.write(c);
  }
}
