#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Adafruit_PWMServoDriver.h>
#include <LittleFS.h>
#include <ArduinoJson.h>


#include "web_ui.h"

// --------- 分片保存状态（chunk save）---------
File gSaveFile;
bool gSaving = false;
int gSaveCountExpected = 0;
int gSaveFramesWritten = 0;
String gSaveName;

String sanitizeName(const String& in);
String actPath(const String& name);

void saveAbortCleanup(){
  if(gSaveFile) gSaveFile.close();
  gSaving = false;
  gSaveCountExpected = 0;
  gSaveFramesWritten = 0;
  gSaveName = "";
}

bool saveBegin(const String& name, int count){
  saveAbortCleanup();

  gSaveName = name;
  gSaveCountExpected = count;
  gSaveFramesWritten = 0;

  String path = actPath(name);
  gSaveFile = LittleFS.open(path, "w");   // ✅ 同名直接覆盖
  if(!gSaveFile){
    saveAbortCleanup();
    return false;
  }

  String safe = sanitizeName(name);
  gSaveFile.print("{\"ver\":1,\"name\":\"");
  gSaveFile.print(safe);
  gSaveFile.print("\",\"frames\":[");
  gSaving = true;
  return true;
}

bool saveWriteFrame(int idx, const int p[6], int hold){
  if(!gSaving || !gSaveFile) return false;

  (void)idx; // 目前不强制按idx顺序写入

  if(gSaveFramesWritten > 0) gSaveFile.print(',');

  gSaveFile.print("{\"p\":[");
  for(int i=0;i<6;i++){
    if(i) gSaveFile.print(',');
    gSaveFile.print(p[i]);
  }
  gSaveFile.print("],\"hold\":");
  gSaveFile.print(hold);
  gSaveFile.print("}");

  gSaveFramesWritten++;
  return true;
}

bool saveEnd(){
  if(!gSaving || !gSaveFile) return false;

  gSaveFile.print("]}");
  gSaveFile.close();

  bool ok = (gSaveFramesWritten > 0);
  saveAbortCleanup();
  return ok;
}

// ------------------- PCA9685 -------------------
Adafruit_PWMServoDriver pca(0x40);
const int SERVO_FREQ = 50;

static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline int usToTicks(int us){ return (int)(us * 4096L / 20000L); } // 50Hz -> 20ms

// ------------------- 机械臂标定 -------------------
struct Joint {
  uint8_t ch;
  int min_us, max_us;
  int dir;            // +1/-1
  float offset;       // 度
  float min_deg, max_deg;
  float cur_deg;
  float tgt_deg;
};

// 你的当前标定（已放宽脉宽）
Joint j[6] = {
  {0, 700, 2300, -1,  0,  0, 180, 90, 90},  // S1
  {1, 700, 2300, +1, +10, 0, 180, 90, 90},  // S2
  {2, 700, 2300, +1, +10, 0, 180, 90, 90},  // S3
  {3, 700, 2300, -1, +15, 0, 180, 90, 90},  // S4
  {4, 700, 2300, +1, +10, 0, 180, 90, 90},  // S5
  {5, 700, 2300, +1,  0, 50, 150, 90, 90},  // S6（大开150，小合50）
};

// 关键：围绕90翻转的dir模型
int degToUs(int idx, float deg_logic){
  Joint &c = j[idx];
  float deg = clampf(deg_logic, c.min_deg, c.max_deg);

  float cmd = 90.0f + c.dir * (deg - 90.0f) + c.offset;
  cmd = clampf(cmd, 0, 180);

  return c.min_us + (int)((c.max_us - c.min_us) * (cmd / 180.0f));
}

void writeJointNow(int idx, float deg_logic){
  int us = degToUs(idx, deg_logic);
  pca.setPWM(j[idx].ch, 0, usToTicks(us));
}

void setTarget(int idx, float deg){
  j[idx].tgt_deg = clampf(deg, j[idx].min_deg, j[idx].max_deg);
}

void setPoseTargets(const float p[6]){
  for(int i=0;i<6;i++) setTarget(i, p[i]);
}
static const float BOOT_POSE[6] = {88, 0, 180, 180, 90, 90};

void homePose(){
  float p[6] = {88,0,180,180,90,90};
  setPoseTargets(p);
}

void readyPose(){
  float p[6] = {90,110,110,90,90,90};
  setPoseTargets(p);
}

// ------------------- 非阻塞舵机更新（50Hz） -------------------
const uint32_t SERVO_PERIOD_MS = 20;
uint32_t lastServoMs = 0;
const float STEP_DEG = 2.0f;

