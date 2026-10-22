// ESP32-S3 Offline Chat
// ESP32 menjadi Access Point WiFi + web server + WebSocket. Tidak butuh internet.
//  - Pengguna wajib mengisi nama sebelum bisa chat
//  - Nama harus unik selama sedang online (tidak boleh kembar)
//  - Nama pengirim dipasang oleh server, jadi tidak bisa dipalsukan lewat pesan
//  - Bisa kirim file (disimpan di flash FFat)
//
// Board  : ESP32S3 Dev Module, Flash 16MB, Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)"
// Library: ESP Async WebServer + Async TCP (organisasi ESP32Async)

#include <WiFi.h>
#include <DNSServer.h>
#include <FFat.h>
#include <ESPAsyncWebServer.h>

// ===================== Konfigurasi =====================
const char* AP_SSID = "ChatRoom-ESP32";
const char* AP_PASS = "";                 // kosong = terbuka. Isi minimal 8 karakter untuk WPA2.
const bool  CLEAR_FILES_ON_BOOT = false;  // true = hapus semua file kiriman setiap ESP32 menyala
#define MAX_USERS 12                      // WiFi ESP32 sendiri dibatasi 8 perangkat (lihat setup)
const int   HIST = 20;                    // jumlah pesan terakhir yang dikirim ke pengguna baru
// =======================================================

DNSServer dns;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct User {
  bool used;
  uint32_t id;
  String nick;
  String tok;      // token acak per browser, agar refresh tidak dianggap "nama dipakai"
};
User users[MAX_USERS];

String history[HIST];
int histCount = 0;
uint32_t seq = 0;   // nomor urut pesan, dipakai client untuk membuang pesan ganda

void addHistory(const String& m) {
  if (histCount < HIST) {
    history[histCount++] = m;
  } else {
    for (int i = 1; i < HIST; i++) history[i - 1] = history[i];
    history[HIST - 1] = m;
  }
}

bool isConnected(uint32_t id) {
  AsyncWebSocketClient* c = ws.client(id);
  return c && c->status() == WS_CONNECTED;
}

int slotById(uint32_t id) {
  for (int i = 0; i < MAX_USERS; i++)
    if (users[i].used && users[i].id == id) return i;
  return -1;
}

int freeSlot() {
  for (int i = 0; i < MAX_USERS; i++) {
    if (users[i].used && !isConnected(users[i].id)) users[i].used = false;  // bersihkan sisa koneksi putus
    if (!users[i].used) return i;
  }
  return -1;
}

// Hapus karakter berbahaya/kontrol dari nama
String cleanNick(String n) {
  n.trim();
  String out;
  for (size_t i = 0; i < n.length() && out.length() < 40; i++) {
    char c = n[i];
    if ((unsigned char)c < 32 || c == '|' || c == '<' || c == '>') continue;
    out += c;
  }
  out.trim();
  return out;
}

// Token hanya huruf/angka, maks 40 karakter
String cleanTok(const String& t) {
  String out;
  for (size_t i = 0; i < t.length() && out.length() < 40; i++)
    if (isalnum((unsigned char)t[i])) out += t[i];
  return out;
}

// Jika nama sedang online, kembalikan penulisan aslinya. Jika tidak, string kosong.
String canonicalNick(const String& n) {
  String c = cleanNick(n);
  if (c.length() == 0) return "";
  for (int i = 0; i < MAX_USERS; i++)
    if (users[i].used && isConnected(users[i].id) && users[i].nick.equalsIgnoreCase(c))
      return users[i].nick;
  return "";
}

// Kirim ke semua pengguna yang sudah masuk (punya nama)
void broadcast(const String& s) {
  for (int i = 0; i < MAX_USERS; i++)
    if (users[i].used && isConnected(users[i].id)) ws.text(users[i].id, s);
}

void broadcastUsers() {
  String s = "USERS|";
  bool first = true;
  for (int i = 0; i < MAX_USERS; i++) {
    if (users[i].used && isConnected(users[i].id)) {
      if (!first) s += ",";
      s += users[i].nick;
      first = false;
    }
  }
  broadcast(s);
}

// Hanya huruf, angka, titik, minus, underscore. Mencegah path berbahaya.
String safeName(String n, size_t maxLen = 40) {
  int slash = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (slash >= 0) n = n.substring(slash + 1);
  String out;
  for (size_t i = 0; i < n.length() && out.length() < maxLen; i++) {
    char c = n[i];
    if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') out += c;
    else out += '_';
  }
  if (out.length() == 0 || out[0] == '.') out = "file" + out;
  return out;
}

