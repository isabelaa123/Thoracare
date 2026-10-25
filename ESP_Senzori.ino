#include <ESP8266WiFi.h>
#include <Wire.h>
#include <MAX30105.h>
#include "spo2_algorithm.h"
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

const char* WIFI_NAME = "NUMELE_WIFI";
const char* WIFI_PASSWORD = "PAROLA_WIFI";
const char* IP_ESP_MONITOR = "192.168.1.100"; // Înlocuiește cu IP-ul afișat pe OLED

#define PIN_SDA D6
#define PIN_SCL D5
#define PIN_LO_PLUS D1
#define PIN_LO_MINUS D2

WiFiClient client;
MAX30105 max30102;
Adafruit_ADS1115 ads;
Adafruit_MPU6050 mpu;
bool maxOK=false, adsOK=false, mpuOK=false;

uint32_t irBuffer[100], redBuffer[100], irValue=0;
uint16_t bufferPos=0;
int32_t heartRate=0, oxygen=0;
int8_t heartValid=0, oxygenValid=0;
unsigned long ultimaTrimitere=0;

bool i2c(uint8_t address){Wire.beginTransmission(address);return Wire.endTransmission()==0;}

void conectareWiFi(){
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(WIFI_NAME,WIFI_PASSWORD);
  Serial.print("Conectare WiFi"); while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print('.');}
  Serial.println(); Serial.print("IP ESP senzori: "); Serial.println(WiFi.localIP());
}

void initMAX(){
  if(!i2c(0x57)){Serial.println("MAX30102 lipsa");return;}
  maxOK=max30102.begin(Wire,I2C_SPEED_STANDARD); if(!maxOK)return;
  max30102.setup(60,4,2,100,411,4096); max30102.setPulseAmplitudeRed(0x1F);
  max30102.setPulseAmplitudeIR(0x1F); max30102.setPulseAmplitudeGreen(0);
}

void initADS(){
  if(!i2c(0x48)){Serial.println("ADS1115 lipsa");return;}
  adsOK=ads.begin(0x48,&Wire); if(adsOK)ads.setGain(GAIN_ONE);
}

void initMPU(){
  mpuOK=mpu.begin(0x68,&Wire); if(!mpuOK)mpuOK=mpu.begin(0x69,&Wire);
  if(mpuOK){mpu.setAccelerometerRange(MPU6050_RANGE_8_G);mpu.setGyroRange(MPU6050_RANGE_500_DEG);mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);}
}

void citesteMAX(){
  if(!maxOK)return; max30102.check();
  while(max30102.available()){
    redBuffer[bufferPos]=max30102.getRed(); irBuffer[bufferPos]=max30102.getIR(); irValue=irBuffer[bufferPos];
    bufferPos++; max30102.nextSample();
    if(bufferPos>=100){
      maxim_heart_rate_and_oxygen_saturation(irBuffer,100,redBuffer,&oxygen,&oxygenValid,&heartRate,&heartValid);
      for(int i=25;i<100;i++){irBuffer[i-25]=irBuffer[i];redBuffer[i-25]=redBuffer[i];} bufferPos=75;
    }
    yield();
  }
}

void trimite(int p,int s,long e,float t,bool m,float pressure){
  if(WiFi.status()!=WL_CONNECTED || !client.connect(IP_ESP_MONITOR,80)){Serial.println("Monitorul nu raspunde");return;}
  String path="/update?puls="+String(p)+"&spo2="+String(s)+"&ecg="+String(e)+"&temp="+String(t,1)+"&miscare="+(m?"1":"0")+"&presiune="+String(pressure,2);
  client.print(String("GET ")+path+" HTTP/1.1\r\nHost: "+IP_ESP_MONITOR+"\r\nConnection: close\r\n\r\n");
  delay(20); client.stop();
}

void setup(){
  Serial.begin(115200); pinMode(PIN_LO_PLUS,INPUT); pinMode(PIN_LO_MINUS,INPUT);
  Wire.begin(PIN_SDA,PIN_SCL); Wire.setClock(100000);
  initMAX(); initADS(); initMPU(); conectareWiFi();
  Serial.printf("MAX30102:%s ADS1115:%s MPU6050:%s\n",maxOK?"OK":"LIPSA",adsOK?"OK":"LIPSA",mpuOK?"OK":"LIPSA");
}

void loop(){
  citesteMAX();
  if(WiFi.status()!=WL_CONNECTED)conectareWiFi();
  if(millis()-ultimaTrimitere<1000){yield();return;} ultimaTrimitere=millis();

  bool finger=irValue>50000;
  int p=(finger&&heartValid&&heartRate>=30&&heartRate<=220)?heartRate:0;
  int s=(finger&&oxygenValid&&oxygen>=70&&oxygen<=100)?oxygen:0;
  bool leadsOff=digitalRead(PIN_LO_PLUS)||digitalRead(PIN_LO_MINUS);
  long e=(adsOK&&!leadsOff)?ads.readADC_SingleEnded(0):-1;
  float temp=0; bool movement=false;
  if(mpuOK){sensors_event_t a,g,tm;mpu.getEvent(&a,&g,&tm);temp=tm.temperature;
    float total=sqrt(a.acceleration.x*a.acceleration.x+a.acceleration.y*a.acceleration.y+a.acceleration.z*a.acceleration.z);
    movement=abs(total-9.81)>1.5 || abs(g.gyro.x)+abs(g.gyro.y)+abs(g.gyro.z)>0.5;
  }
  float pressure=0.0; // MPXV7002DP va fi adăugat după conectare și calibrare
  Serial.printf("Puls:%d SpO2:%d ECG:%ld Temp:%.1f Miscare:%s\n",p,s,e,temp,movement?"DA":"NU");
  trimite(p,s,e,temp,movement,pressure);
}