void servoUpdateStep(){
  uint32_t now = millis();
  if(now - lastServoMs < SERVO_PERIOD_MS) return;
  lastServoMs = now;

  for(int i=0;i<6;i++){
    float cur = j[i].cur_deg;
    float tgt = clampf(j[i].tgt_deg, j[i].min_deg, j[i].max_deg);

    float next = cur;
    if(fabsf(tgt - cur) <= STEP_DEG) next = tgt;
    else next = cur + (tgt > cur ? STEP_DEG : -STEP_DEG);

    j[i].cur_deg = next;
    writeJointNow(i, next);
  }
}

bool poseNear(float eps=2.0f){
  for(int i=0;i<6;i++){
    if(fabsf(j[i].cur_deg - j[i].tgt_deg) > eps) return false;
  }
  return true;
}

// ------------------- Action 存储格式 -------------------
String sanitizeName(const String& in){
  String out;
  out.reserve(in.length());
  for(size_t i=0;i<in.length();i++){
    char c = in[i];
    if( (c>='0'&&c<='9') || (c>='A'&&c<='Z') || (c>='a'&&c<='z') || c=='_' || c=='-' ){
      out += c;
    }
  }
  if(out.length()==0) out = "noname";
  return out;
}

String actPath(const String& name){
  return "/" + String("act_") + sanitizeName(name) + ".json";
}

// 播放器
struct Frame {
  int p[6];
  uint16_t hold;
};

const int MAX_FRAMES = 220;
Frame playFrames[MAX_FRAMES];
int playCount = 0;
int playIdx = 0;
bool playing = false;
uint32_t frameStartMs = 0;

void playStop(){
  playing = false;
}

bool loadActionToPlayer(const String& name){
  String path = actPath(name);
  File f = LittleFS.open(path, "r");
  if(!f) return false;

  StaticJsonDocument<16384> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if(err) return false;

  JsonArray frames = doc["frames"].as<JsonArray>();
  if(frames.isNull()) return false;

  int n = 0;
  for(JsonObject fr : frames){
    if(n >= MAX_FRAMES) break;
    JsonArray p = fr["p"].as<JsonArray>();
    if(p.size() != 6) continue;
    for(int i=0;i<6;i++) playFrames[n].p[i] = (int)p[i];
    playFrames[n].hold = (uint16_t)(fr["hold"] | 0);
    n++;
  }
  playCount = n;
  return playCount > 0;
}

void playStartLoaded(){
  if(playCount <= 0) return;
  playing = true;
  playIdx = 0;
  frameStartMs = millis();

  float p[6];
  for(int i=0;i<6;i++) p[i] = playFrames[0].p[i];
  setPoseTargets(p);
}

void playUpdate(){
  if(!playing) return;
  if(playIdx >= playCount){
    playing = false;
    return;
  }

  uint32_t now = millis();
  uint16_t hold = playFrames[playIdx].hold;

  if((now - frameStartMs) >= hold && poseNear(3.0f)){
    playIdx++;
    if(playIdx >= playCount){
      playing = false;
      return;
    }
    frameStartMs = now;
    float p[6];
    for(int i=0;i<6;i++) p[i] = playFrames[playIdx].p[i];
    setPoseTargets(p);
  }
}



// ------------------- WiFi + Web -------------------
const char* AP_SSID = "ESP32_ARM";
const char* AP_PASS = "12345678";

WebServer server(80);
WebSocketsServer ws(81);

void wsBroadcastJson(const JsonDocument& doc){
  String s;
  serializeJson(doc, s);
  ws.broadcastTXT(s);
}

void sendPose(){
  StaticJsonDocument<256> doc;
  doc["type"] = "pose";
  JsonArray p = doc.createNestedArray("p");
  for(int i=0;i<6;i++) p.add((int)lroundf(j[i].cur_deg));
  wsBroadcastJson(doc);
}

void sendStatus(const char* msg){
  StaticJsonDocument<256> doc;
  doc["type"]="status";
  doc["msg"]=msg;
  wsBroadcastJson(doc);
}

void sendPlayState(){
  StaticJsonDocument<128> doc;
  doc["type"]="play";
  doc["state"]= playing ? "PLAYING" : "IDLE";
  wsBroadcastJson(doc);
}

void sendActList(){
  StaticJsonDocument<4096> d;
  d["type"] = "act_list";
  JsonArray arr = d.createNestedArray("list");

  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while(f){
    String n = f.name();
    if(n.startsWith("/")) n.remove(0,1);

    if(n.startsWith("act_") && n.endsWith(".json")){
      String base = n.substring(4, n.length()-5);
      arr.add(base);
    }
    f = root.openNextFile();
  }

  String out;
  serializeJson(d, out);
  ws.broadcastTXT(out);
}

void sendActToEditor(const String& name){
  String path = actPath(name);
  File f = LittleFS.open(path, "r");
  if(!f){ sendStatus("load failed"); return; }

  StaticJsonDocument<16384> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if(err){ sendStatus("load json failed"); return; }

  StaticJsonDocument<16384> out;
  out["type"]="act";
  out["name"]=name;
  out["frames"]=doc["frames"];
  wsBroadcastJson(out);
}

