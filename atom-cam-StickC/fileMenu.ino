std::vector<String> vfileMenu;
int uFileSelect = 0;
uint32_t drawRange = 0;
int viewOffset = 0;
M5Canvas menuCanvas(&StickCP2.Display);

/**
 * @brief ファイルメニューの初期化
 * @details ファイルリストをクリアし、表示範囲とキャンバスを初期化
 */
void fileMenu_init() {
  vfileMenu.clear();
  uFileSelect = 0;
  viewOffset = 0;
  int y = StickCP2.Display.fontHeight();
  drawRange = (M5.Lcd.height() / y) -2;

  menuCanvas.createSprite(M5.Lcd.width(), M5.Lcd.height());
}

/**
 * @brief ファイルメニューにファイル名を追加
 * @param fileName 追加するファイル名
 */
void fileMenu_add(String fileName) {
  vfileMenu.push_back(fileName);
}

/**
 * @brief 現在選択されているファイル名を取得
 * @return 選択中のファイル名
 */
String fileMenu_Select() {
  return vfileMenu[uFileSelect];
}

/**
 * @brief ファイルメニューの選択を上に移動
 * @details 選択位置を上に移動し、必要に応じて表示オフセットを調整
 */
void fileMenu_Up() {
  uFileSelect--;
  if( uFileSelect < 0 ) {
    // uFileSelect = 0;
    uFileSelect = vfileMenu.size() - 1;
    viewOffset = uFileSelect - (drawRange-1);
  } else {
    if(uFileSelect < viewOffset) viewOffset--;
  }
  Serial.printf("fileMenu_Up: uFileSelect: %d, uFileSelect: %d\n", uFileSelect, viewOffset);
}

/**
 * @brief ファイルメニューの選択を下に移動
 * @details 選択位置を下に移動し、必要に応じて表示オフセットを調整
 */
void fileMenu_Down() {
  uFileSelect++;
  if( uFileSelect >= vfileMenu.size() ) {
    //uFileSelect = vfileMenu.size()-1;
    uFileSelect = viewOffset = 0;
  } else {
    if(uFileSelect > viewOffset + drawRange - 1) viewOffset++;
  }

  Serial.printf("fileMenu_Down: uFileSelect: %d, uFileSelect: %d\n", uFileSelect, viewOffset);
}

/**
 * @brief ファイルメニューを画面に描画
 * @details ファイル一覧を表示し、選択中の項目をハイライト表示
 */
void fileMenu_draw() {
  menuCanvas.fillSprite(BLACK);
  menuCanvas.setCursor(0, 0, 1);
  menuCanvas.printf("== File List(%d) ==\n", vfileMenu.size());

  for(int i = 0 ; i < drawRange ; i++) {
    int idx = i + viewOffset;
    if( idx == uFileSelect ) {
      menuCanvas.fillRect(0, (i+1)*StickCP2.Display.fontHeight(),M5.Lcd.width(), StickCP2.Display.fontHeight(), BLUE);
    }
    menuCanvas.printf("%d: %s\n", idx, vfileMenu[idx].c_str());
  }

  buttonDiscriptionCanvas(menuCanvas, "A:Select Photo", "B:Menu");
  menuCanvas.pushSprite(&StickCP2.Display, 0, 0);
}

/**
 * @brief ファイルメニューの終了処理
 * @details ファイルリストをクリアし、キャンバスのメモリを解放
 */
void fileMenu_exit() {
  vfileMenu.clear();
  menuCanvas.deleteSprite();
}