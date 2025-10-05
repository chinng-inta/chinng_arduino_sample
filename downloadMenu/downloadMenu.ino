//#include <M5Stack.h>
#include <M5StickCPlus2.h>
#include <Wire.h>

M5Canvas canvas(&StickCP2.Display);

#define CARDKB_ADDR 0x5F  // Define the I2C address of CardKB.

int g_palletIdx = 0;

std::vector<uint32_t> vcolorIdx;

int btn1_cur_value = 0;                           // 物理ボタン１のステータス格納
int btn1_last_value = 0;                          // 物理ボタン１の前回ステータス格納

enum e_keyInputMode {
    e_keyModeNone = 0,
    e_keyModeGetPalette,
    e_keyModeEnter,
    e_keyModeReset,
    e_keyModeUp,
    e_keyModeDown,
    e_keyModeLeft,
    e_keyModeRight,
    e_keyModeShot,    
    e_keyModeMax,
};

int keyMode = e_keyModeNone;

void keyCheck(void* arg ) {
    while(1) {
        Wire.requestFrom( CARDKB_ADDR, 1 );
        while ( Wire.available() )
        {
            char c = Wire.read();  // 1Byte読み込む
            if (c != 0) {
                switch(c) {
                    case 'p':
                        keyMode = e_keyModeGetPalette;
                        break;
                    case 0x0D:
                        keyMode = e_keyModeEnter;
                        break;
                    case 'r':
                        keyMode = e_keyModeReset;
                        break;
                    case 's':
                        keyMode = e_keyModeShot;
                        break;
                    case 0xB5:
                        keyMode = e_keyModeUp;
                        break;
                    case 0xB6:
                        keyMode = e_keyModeDown;
                        break;
                    case 0xB4:
                        keyMode = e_keyModeLeft;
                        break;
                    case 0xB7:
                        keyMode = e_keyModeRight;
                        break;
                    default:
                        keyMode = e_keyModeNone;
                        break;
                }
            }
        }
        delay(100);
    }
}

std::vector<uint32_t> Communication32(String sendMsg, String lcdMsg) {
    auto resetLcd = []() {
        StickCP2.Display.clear();
        StickCP2.Display.setCursor(0, 0, 1);
    };
    Serial.println(sendMsg);

    while(1) {
        if(Serial.available()) {
            char c = Serial.read();
            if( c == 0x2) break; 
        }
    }
    String sourceStr = "";
    int i = 0;
    while(sourceStr.indexOf(0x3) == -1) {
        if( i++ % 60 == 0 ) {
            resetLcd();
            StickCP2.Display.print(lcdMsg);
        }
        if(Serial.available()) {
            char buffer[1024];
            memset(buffer, 0, 1024);
            // 改行コード('\n') またはタイムアウトまで読み込み
            size_t len = Serial.readBytesUntil('\n', buffer, 1022);
            // 受信したデータがあるか確認
            if (len > 0) {
                buffer[len] = ','; // 終端文字を追加
                buffer[len+1] = '\0'; // 終端文字を追加
                sourceStr += buffer;
            } else {
                delay(100);
            }
        }
        StickCP2.Display.print(".");
        //delay(10);
    }

    resetLcd();

    StickCP2.Display.println(lcdMsg + " recieved");

    std::vector<uint32_t> v;
    while(sourceStr.indexOf(',') != -1) {
        int idx = sourceStr.indexOf(',');
        String s = sourceStr.substring(0, idx);
           
        if(s.length() > 0) {
            const char* c_str_ptr = s.c_str();
            char* endptr;
            uint32_t num = strtoul(c_str_ptr, &endptr, 16);
            if (c_str_ptr != endptr && *endptr == '\0') {
                v.push_back(num);
            }
        } 
        sourceStr = sourceStr.substring(idx+1);
    }
    if(sourceStr.length() > 0) {
        int idx = sourceStr.length();
        if(sourceStr.indexOf(0x3) != -1) {
            idx = sourceStr.indexOf(0x3);
        }
        String s = sourceStr.substring(0, idx);
        if(s.length() > 0) {    
            const char* c_str_ptr = s.c_str();
            char* endptr;
            uint32_t num = strtoul(c_str_ptr, &endptr, 16);
            if (c_str_ptr != endptr && *endptr == '\0') {
                v.push_back(num);
            }
        }
    }
    StickCP2.Display.print(v.size());
    StickCP2.Display.print(", ");
    return v;
}

