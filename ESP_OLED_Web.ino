#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

const char* WIFI_NAME = "NUMELE_WIFI";
const char* WIFI_PASSWORD = "PAROLA_WIFI";

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);

bool oledOK = false;
int puls = 0, spo2 = 0, ecg = -1, miscare = 0;
float temperatura = 0.0, presiune = 0.0;
unsigned long ultimaPrimire = 0;
unsigned long ultimaAfisare = 0;

const char PAGINA[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="ro"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Thoracare</title><style>
*{box-sizing:border-box}body{margin:0;font-family:Arial;color:#f8fafc;background:linear-gradient(135deg,#050816,#182044);min-height:100vh}
header,main{max-width:1100px;margin:auto;padding:24px}h1{margin-bottom:4px}.sub{color:#93c5fd}.status{padding:16px;border:1px solid #22d3ee;border-radius:18px;background:#101936;margin:20px 0}.ok{color:#4ade80}.off{color:#fbbf24}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:16px}.card{padding:20px;border-radius:20px;background:#131c39;border:1px solid #293655}.label{color:#a5bff7}.value{font-size:27px;font-weight:bold;margin:9px 0}.sensor{color:#7896cf;font-size:14px}canvas{width:100%;height:210px;background:#091126;border-radius:18px;margin-top:20px}footer{text-align:center;color:#7690c7;padding:28px}
</style></head><body><header><h1>Thoracare</h1><div class="sub">Vestă inteligentă cu Hugging Shield</div><div id="status" class="status off">Așteaptă ESP-ul cu senzori</div></header><main><section class="grid">
<div class="card"><div class="label">Puls</div><div class="value"><span id="puls">--</span> BPM</div><div class="sensor">MAX30102</div></div>
<div class="card"><div class="label">SpO₂</div><div class="value"><span id="spo2">--</span> %</div><div class="sensor">MAX30102</div></div>
<div class="card"><div class="label">Temperatura IMU</div><div class="value"><span id="temp">--</span> °C</div><div class="sensor">MPU6050</div></div>
<div class="card"><div class="label">Mișcare</div><div class="value" id="miscare">--</div><div class="sensor">MPU6050</div></div>
<div class="card"><div class="label">Presiune aer</div><div class="value"><span id="presiune">--</span> kPa</div><div class="sensor">MPXV7002DP</div></div>
<div class="card"><div class="label">ECG brut</div><div class="value" id="ecg">--</div><div class="sensor">AD8232 + ADS1115</div></div>
</section><canvas id="grafic"></canvas></main><footer>Prototip educațional – fără rol medical</footer>
<script>
const valori=[];async function update(){try{const r=await fetch('/data?t='+Date.now());const d=await r.json();const s=document.getElementById('status');if(d.senzori_online){s.className='status ok';s.textContent='ESP senzori conectat';puls.textContent=d.puls;spo2.textContent=d.spo2;temp.textContent=Number(d.temperatura).toFixed(1);presiune.textContent=Number(d.presiune).toFixed(2);ecg.textContent=d.ecg;miscare.textContent=d.miscare?'Detectată':'Repaus';valori.push(Number(d.ecg));if(valori.length>100)valori.shift();draw()}else{s.className='status off';s.textContent='Așteaptă ESP-ul cu senzori'}}catch(e){status.textContent='Conexiune întreruptă'}}
function draw(){const c=document.getElementById('grafic'),x=c.getContext('2d'),w=c.clientWidth,h=210;c.width=w*devicePixelRatio;c.height=h*devicePixelRatio;x.scale(devicePixelRatio,devicePixelRatio);x.clearRect(0,0,w,h);if(valori.length<2)return;let mn=Math.min(...valori),mx=Math.max(...valori);if(mn==mx)mx++;x.beginPath();x.strokeStyle='#ec4899';x.lineWidth=3;valori.forEach((v,i)=>{const px=i*w/(valori.length-1),py=h-12-(v-mn)/(mx-mn)*(h-24);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}update();setInterval(update,1000);
</script></body></html>)rawliteral";

bool senzoriOnline() { return ultimaPrimire && millis() - ultimaPrimire < 5000; }

void afiseaza(String a, String b, String c, String d) {
  if (!oledOK) return;
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(0,0); display.println(a); display.setCursor(0,16); display.println(b);
  display.setCursor(0,32); display.println(c); display.setCursor(0,48); display.println(d); display.display();
}

void paginaPrincipala() { server.send_P(200, "text/html; charset=UTF-8", PAGINA); }

void trimiteJSON() {
  String j = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"puls\":" + String(puls) +
    ",\"spo2\":" + String(spo2) + ",\"ecg\":" + String(ecg) +
    ",\"temperatura\":" + String(temperatura,1) + ",\"miscare\":" + String(miscare) +
    ",\"presiune\":" + String(presiune,2) + ",\"senzori_online\":" + (senzoriOnline()?"true":"false") + "}";
  server.send(200, "application/json; charset=UTF-8", j);
}

void primesteDate() {
  if (server.hasArg("puls")) puls=server.arg("puls").toInt();
  if (server.hasArg("spo2")) spo2=server.arg("spo2").toInt();
  if (server.hasArg("ecg")) ecg=server.arg("ecg").toInt();
  if (server.hasArg("temp")) temperatura=server.arg("temp").toFloat();
  if (server.hasArg("miscare")) miscare=server.arg("miscare").toInt();
  if (server.hasArg("presiune")) presiune=server.arg("presiune").toFloat();
  ultimaPrimire=millis(); server.send(200,"text/plain","OK");
}

void setup() {
  Serial.begin(115200); Wire.begin(D2,D1);
  oledOK=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDRESS);
  afiseaza("THORACARE","Pornire...","","");
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(WIFI_NAME,WIFI_PASSWORD);
  while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print('.');}
  Serial.println(); Serial.print("IP ESP Monitor: "); Serial.println(WiFi.localIP());
  afiseaza("WIFI CONECTAT","IP:",WiFi.localIP().toString(),"Server activ");
  server.on("/",paginaPrincipala); server.on("/data",trimiteJSON); server.on("/update",primesteDate); server.begin();
}

void loop() {
  server.handleClient();
  if(millis()-ultimaAfisare>=1000){ultimaAfisare=millis();
    if(senzoriOnline()) afiseaza("SENZORI CONECTATI","Puls: "+String(puls),"SpO2: "+String(spo2)+"%","ECG: "+String(ecg));
    else afiseaza("ASTEPT SENZORI","IP:",WiFi.localIP().toString(),"Server activ");
  }
}