void clearFiles() {
  File root = FFat.open("/");
  File f = root.openNextFile();
  while (f) {
    String p = String("/") + f.name();
    f.close();
    FFat.remove(p);
    root = FFat.open("/");
    f = root.openNextFile();
  }
}

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="id"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Chat</title>
<style>
 *{box-sizing:border-box}
 body{font-family:sans-serif;margin:0;display:flex;flex-direction:column;height:100vh;height:100dvh;background:#eceff1}
 header{background:#0a7;color:#fff;padding:8px 12px;display:flex;justify-content:space-between;align-items:center;gap:8px}
 header b{font-size:16px}
 header small{display:block;font-size:12px;opacity:.9}
 header a{color:#fff;font-size:12px;text-decoration:underline;cursor:pointer}
 #users{background:#d7efe8;padding:4px 12px;font-size:12px;color:#064}
 #log{flex:1;overflow-y:auto;padding:10px}
 .m{max-width:85%;width:fit-content;background:#fff;border-radius:10px;padding:6px 10px;margin:6px 0;overflow-wrap:anywhere}
 .m.me{margin-left:auto;background:#d9fdd3}
 .n{font-size:12px;font-weight:bold;margin-bottom:2px}
 .sys{text-align:center;font-size:12px;color:#666;margin:6px 0}
 #bar{padding:0 10px;font-size:13px;color:#555;min-height:16px}
 form{display:flex;gap:6px;padding:8px;background:#ddd}
 input[type=text]{flex:1;padding:10px;font-size:16px;min-width:0}
 button{padding:10px 14px;font-size:16px}
 #login{position:fixed;inset:0;background:#0a7;display:flex;align-items:center;justify-content:center;padding:20px;z-index:10}
 #login .box{background:#fff;border-radius:12px;padding:20px;width:100%;max-width:320px}
 #login h3{margin:0 0 8px}
 #login p{margin:0 0 12px;font-size:14px;color:#444}
 #login input{width:100%;margin-bottom:8px}
 #login button{width:100%}
 #err{color:#c00;font-size:13px;min-height:18px;margin-bottom:6px}
</style></head><body>

<div id="login"><div class="box">
 <h3>Masuk ke chat</h3>
 <p>Isi nama kamu supaya yang lain tahu siapa yang mengirim pesan.</p>
 <input type="text" id="nm" maxlength="16" placeholder="Nama kamu" autocomplete="off">
 <div id="err"></div>
 <button id="go">Masuk</button>
</div></div>

<header>
 <div><b>ESP32 Chat Room</b><small id="who"></small></div>
 <div><span id="st">○ putus</span><br><a id="rename">ganti nama</a></div>
</header>
<div id="users"></div>
<div id="log"></div>
<div id="bar"></div>
<form id="f">
 <button type="button" id="att">📎</button>
 <input type="file" id="fi" hidden>
 <input type="text" id="t" autocomplete="off" placeholder="Ketik pesan...">
 <button>Kirim</button>
</form>

<script>
 const $ = id => document.getElementById(id);
 const log = $('log'), st = $('st'), bar = $('bar'), fi = $('fi'),
       login = $('login'), nm = $('nm'), err = $('err'), usersEl = $('users');
 const MAX = 5 * 1024 * 1024;     // batas ukuran file: 5 MB
 let ws, me = '', want = '', tok = '';
 const seen = new Set();          // nomor pesan yang sudah tampil, untuk buang duplikat
 try { want = localStorage.getItem('nick') || ''; tok = localStorage.getItem('tok') || ''; } catch (e) {}
 if (!tok) {
   tok = (Math.random().toString(36).slice(2) + Date.now().toString(36)).replace(/[^a-z0-9]/g, '');
   try { localStorage.setItem('tok', tok); } catch (e) {}
 }
 nm.value = want;

 function color(n){ let h = 0; for (const c of n) h = (h * 31 + c.charCodeAt(0)) % 360; return 'hsl(' + h + ',60%,35%)'; }
 function fmt(n){ return n > 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB'; }
 function showLogin(msg){ login.style.display = 'flex'; err.textContent = msg || ''; nm.focus(); }
 function add(el){ log.appendChild(el); log.scrollTop = log.scrollHeight; }

 function bubble(n, text, f){
   const d = document.createElement('div');
   d.className = 'm' + (n === me ? ' me' : '');
   const h = document.createElement('div');
   h.className = 'n';
   h.textContent = (n === me) ? 'Kamu' : n;
   h.style.color = color(n);
   d.appendChild(h);
   if (f) {
     d.appendChild(document.createTextNode('📎 '));
     const a = document.createElement('a');
     a.href = '/dl?name=' + encodeURIComponent(f[1]);
     a.setAttribute('download', f[2]);
     a.textContent = f[2] + ' (' + fmt(+f[3]) + ')';
     d.appendChild(a);
   } else {
     d.appendChild(document.createTextNode(text));   // textNode = aman dari XSS
   }
   add(d);
 }

 function connect(){
   ws = new WebSocket('ws://' + location.host + '/ws');
   ws.onopen = () => {
     st.textContent = '● online';
     if (want) ws.send('JOIN|' + want + '|' + tok); else showLogin();
   };
   ws.onclose = () => { st.textContent = '○ putus'; me = ''; setTimeout(connect, 1500); };
   ws.onmessage = e => {
     const s = e.data, i = s.indexOf('|'), t = s.slice(0, i), r = s.slice(i + 1);
     if (t === 'OK') {
       me = r; want = r;
       try { localStorage.setItem('nick', r); } catch (x) {}
       login.style.display = 'none';
       log.textContent = '';
       seen.clear();
       $('who').textContent = 'Masuk sebagai ' + r;
     } else if (t === 'ERR') {
       me = '';
       showLogin(r);
     } else if (t === 'USERS') {
       const a = r ? r.split(',') : [];
       usersEl.textContent = '👥 ' + a.length + ' online: ' + a.join(', ');
     } else if (t === 'SYS') {
       const d = document.createElement('div'); d.className = 'sys'; d.textContent = r; add(d);
     } else if (t === 'MSG') {
       const a = r.indexOf('|'), b = r.indexOf('|', a + 1);
       const id = r.slice(0, a);
       if (seen.has(id)) return;              // buang pesan ganda
       seen.add(id);
       bubble(r.slice(a + 1, b), r.slice(b + 1));
     } else if (t === 'FILE') {
       const p = r.split('|');                // no|nama|tersimpan|asli|ukuran
       if (p.length === 5 && !seen.has(p[0])) { seen.add(p[0]); bubble(p[1], null, p.slice(1)); }
     }
   };
 }
 connect();

 function join(){
   const v = nm.value.replace(/[|<>]/g, '').trim();
   if (!v) { err.textContent = 'Nama tidak boleh kosong'; return; }
   want = v; err.textContent = '';
   if (ws && ws.readyState === 1) ws.send('JOIN|' + v + '|' + tok);
 }
 $('go').onclick = join;
 nm.onkeydown = e => { if (e.key === 'Enter') join(); };
 $('rename').onclick = () => { try { localStorage.removeItem('nick'); } catch (e) {} location.reload(); };

 $('f').onsubmit = e => {
   e.preventDefault();
   const t = $('t'), v = t.value.trim();
   if (v && me && ws.readyState === 1) ws.send('MSG|' + v);
   t.value = '';
 };

 $('att').onclick = () => { if (!me) return showLogin(); fi.click(); };
 fi.onchange = () => {
   const f = fi.files[0];
   if (!f) return;
   if (f.size > MAX) { alert('File terlalu besar (maks 5 MB)'); fi.value = ''; return; }
   const fd = new FormData();
   fd.append('file', f, f.name);
   const x = new XMLHttpRequest();
   x.open('POST', '/upload?nick=' + encodeURIComponent(me));
   x.upload.onprogress = ev => {
     if (ev.lengthComputable) bar.textContent = 'Mengirim ' + f.name + ' ' + Math.round(ev.loaded * 100 / ev.total) + '%';
   };
   x.onload  = () => { bar.textContent = x.status === 200 ? '' : 'Gagal kirim file'; };
   x.onerror = () => { bar.textContent = 'Gagal kirim file'; };
   x.send(fd);
   fi.value = '';
 };
</script></body></html>
)rawliteral";

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type,
               void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_DISCONNECT) {
    int i = slotById(c->id());
    if (i >= 0) {
      String n = users[i].nick;
      users[i].used = false;
      broadcast("SYS|" + n + " keluar");
      broadcastUsers();
    }
    return;
  }
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;
  if (len > 400) return;

  String msg;
  msg.reserve(len);
  for (size_t i = 0; i < len; i++) msg += (char)data[i];

  int me = slotById(c->id());

  if (msg.startsWith("JOIN|")) {
    if (me >= 0) return;                                   // sudah masuk
    String rest = msg.substring(5);
    int bar = rest.indexOf('|');
    String n   = cleanNick(bar < 0 ? rest : rest.substring(0, bar));
    String tok = bar < 0 ? "" : cleanTok(rest.substring(bar + 1));
    if (n.length() == 0) { c->text("ERR|Nama tidak boleh kosong"); return; }

    // Browser yang sama (token sama) masuk lagi, misalnya setelah refresh:
    // sambungan lama diputus dan digantikan, bukan ditolak.
    bool silent = false;
    for (int i = 0; i < MAX_USERS; i++) {
      if (users[i].used && tok.length() > 0 && users[i].tok == tok) {
        String oldNick = users[i].nick;
        AsyncWebSocketClient* o = ws.client(users[i].id);
        users[i].used = false;
        if (o) o->close();
        if (oldNick.equalsIgnoreCase(n)) silent = true;
        else broadcast("SYS|" + oldNick + " keluar");
      }
    }

    if (canonicalNick(n).length() > 0) { c->text("ERR|Nama sudah dipakai orang lain, pilih nama lain"); return; }
    int s = freeSlot();
    if (s < 0) { c->text("ERR|Ruang chat penuh"); return; }
    users[s].used = true;
    users[s].id = c->id();
    users[s].nick = n;
    users[s].tok = tok;
    c->text("OK|" + n);
    for (int i = 0; i < histCount; i++) c->text(history[i]);
    if (!silent) broadcast("SYS|" + n + " masuk");
    broadcastUsers();

  } else if (msg.startsWith("MSG|") && me >= 0) {
    String text = msg.substring(4);
    text.trim();
    if (text.length() == 0) return;
    if (text.length() > 300) text = text.substring(0, 300);
    String out = "MSG|" + String(++seq) + "|" + users[me].nick + "|" + text;    // nama dipasang server, bukan client
    addHistory(out);
    broadcast(out);
  }
}

void handleUpload(AsyncWebServerRequest* r, const String& filename, size_t index,
                  uint8_t* data, size_t len, bool final) {
  String who = r->hasParam("nick") ? r->getParam("nick")->value() : "";

  if (index == 0) {
    if (canonicalNick(who).length() == 0 || r->contentLength() > FFat.freeBytes()) {
      r->abort();                                          // bukan pengguna online / penyimpanan penuh
      return;
    }
    String saved = String(millis() % 1000000) + "_" + safeName(filename);
    r->_tempFile = FFat.open("/" + saved, "w");
  }
  if (r->_tempFile && len) r->_tempFile.write(data, len);

  if (final && r->_tempFile) {
    String saved = r->_tempFile.name();
    r->_tempFile.close();
    String nick = canonicalNick(who);
    if (nick.length() == 0) { FFat.remove("/" + saved); return; }
    String out = "FILE|" + String(++seq) + "|" + nick + "|" + saved + "|" + safeName(filename) + "|" + String(index + len);
    addHistory(out);
    broadcast(out);
  }
}

void setup() {
  Serial.begin(115200);

  if (!FFat.begin(true)) {
    Serial.println("FFat gagal dimulai (cek Partition Scheme)");
  } else {
    if (CLEAR_FILES_ON_BOOT) clearFiles();
    Serial.printf("FFat: bebas %u KB\n", (unsigned)(FFat.freeBytes() / 1024));
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS[0] ? AP_PASS : nullptr, 1, 0, 8);   // maks 8 perangkat
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(ip);

  dns.start(53, "*", ip);                                  // captive portal

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", PAGE);
  });

  server.on("/upload", HTTP_POST,
            [](AsyncWebServerRequest* r) { r->send(200, "text/plain", "OK"); },
            handleUpload);

  server.on("/dl", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("name")) { r->send(400, "text/plain", "name?"); return; }
    String path = "/" + safeName(r->getParam("name")->value(), 80);
    if (!FFat.exists(path)) { r->send(404, "text/plain", "File tidak ada"); return; }
    r->send(FFat, path, "application/octet-stream", true);
  });

  server.onNotFound([ip](AsyncWebServerRequest* r) {
    r->redirect("http://" + ip.toString() + "/");
  });

  server.begin();
}

void loop() {
  dns.processNextRequest();
  ws.cleanupClients();
  static uint32_t lastPing = 0;
  if (millis() - lastPing > 10000) {       // ping berkala: sambungan mati cepat terdeteksi
    lastPing = millis();
    ws.pingAll();
  }
}
