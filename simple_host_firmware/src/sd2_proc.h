#pragma once
// ============================================================
//  SD2-OS: Process Manager (cooperative multi-tasking)
// ============================================================

namespace ProcMgr {

inline int findFreeSlot() {
  for (int i = 0; i < MAX_PROCS; i++) { if (!procs[i].running) return i; }
  return -1;
}

int spawn(const String& name, const String& binPath, uint8_t prio = PRIO_NORM) {
  int slot = findFreeSlot();
  if (slot < 0) return -1;
  if (!LittleFS.exists(binPath)) return -1;
  File f = LittleFS.open(binPath, "r");
  if (!f) return -1;
  uint8_t hdr[10];
  if (f.read(hdr, 10) != 10) { f.close(); return -1; }
  uint16_t size = (hdr[8] << 8) | hdr[9];
  if (size > 2048) { f.close(); return -1; }
  uint8_t* code = new uint8_t[size];
  f.seek(0);
  if (f.read(code, size) != size) { delete[] code; f.close(); return -1; }
  f.close();

  procs[slot].id = slot + 1;
  procs[slot].code = code; procs[slot].codeSize = size;
  procs[slot].ip = 10; procs[slot].loopTarget = 10;
  procs[slot].loopCount = 0; procs[slot].loopDepth = -1;
  procs[slot].running = true; procs[slot].waitUntil = 0;
  procs[slot].state = READY; procs[slot].priority = prio;
  procs[slot].sigPending = 0; procs[slot].sigHandler = 0;
  procs[slot].name = name;
  if (slot >= procCount) procCount = slot + 1;
  Log::info("PROC spawn " + name + " pid=" + String(slot+1) + " prio=" + String(prio));
  return slot + 1;
}

inline bool kill(int pid) {
  if (pid < 1 || pid > MAX_PROCS) return false;
  int idx = pid - 1;
  if (!procs[idx].running) return false;
  delete[] procs[idx].code; procs[idx].code = nullptr;
  procs[idx].running = false; procs[idx].name = "";
  while (procCount > 0 && !procs[procCount-1].running) procCount--;
  Log::info("PROC kill pid=" + String(pid));
  return true;
}

inline void killAll() {
  for (int i=0;i<MAX_PROCS;i++) { if(procs[i].running){delete[] procs[i].code;procs[i].code=nullptr;procs[i].running=false;} }
  procCount=0;
}

inline bool signal(int pid, uint8_t sig) {
  if (pid<1||pid>MAX_PROCS) return false;
  Proc& p=procs[pid-1];
  if(!p.running) return false;
  p.sigPending |= (1<<sig);
  if(p.state==BLOCKED_SIG){p.state=READY;p.ip=p.savedIp;}
  return true;
}

String listJson() {
  String json="{\"ok\":true,\"procs\":[";
  bool first=true;
  const char* stateNames[]={"READY","WAIT","SIGBLK"};
  const char* prioNames[]={"HIGH","NORM","LOW","IDLE"};
  for(int i=0;i<MAX_PROCS;i++){
    if(!procs[i].running)continue;
    if(!first)json+=","; first=false;
    json+="{\"pid\":"+String(i+1)+",\"name\":\""+procs[i].name+"\"";
    json+=",\"state\":\""+String(stateNames[procs[i].state])+"\"";
    json+=",\"prio\":\""+String(prioNames[procs[i].priority])+"\"";
    json+=",\"ip\":"+String(procs[i].ip);
    if(procs[i].waitUntil)json+=",\"sleep\":"+String(procs[i].waitUntil-millis());
    if(procs[i].sigPending)json+=",\"sig\":"+String(procs[i].sigPending);
    json+="}";
  }
  json+="]}"; return json;
}

// ---- Timers ----
inline int timerAdd(uint32_t ms, const String& cmd, bool periodic) {
  for(int i=0;i<MAX_TIMERS;i++){
    if(!timers[i].active){
      timers[i].nextFire=millis()+ms; timers[i].period=periodic?ms:0;
      timers[i].command=cmd; timers[i].active=true;
      Log::info("TIMER add "+String(ms)+"ms "+cmd);
      return i;
    }
  }
  return -1;
}

inline void timerPoll() {
  uint32_t now=millis();
  for(int i=0;i<MAX_TIMERS;i++){
    if(!timers[i].active)continue;
    if((int32_t)(now-timers[i].nextFire)>=0){
      runHostCommand(timers[i].command, false);
      if(timers[i].period){timers[i].nextFire=now+timers[i].period;}
      else {timers[i].active=false;}
    }
  }
}

String timerList() {
  String s="TIMERS:";
  for(int i=0;i<MAX_TIMERS;i++){
    if(timers[i].active){s+="\n  #"+String(i)+" in "+
      String((int32_t)(timers[i].nextFire-millis()))+"ms: "+timers[i].command+
      (timers[i].period?" [every "+String(timers[i].period)+"ms]":"");}
  }
  return s;
}

// ---- Hooks ----
inline int hookAdd(const String& event, const String& cmd) {
  for(int i=0;i<MAX_HOOKS;i++){
    if(!hooks[i].active){hooks[i].event=event;hooks[i].command=cmd;hooks[i].active=true;return i;}
  }
  return -1;
}

inline void hookFire(const String& event) {
  for(int i=0;i<MAX_HOOKS;i++){
    if(hooks[i].active && hooks[i].event==event){
      Log::info("HOOK fire "+event+": "+hooks[i].command);
      runHostCommand(hooks[i].command, false);
    }
  }
}

String hookList() {
  String s="HOOKS:";
  for(int i=0;i<MAX_HOOKS;i++){
    if(hooks[i].active){s+="\n  "+hooks[i].event+" -> "+hooks[i].command;}
  }
  return s;
}

// Priority-based step: one opcode of highest-priority ready process
inline void step(int idx) {
  Proc& p=procs[idx];
  if(!p.running||!p.code)return;
  if(p.state==BLOCKED_SIG){
    if(p.sigPending){p.state=READY;p.ip=p.savedIp;}
    else return;
  }
  if(p.ip>=p.codeSize){kill(p.id);return;}

  uint8_t op=p.code[p.ip++];
  switch(op){
    case ScriptOp::NOP: break;
    case ScriptOp::SHOW_BUILTIN: {const char* n[]={"logo","face","bars","status"};uint8_t id=p.code[p.ip++];showImage(id<4?n[id]:"logo");break;}
    case ScriptOp::SHOW_FILE: {String a((const char*)&p.code[p.ip]);p.ip+=a.length()+1;showStoredFile(a);break;}
    case ScriptOp::TEXT: {String a((const char*)&p.code[p.ip]);p.ip+=a.length()+1;drawTextMessage(a);break;}
    case ScriptOp::BEEP_OP: {uint16_t f=(p.code[p.ip]<<8)|p.code[p.ip+1],m=(p.code[p.ip+2]<<8)|p.code[p.ip+3];p.ip+=4;beep(f?f:2200,m?m:70);break;}
    case ScriptOp::BRIGHT_OP: setBacklightPercent(p.code[p.ip++]);break;
    case ScriptOp::WAIT: {uint16_t ms=(p.code[p.ip]<<8)|p.code[p.ip+1];p.ip+=2;p.waitUntil=millis()+ms;break;}
    case ScriptOp::ANIM_PLAY_OP: {String a((const char*)&p.code[p.ip]);p.ip+=a.length()+1;animStart(a);break;}
    case ScriptOp::ANIM_STOP_OP: animStop();break;
    case ScriptOp::LOOP: {uint16_t c=(p.code[p.ip]<<8)|p.code[p.ip+1];p.ip+=2;p.loopTarget=p.ip;p.loopDepth=0;p.loopCount=c;break;}
    case ScriptOp::END:
      if(p.loopDepth>=0&&(p.loopCount==0||p.loopCount>1)){if(p.loopCount>1)p.loopCount--;p.ip=p.loopTarget;}
      else kill(p.id);
      break;
    case ScriptOp::SIG_WAIT: p.state=BLOCKED_SIG; p.savedIp=p.ip; break;
    case ScriptOp::SIG_SEND: { uint8_t tpid=p.code[p.ip++]; uint8_t sig=p.code[p.ip++]; signal(tpid, sig); break; }
    case ScriptOp::YIELD: break;
    case ScriptOp::PRIORITY: p.priority=constrain((int)p.code[p.ip++],0,3); break;
    default: kill(p.id);break;
  }
}

inline void schedule() {
  timerPoll();
  for(int prio=0;prio<4;prio++){
    for(int i=0;i<procCount;i++){
      if(!procs[i].running)continue;
      if(procs[i].priority!=prio)continue;
      if(procs[i].waitUntil && millis()<procs[i].waitUntil)continue;
      if(procs[i].state==BLOCKED_SIG && !procs[i].sigPending)continue;
      step(i); yield(); return;
    }
  }
}

} // namespace ProcMgr