bool deleteActionFile(const String& name){
  String path = actPath(name);
  if(!LittleFS.exists(path)) return false;
  return LittleFS.remove(path);
}
// ------------------- Jog / 点动（末端感联动，不用IK） -------------------
void jogApply(const char* axis, int dir, int stepDeg){
  if(dir==0) return;
  if(stepDeg<=0) stepDeg = 2;

  // 统一：dir=+1 表示“正方向”，dir=-1 表示“负方向”
  // 你可以按手感改联动的符号（非常正常）
  auto jogOne = [&](int idx, int delta){
    setTarget(idx, (int)lroundf(j[idx].tgt_deg + delta));
  };

  if(strcmp(axis,"yaw")==0){
    // 左右转底座
    jogOne(0, dir * stepDeg);
    return;
  }

  if(strcmp(axis,"reach")==0){
    // 前后“伸缩”：肩+肘同向（更伸/更缩）
    jogOne(1, dir * stepDeg);
    jogOne(2, dir * stepDeg);
    return;
  }

  if(strcmp(axis,"lift")==0){
    // 上下“抬落”：肩和肘反向（更抬/更压）
    // 这里定义：dir=+1 -> 上抬
    jogOne(1, -dir * stepDeg);
    jogOne(2, +dir * stepDeg);
    return;
  }

  if(strcmp(axis,"pitch")==0){
    // 手腕俯仰
    jogOne(3, dir * stepDeg);
    return;
  }

  if(strcmp(axis,"roll")==0){
    // 手腕滚转
    jogOne(4, dir * stepDeg);
    return;
  }

  if(strcmp(axis,"grip")==0){
    // 夹爪开合（你的 S6 范围 50..150）
    jogOne(5, dir * stepDeg);
    return;
  }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len){
  (void)num;
  if(type != WStype_TEXT) return;

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if(err){
    Serial.printf("[WS] json err: %s\n", err.c_str());
    return;
  }

  const char* cmd = doc["cmd"] | "";
  if(!cmd || !cmd[0]) return;

  // ---------------- 基础控制 ----------------
  if(strcmp(cmd,"get")==0){
    sendPose();
    sendPlayState();
    return;
  }
  if(strcmp(cmd,"home")==0){ homePose(); return; }
  if(strcmp(cmd,"ready")==0){ readyPose(); return; }

  if(strcmp(cmd,"j")==0){
    int idx = doc["i"] | -1;
    int deg = doc["deg"] | 90;
    if(idx>=0 && idx<6) setTarget(idx, deg);
    return;
  }

  if(strcmp(cmd,"p")==0){
    JsonArray p = doc["p"].as<JsonArray>();
    if(p.size()==6){
      float pp[6];
      for(int i=0;i<6;i++) pp[i] = (float)(p[i] | 90);
      setPoseTargets(pp);
    }
    return;
  }



  // ---------------- 动作库：list / load / delete / run / stop ----------------
  if(strcmp(cmd,"act_list")==0){
    sendActList();
    return;
  }

  if(strcmp(cmd,"act_load")==0){
    String name = (const char*)(doc["name"] | "");
    Serial.printf("[ACT] load '%s'\n", name.c_str());
    sendActToEditor(name);
    return;
  }

  if(strcmp(cmd,"act_delete")==0){
    String name = (const char*)(doc["name"] | "");
    Serial.printf("[ACT] delete '%s'\n", name.c_str());
    bool ok = deleteActionFile(name);
    sendStatus(ok ? "deleted" : "delete failed");
    sendActList();
    return;
  }

  if(strcmp(cmd,"act_run")==0){
    String name = (const char*)(doc["name"] | "");
    Serial.printf("[ACT] run '%s'\n", name.c_str());
    playStop();
    bool ok = loadActionToPlayer(name);
    if(ok){
      playStartLoaded();
      sendStatus("playing");
      sendPlayState();
    }else{
      sendStatus("run failed");
    }
    return;
  }

  if(strcmp(cmd,"act_stop")==0){
    Serial.println("[ACT] stop");
    playStop();
    sendStatus("stopped");
    sendPlayState();
    return;
  }

  // ---------------- ✅ 分片保存 ----------------
  if(strcmp(cmd,"act_save_begin")==0){
    String name = (const char*)(doc["name"] | "");
    int count = doc["count"] | 0;

    Serial.printf("[SAVE] begin name='%s' count=%d\n", name.c_str(), count);
    bool ok = saveBegin(name, count);
    Serial.println(ok ? "[SAVE] begin OK" : "[SAVE] begin FAIL");

    sendStatus(ok ? "save_begin ok" : "save_begin failed");
    return;
  }

  if(strcmp(cmd,"act_save_frame")==0){
    int idx  = doc["idx"] | gSaveFramesWritten;
    int hold = doc["hold"] | 0;

    JsonArray p = doc["p"].as<JsonArray>();
    if(p.size()!=6){
      Serial.println("[SAVE] frame bad p");
      sendStatus("save_frame bad p");
      return;
    }

    int pp[6];
    for(int i=0;i<6;i++) pp[i] = (int)(p[i] | 90);

    bool ok = saveWriteFrame(idx, pp, hold);
    if(!ok){
      Serial.printf("[SAVE] frame FAIL idx=%d\n", idx);
      sendStatus("save_frame failed");
    }
    return;
  }

  if(strcmp(cmd,"act_save_end")==0){
    String name = (const char*)(doc["name"] | gSaveName.c_str());

    Serial.printf("[SAVE] end name='%s' written=%d\n", name.c_str(), gSaveFramesWritten);
    bool ok = saveEnd();
    Serial.println(ok ? "[SAVE] end OK" : "[SAVE] end FAIL");

    Serial.println("[FS] list:");
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while(f){
      Serial.printf(" - %s (%u)\n", f.name(), (unsigned)f.size());
      f = root.openNextFile();
    }

    sendStatus(ok ? "saved" : "save_end failed");
    sendActList();
    return;
  }
  // ---------------- Jog：点动控制 ----------------
  // JSON: {"cmd":"jog","a":"yaw","dir":1,"step":2}
  if(strcmp(cmd,"jog")==0){
    const char* a = doc["a"] | "";
    int dir  = doc["dir"] | 0;     // +1/-1
    int step = doc["step"] | 2;    // 每次点动多少度
    playStop();                    // 避免动作播放抢控制
    jogApply(a, dir, step);
    return;
  }

  Serial.printf("[WS] unknown cmd: %s\n", cmd);
}

