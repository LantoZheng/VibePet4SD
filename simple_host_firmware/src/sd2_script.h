#pragma once
// ============================================================
//  SD2-OS: Script Engine (.sh text → .s2b bytecode → VM)
// ============================================================

namespace Script {

constexpr uint32_t MAGIC    = 0x53443242;
constexpr uint16_t MAX_CMDS = 128;

bool compile(const String& srcPath, const String& dstPath) {
  if (!LittleFS.exists(srcPath)) return false;
  File src = LittleFS.open(srcPath, "r");
  if (!src) return false;

  uint16_t cmdCount = 0;
  uint16_t dataSize = 10;
  String line;
  while (src.available() && cmdCount < MAX_CMDS) {
    line = src.readStringUntil('\n'); line.trim();
    if (!line.length() || line.startsWith("#")) continue;
    cmdCount++; dataSize++;
    int sp = line.indexOf(' ');
    String cmd = sp >= 0 ? line.substring(0, sp) : line;
    String arg = sp >= 0 ? line.substring(sp + 1) : "";
    cmd.toUpperCase(); arg.trim();
    if (cmd == "SHOW")           dataSize += 1;
    else if (cmd == "SHOW_FILE") dataSize += arg.length() + 1;
    else if (cmd == "TEXT")      dataSize += arg.length() + 1;
    else if (cmd == "BEEP")      dataSize += 4;
    else if (cmd == "BRIGHT")    dataSize += 1;
    else if (cmd == "WAIT")      dataSize += 2;
    else if (cmd == "ANIM_PLAY") dataSize += arg.length() + 1;
    else if (cmd == "ANIM_STOP" || cmd == "END") {}
    else if (cmd == "LOOP")      dataSize += 2;
    else if (cmd == "SIG_WAIT")  dataSize += 0; // new op: no args
    else if (cmd == "SIG_SEND")  dataSize += 2; // pid + sig
    else if (cmd == "YIELD")     dataSize += 0;
    else if (cmd == "PRIORITY")  dataSize += 1;
    else { src.close(); return false; }
  }
  src.close();

  File dst = LittleFS.open(dstPath, "w");
  if (!dst) return false;
  uint8_t hdr[10] = {0x53,0x44,0x32,0x42, 0x01,0x00,
    uint8_t(cmdCount>>8), uint8_t(cmdCount),
    uint8_t(dataSize>>8), uint8_t(dataSize)};
  dst.write(hdr, 10);

  src = LittleFS.open(srcPath, "r");
  while (src.available()) {
    line = src.readStringUntil('\n'); line.trim();
    if (!line.length() || line.startsWith("#")) continue;
    int sp = line.indexOf(' ');
    String cmd = sp >= 0 ? line.substring(0, sp) : line;
    String arg = sp >= 0 ? line.substring(sp + 1) : "";
    cmd.toUpperCase(); arg.trim();

    if (cmd == "SHOW") {
      uint8_t op = ScriptOp::SHOW_BUILTIN, id = 0;
      if (arg == "face") id=1; else if (arg == "bars") id=2; else if (arg == "status") id=3;
      dst.write(&op,1); dst.write(&id,1);
    } else if (cmd == "SHOW_FILE") {
      uint8_t op = ScriptOp::SHOW_FILE; dst.write(&op,1); dst.print(arg); dst.write('\0');
    } else if (cmd == "TEXT") {
      uint8_t op = ScriptOp::TEXT; dst.write(&op,1); dst.print(arg); dst.write('\0');
    } else if (cmd == "BEEP") {
      uint8_t op = ScriptOp::BEEP_OP; dst.write(&op,1);
      uint16_t f=2200, m=70;
      int s=arg.indexOf(' ');
      if(arg.length()){f=s>0?arg.substring(0,s).toInt():arg.toInt();m=s>0?arg.substring(s+1).toInt():70;}
      uint8_t b[4]={uint8_t(f>>8),uint8_t(f),uint8_t(m>>8),uint8_t(m)}; dst.write(b,4);
    } else if (cmd == "BRIGHT") {
      uint8_t op=ScriptOp::BRIGHT_OP, v=arg.length()?constrain(arg.toInt(),0,100):85;
      dst.write(&op,1); dst.write(&v,1);
    } else if (cmd == "WAIT") {
      uint8_t op=ScriptOp::WAIT; uint16_t ms=arg.length()?arg.toInt():1000;
      dst.write(&op,1); dst.write(uint8_t(ms>>8)); dst.write(uint8_t(ms));
    } else if (cmd == "ANIM_PLAY") {
      uint8_t op=ScriptOp::ANIM_PLAY_OP; dst.write(&op,1); dst.print(arg); dst.write('\0');
    } else if (cmd == "ANIM_STOP") {
      uint8_t op=ScriptOp::ANIM_STOP_OP; dst.write(&op,1);
    } else if (cmd == "LOOP") {
      uint8_t op=ScriptOp::LOOP; uint16_t c=arg.length()?arg.toInt():0;
      dst.write(&op,1); dst.write(uint8_t(c>>8)); dst.write(uint8_t(c));
    } else if (cmd == "END") {
      uint8_t op=ScriptOp::END; dst.write(&op,1);
    } else if (cmd == "SIG_WAIT") {
      uint8_t op=ScriptOp::SIG_WAIT; dst.write(&op,1);
    } else if (cmd == "SIG_SEND") {
      uint8_t op=ScriptOp::SIG_SEND; dst.write(&op,1);
      int s2=arg.indexOf(' '); uint8_t pid=s2>0?arg.substring(0,s2).toInt():1;
      uint8_t sig=s2>0?arg.substring(s2+1).toInt():0;
      dst.write(&pid,1); dst.write(&sig,1);
    } else if (cmd == "YIELD") {
      uint8_t op=ScriptOp::YIELD; dst.write(&op,1);
    } else if (cmd == "PRIORITY") {
      uint8_t op=ScriptOp::PRIORITY, p=arg.length()?constrain(arg.toInt(),0,3):1;
      dst.write(&op,1); dst.write(&p,1);
    }
  }
  src.close(); dst.close();
  return true;
}

String run(const String& path) {
  uint8_t* code;
  uint16_t totalSize;

  if (scriptCache.code && scriptCache.name == path) {
    code = scriptCache.code;
    totalSize = scriptCache.size;
  } else {
    if (!scriptCache.load(path)) return "ERR script not found or too large";
    code = scriptCache.code;
    totalSize = scriptCache.size;
  }

  uint16_t ip=10, loopTarget=10;
  uint16_t loopCounts[16]={};
  int loopDepth=-1;

  while (ip<totalSize) {
    uint8_t op=code[ip++];
    switch(op){
      case ScriptOp::NOP: break;
      case ScriptOp::SHOW_BUILTIN: {
        const char* names[]={"logo","face","bars","status"};
        uint8_t id=code[ip++]; showImage(id<4?names[id]:"logo"); break;
      }
      case ScriptOp::SHOW_FILE: {
        String a((const char*)&code[ip]); ip+=a.length()+1; showStoredFile(a); break;
      }
      case ScriptOp::TEXT: {
        String a((const char*)&code[ip]); ip+=a.length()+1; drawTextMessage(a); break;
      }
      case ScriptOp::BEEP_OP: {
        uint16_t freq=(code[ip]<<8)|code[ip+1], ms=(code[ip+2]<<8)|code[ip+3]; ip+=4;
        beep(freq?freq:2200,ms?ms:70); break;
      }
      case ScriptOp::BRIGHT_OP: setBacklightPercent(code[ip++]); break;
      case ScriptOp::WAIT: {
        uint16_t ms=(code[ip]<<8)|code[ip+1]; ip+=2;
        uint32_t end=millis()+ms;
        while(millis()<end){yield();server.handleClient();webSocket.loop();}
        break;
      }
      case ScriptOp::ANIM_PLAY_OP: {
        String a((const char*)&code[ip]); ip+=a.length()+1; animStart(a); break;
      }
      case ScriptOp::ANIM_STOP_OP: animStop(); break;
      case ScriptOp::LOOP: {
        uint16_t count=(code[ip]<<8)|code[ip+1]; ip+=2;
        loopTarget=ip; loopDepth=0; loopCounts[0]=count; break;
      }
      case ScriptOp::END:
        if(loopDepth>=0 && (loopCounts[0]==0 || loopCounts[0]>1)){
          if(loopCounts[0]>1) loopCounts[0]--;
          ip=loopTarget;
          while(anim.playing) animStop();
        } else { return "OK SCRIPT_DONE"; }
        break;
      default: return String("ERR bad opcode ")+op;
    }
    yield();
  }
  return "OK SCRIPT_DONE";
}

String listJson() {
  String json="{\"ok\":true,\"scripts\":[";
  if(!fsMounted){json+="]}";return json;}
  Dir dir=LittleFS.openDir("/");
  bool first=true;
  while(dir.next()){
    String n=dir.fileName();
    if(n.endsWith(".s2b")||n.endsWith(".sh")){
      if(!first)json+=","; first=false;
      json+="{\"name\":\""+jsonEscape(n)+"\",\"size\":"+String(dir.fileSize());
      json+=",\"type\":\""+String(n.endsWith(".s2b")?"binary":"source")+"\"}";
    }
  }
  json+="]}"; return json;
}

} // namespace Script
