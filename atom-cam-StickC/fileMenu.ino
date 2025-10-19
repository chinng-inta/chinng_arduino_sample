std::vector<String> vfileMenu;
int uFileSelect = 0;
uint32_t drawRange = 0;
int viewOffset = 0;
M5Canvas menuCanvas(&StickCP2.Display);

void fileMenu_init() {
  vfileMenu.clear();
  uFileSelect = 0;
  viewOffset = 0;
  int y = StickCP2.Display.fontHeight();
  drawRange = (M5.Lcd.height() / y) -2;

  menuCanvas.createSprite(M5.Lcd.width(), M5.Lcd.height());
}

void fileMenu_add(String fileName) {
  vfileMenu.push_back(fileName);
}

String fileMenu_Select() {
  return vfileMenu[uFileSelect];
}

void fileMenu_Up() {
  uFileSelect--;
  if( uFileSelect < 0 ) uFileSelect = 0;
  if(uFileSelect < viewOffset) viewOffset--;
  Serial.printf("fileMenu_Up: uFileSelect: %d, uFileSelect: %d\n", uFileSelect, viewOffset);
}

void fileMenu_Down() {
  uFileSelect++;
  if( uFileSelect >= vfileMenu.size() ) uFileSelect = vfileMenu.size()-1;
  if(uFileSelect > viewOffset + drawRange - 1) viewOffset++;
  Serial.printf("fileMenu_Down: uFileSelect: %d, uFileSelect: %d\n", uFileSelect, viewOffset);
}

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

void fileMenu_exit() {
  vfileMenu.clear();
  menuCanvas.deleteSprite();
}