// ------------------- 串口控制（加了 ik 命令） -------------------
void serialLoop(){
  static String line;
  while(Serial.available()){
    char c = (char)Serial.read();
    if(c=='\n'){
      line.trim();
      if(line.length()){
        Serial.print("[RX] "); Serial.println(line);

        if(line=="home"){
          homePose();
          Serial.println("[OK] homePose");
        }else if(line=="ready"){
          readyPose();
          Serial.println("[OK] readyPose");
        }else{
          int sp = line.indexOf(' ');
          if(sp>0){
            int idx = line.substring(0, sp).toInt();
            int deg = line.substring(sp+1).toInt();
            if(idx>=0 && idx<6){
              setTarget(idx, deg);
              Serial.printf("[OK] S%d=%d\n", idx+1, deg);
            }else{
              Serial.println("[ERR] idx 0-5");
            }
          }else{
            Serial.println("[ERR] use: home | ready | <idx 0-5> <deg>");
          }
        }
      }
      line="";
    }else if(c!='\r'){
      line += c;
    }
  }
}


void setup(){
  Serial.begin(115200);

  Wire.begin(21,22);

  pca.begin();
  // 如果你发现“所有舵机整体角度偏小/偏大”，可以试试校准振荡器：
  // pca.setOscillatorFrequency(25000000); // 或 27000000
  pca.setPWMFreq(SERVO_FREQ);
  delay(10);

  // FS
  if(!LittleFS.begin(true)){
    Serial.println("[FS] LittleFS mount FAIL");
  }else{
    Serial.println("[FS] LittleFS mount OK");
    Serial.printf("[FS] total=%u used=%u\n", (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());

    File t = LittleFS.open("/_fs_test.txt", "w");
    if(!t) Serial.println("[FS] test open FAIL");
    else { t.println("ok"); t.close(); Serial.println("[FS] test write OK"); }
  }

// 上电直接进入指定初始姿态（你要的那组）
for(int i=0;i<6;i++){
  j[i].cur_deg = BOOT_POSE[i];
  j[i].tgt_deg = BOOT_POSE[i];
  writeJointNow(i, BOOT_POSE[i]);
}


  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", [](){ server.send_P(200,"text/html",WEB_UI); });
  server.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  Serial.println("Open http://192.168.4.1");
}

void loop(){
  server.handleClient();
  ws.loop();

  servoUpdateStep();
  playUpdate();

  // 心跳：每2秒打一次，证明 loop 在跑
  static uint32_t t=0;
  if(millis()-t > 2000){
    t = millis();
    Serial.printf("[HB] ms=%lu heap=%u used=%u/%u\n",
      (unsigned long)millis(),
      (unsigned)ESP.getFreeHeap(),
      (unsigned)LittleFS.usedBytes(),
      (unsigned)LittleFS.totalBytes()
    );
  }

  serialLoop();
}