void getPallete(int palletIdx = 0) {
    String spIdx = "palette"+String(palletIdx);
    StickCP2.Display.setCursor(0, 0, 1);
    StickCP2.Display.print(spIdx); 

    std::vector<uint32_t> vcolor_palette = Communication32(spIdx, spIdx);
    if(vcolor_palette.size() > 16) {
        vcolor_palette.resize(16);
    }

    StickCP2.Display.println(vcolor_palette.size());
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

    RGBColor* cl1 = canvas.getPalette();
    uint32_t ccnt = canvas.getPaletteCount();
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

void setup() {
    // put your setup code here, to run once:
    vcolorIdx.clear();
    Serial.begin(19200);                           // シリアルコンソール開始
    while(!Serial) delay(10);
    delay(500);                                     // 処理待ち
    
    while(Serial.available()) {
        char c = Serial.read();
    }
    Wire.begin();

     auto cfg = M5.config();
    StickCP2.begin(cfg);
    StickCP2.Power.begin();
    StickCP2.Display.setRotation(1);
    StickCP2.Display.setFont(&lgfxJapanGothic_24);
    StickCP2.Display.setTextSize(1);

    canvas.setColorDepth(4);
    canvas.createSprite(M5.Lcd.width(), M5.Lcd.height());

    xTaskCreatePinnedToCore(keyCheck, "", 1024, NULL, 1, NULL, 1);

    getPallete(g_palletIdx);

#if 0
    char cmsg[32];
    memset(cmsg, 0, 32);
    sprintf(cmsg, "Free Heap: %d", ESP.getFreeHeap());
    StickCP2.Display.println(cmsg);
    memset(cmsg, 0, 32);
    sprintf(cmsg, "Total Heap: %d", ESP.getHeapSize());
    StickCP2.Display.println(cmsg);
    memset(cmsg, 0, 32);
    sprintf(cmsg, "Free PSRAM: %d", ESP.getPsramSize());
    StickCP2.Display.println(cmsg);
    memset(cmsg, 0, 32);
    sprintf(cmsg, "Total PSRAM: %d", ESP.getFreePsram());
    StickCP2.Display.println(cmsg);
#endif

    int btn1, btn1Prev;
    btn1 = btn1Prev = 0;
    keyMode = e_keyModeNone;
    while(1) {
        StickCP2.update();                // 本体ボタン状態更新
        btn1 = StickCP2.BtnA.isPressed();
        if( btn1 != btn1Prev || keyMode == e_keyModeEnter ) {
            keyMode = e_keyModeNone;
            break;
        }
        delay(100);
        btn1Prev = btn1;
    }
}

void drawImage(std::vector<uint32_t> v) {
    //const int dx = 78;
    //const int dy = 35;
    const int dx = 100;
    const int dy = 2;
    // 表示（メモリ描画領域）
    for(int i = 0 ; i  < vcolorIdx.size() ; i++ ) {
        int x = i % 64;
        int y = i / 64;
        //canvas.drawPixel(dx+x, dy+y, vcolorIdx[i]);
        canvas.fillRect(dx+x*2, dy+y*2, 2, 2, vcolorIdx[i]); //枠だけ left, top, witdh, height
    }

    // メモリ描画領域を座標を指定して一括表示（スプライト）
    canvas.pushSprite(&StickCP2.Display, 0, 0); 
}

bool bFirst = true;
int iMenuIdx = 0;
void loop() {
    StickCP2.update();                // 本体ボタン状態更新
    btn1_cur_value = StickCP2.BtnA.isPressed();
    bool bShot = false;
    if( (btn1_cur_value != btn1_last_value) || bFirst || ( keyMode > e_keyModeNone && keyMode < e_keyModeMax ) ) {
        if(btn1_cur_value == 0 || bFirst || ( keyMode > e_keyModeNone && keyMode < e_keyModeMax ) ){
            if(!bFirst) {
                if( btn1_cur_value != btn1_last_value ) {
                    bShot = true;
                }
                if( keyMode > e_keyModeNone && keyMode < e_keyModeMax ) {
                    if( keyMode == e_keyModeShot ) {
                        bShot = true;
                    } else if( keyMode == e_keyModeEnter ) {
                        iMenuIdx = (iMenuIdx + 1) % 4;
                    } else if( keyMode == e_keyModeLeft ) {
                        iMenuIdx = (iMenuIdx + 4 - 1) % 4;
                    } else if( keyMode == e_keyModeRight ) {
                        iMenuIdx = (iMenuIdx + 1) % 4;
                    } else if( keyMode == e_keyModeUp ||
                                keyMode == e_keyModeDown ||
                                keyMode == e_keyModeGetPalette
                    ) {
                        if( keyMode == e_keyModeUp ) {
                            g_palletIdx = (g_palletIdx + 1) % 4;
                        } else if( keyMode == e_keyModeDown ) {
                            g_palletIdx = (g_palletIdx + 4 - 1) % 4;
                        } else if( keyMode == e_keyModeGetPalette ) {
                            getPallete(g_palletIdx);
                            drawImage(vcolorIdx);
                        }
                        keyMode = e_keyModeNone;
                        return;
                    }  else if( keyMode == e_keyModeReset ) {
                        ESP.restart();
                    }
                    keyMode = e_keyModeNone;
                }
            }
            vcolorIdx.clear();
            StickCP2.Display.clear();
            StickCP2.Display.setCursor(0, 0, 1);
            uint32_t startTime, EndTime;
            startTime = millis();
            if( bShot == true ) {
                String s = String('s');
                StickCP2.Display.print("Shot");
                vcolorIdx = Communication32(s, "Shot");
            } else {
                String s = String(iMenuIdx);
                StickCP2.Display.print("ICON");
                vcolorIdx = Communication32(s, "ICON");
            }

            if( vcolorIdx.size() > 4096 ) {
                vcolorIdx.resize(4096);
            }

            EndTime = millis();
            double ProcTime = ((EndTime - startTime) / 1000.0);
            char cTime[16] = {'\0'};
            sprintf(cTime, "(%.2fsec)", ProcTime);

            StickCP2.Display.print(vcolorIdx.size());
            StickCP2.Display.print(cTime);
            StickCP2.Display.println("");

            // メモリ内に描画した画面を一括出力（チラツキなし）-------------------------------------------------------
            canvas.fillSprite(BLACK);
            //canvas.setTextColor(WHITE);
            int cIdx = canvas.getPaletteIndex(WHITE);
            canvas.setTextColor(cIdx);
            canvas.setCursor(0, 0, 1);
            canvas.println("ICON recieved");
            canvas.print(vcolorIdx.size());
            canvas.println(cTime);

            drawImage(vcolorIdx);
        }
        if(!bFirst) {
            btn1_last_value = btn1_cur_value;
            keyMode = e_keyModeNone;
        }
        bFirst = false;
    }
    delay(100);
}
