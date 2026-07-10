#pragma once
// ============================================================
//  SD2-OS: LLM Middleware (OpenAI / Claude API bridge)
// ============================================================

namespace LLM {

constexpr int RESPONSE_TIMEOUT = 30000;
constexpr int MAX_RESPONSE    = 4096;

extern String   lastResponse;
extern uint32_t lastCallMs;

struct Provider {
  const char* name;
  const char* host;
  uint16_t    port;
  const char* path;
  const char* bodyTemplate;
};

const Provider OPENAI = {
  "openai", "api.openai.com", 443, "/v1/chat/completions",
  R"({"model":"{{MODEL}}","messages":[{"role":"system","content":"{{SYSTEM}}"},{"role":"user","content":"{{PROMPT}}"}]})"
};

const Provider CLAUDE = {
  "claude", "api.anthropic.com", 443, "/v1/messages",
  R"({"model":"{{MODEL}}","max_tokens":1024,"system":"{{SYSTEM}}","messages":[{"role":"user","content":"{{PROMPT}}"}]})"
};

String extractJSON(const String& json, const String& key) {
  String search = "\"" + key + "\"";
  int pos = json.indexOf(search);
  if (pos < 0) return "";
  pos += search.length();
  while (pos < (int)json.length() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\"')) pos++;
  if (pos >= (int)json.length()) return "";
  int end = pos;
  while (end < (int)json.length()) {
    if (json[end] == '\\') { end += 2; continue; }
    if (json[end] == '\"') break;
    end++;
  }
  return json.substring(pos, end);
}

String extractContent(const String& json) {
  String text = extractJSON(json, "text");
  if (text.length() > 0) return text;
  text = extractJSON(json, "content");
  return text;
}

String buildBody(const Provider& p, const String& model, const String& sys, const String& prompt) {
  String body = p.bodyTemplate;
  body.replace("{{MODEL}}", model);
  body.replace("{{SYSTEM}}", sys);
  body.replace("{{PROMPT}}", prompt);
  return body;
}

String ask(const String& prompt) {
  String provider = Env::get("LLM_PROVIDER");
  String apiKey   = Env::get("LLM_API_KEY");
  String model    = Env::get("LLM_MODEL");
  String sys      = Env::get("LLM_SYSTEM");

  if (!apiKey.length()) return "ERR: LLM_API_KEY not set. Use SET LLM_API_KEY=sk-...";
  if (!model.length()) model = "gpt-4o";

  const Provider* p = &OPENAI;
  if (provider == "claude" || provider == "anthropic") p = &CLAUDE;

  Log::info("LLM asking " + provider + "/" + model);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(RESPONSE_TIMEOUT);

  if (!client.connect(p->host, p->port)) {
    Log::error("LLM connect failed");
    return "ERR: cannot connect to " + String(p->host);
  }

  String body = buildBody(*p, model, sys.length() ? sys : "Be concise.", prompt);

  String req;
  req += "POST " + String(p->path) + " HTTP/1.1\r\n";
  req += "Host: " + String(p->host) + "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "x-api-key: " + apiKey + "\r\n";
  req += "Authorization: Bearer " + apiKey + "\r\n";
  if (p == &CLAUDE) {
    req += "anthropic-version: 2023-06-01\r\n";
  }
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "Connection: close\r\n";
  req += "\r\n";
  req += body;

  client.print(req);

  String response;
  uint32_t start = millis();
  while (client.connected() || client.available()) {
    if (client.available()) {
      String chunk = client.readString();
      response += chunk;
      if (response.length() > MAX_RESPONSE) break;
    }
    if (millis() - start > RESPONSE_TIMEOUT) break;
    yield();
  }
  client.stop();

  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart < 0) {
    Log::error("LLM bad response");
    return "ERR: bad response";
  }
  String respBody = response.substring(bodyStart + 4);

  if (respBody.indexOf("\"error\"") >= 0) {
    String err = extractJSON(respBody, "message");
    Log::error("LLM API error: " + err);
    return "ERR: " + err;
  }

  String content = extractContent(respBody);
  if (!content.length()) return "ERR: empty response";

  lastResponse = content;
  lastCallMs = millis();
  Log::info("LLM response " + String(content.length()) + " chars");
  return content;
}

inline String cache() { return lastResponse; }

} // namespace LLM
