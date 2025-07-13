#include "esp_camera.h"
#include "properties.h"
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
								   
#define CAMERA_MODEL_AI_THINKER // Has PSRAM

#include "camera_pins.h"

//  WiFi credeciales tomadas del archivo properties.h				  
const char *ssid = VAR_SSID;
const char *password = VAR_PASS;

//  Pins configs
const int pinEcho = 4;
const int pinTrig = 2;
const byte MAX_CHARS=16;
const byte MAX_ROWS=2;
const int pinSDA = 14;
const int pinSCL = 15;

// Vars - no utilizadas por ahora
//int delayTime = 0;

// LCD
LiquidCrystal_I2C lcd(0x27,MAX_CHARS, MAX_ROWS);

// Conf para subir imagen, VAR_SERVER tomado del archivo properties.h				  
const char *serverUrl = VAR_SERVER; // Server URL
String contentLengthStr = String("Content-Length=");
String crLf = String("\r\n");
String bodyStart = "--boundary\r\nContent-Disposition: form-data; name=image; filename=esp32-cam.jpg\r\nContent-Type: image/jpeg\r\n\r\n";
String bodyEnd = "\r\n--boundary--\r\n";
int contentLength = 0;

void setup() {
  //Serial.begin(115200);

  //configuracion de la camara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
                    
  config.pixel_format = PIXFORMAT_JPEG; 

  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;       
    config.jpeg_quality = 10;
    config.fb_count = 2;
    //Serial.println("usamos memoria");    
  } else {                  
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;         
    config.fb_count = 1;
    //Serial.println("no usamos memoria");
  }

  
  // inicializa camara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    //Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  else {
    //Serial.println("Camera init successful!");
  }
  
  // Conecta a wifi
  //Serial.println("Connecting to wifi");
  WiFi.begin(ssid, password);
  
  // Espera hasta que wifi conecte, sino no continua
  while (WiFi.status() != WL_CONNECTED) { 
    //Serial.println("Connecting to WiFi..");
    delay(10000);
  }
  ////Serial.println("Wifi connected");
  
  // Pines para el sensor HCSR-04
  pinMode(pinTrig, OUTPUT);
  pinMode(pinEcho, INPUT);

  Wire.begin(pinSDA, pinSCL); // inicializa Wire() para LCD necesario en ESP32-CAM, ya que no tiene pines I2C fijos.
  lcd.init(); // inicializa lcd
  lcd.backlight(); // prende la luz
  lcd.clear(); // limpia lcd											                          
										
}


float takeDistance(){
  digitalWrite(pinTrig, LOW);  //para generar un pulso limpio ponemos a LOW 4us
  delayMicroseconds(4);
  digitalWrite(pinTrig, HIGH);  //generamos Trigger (disparo) de 10us
  delayMicroseconds(10);
  digitalWrite(pinTrig, LOW);
  
  int duration = pulseIn(pinEcho, HIGH, 20000);  //medimos el tiempo entre pulsos, en microsegundos
  //Serial.println(duration);
  int cm = duration *0.034/2;
  return cm;
}

String postImage() {
  //Serial.println("Getting Frame buffer");
  camera_fb_t * frameBuffer = NULL;
  frameBuffer = esp_camera_fb_get();
  if (!frameBuffer) {
    //Serial.println("Frame buffer could not be acquired");
    esp_camera_fb_return(frameBuffer);
    return "Failure - no image";
  }

  WiFiClient wifiClient;
  if (WiFi.status() != WL_CONNECTED) {
    //Serial.println("Wifi is not connected!");
    esp_camera_fb_return(frameBuffer);
    return "Failure, no wifi";
  }
  //Serial.println("Connecting to Patriot server");
  if (!wifiClient.connect(serverUrl, 8080)) {
    //Serial.println("Connection to Patriot server failed!");
  }
  else {
    uint32_t imageLength = frameBuffer->len;
    contentLength = bodyStart.length() + imageLength + bodyEnd.length();
    //Serial.print("Content length: ");
    //Serial.println(String(contentLength));
    //Serial.println("Posting image");
    //URL
    wifiClient.println("POST /image/upload HTTP/1.1");
    //SERVER
    wifiClient.println("Host: 192.168.99.62");

    wifiClient.println("Content-Length: " + String(contentLength));
    wifiClient.println("Content-Type: multipart/form-data;boundary=boundary\r\n");

    wifiClient.print(bodyStart);
    wifiClient.write(frameBuffer->buf, frameBuffer->len);            
    wifiClient.print(bodyEnd);

    esp_camera_fb_return(frameBuffer);

    //Serial.println("Response:");
    long tiempoInicio = millis();
    long wait = 0;
    
    while (wifiClient.connected() && wait <5000) { //espera la respuesta hasta que finalize o como maximo 5 segundos, evita espera eterna
      if (wifiClient.available()) {
        String response = wifiClient.readStringUntil('\n');
        //Serial.println(response.c_str());
      }
      wait =  millis() - tiempoInicio;
    }
    //Serial.println("Stopping wifiClient");
    wifiClient.stop();
    delay(50);
    //Serial.println("wifiClient stopped");
  }

  //Serial.println("Returning framebuffer");
  esp_camera_fb_return(frameBuffer);
  return "done";
          
}

void loop() {

  float cm = takeDistance();
  //Serial.print("Lectura de distancia: ");
  //Serial.println(cm);
  //Serial.println("captura imagen");

  if (cm>0 && cm<=20){ //si la distancia tomada, esta entre 0 y 20, identifico un objeto
    lcd.setCursor(0, 0);
    lcd.print("Objeto detectado");
    postImage();
    lcd.setCursor(0, 1);
    lcd.print("Imagen tomada");
    //delayTime = delayTime + 5000; //suma 5 segundos para evitar reenvio de imagenes tan seguidas
    //envia espera 5
    //envia espera 10
    //envia espera 15
    //envia espera 20
    //envia espera 25
    //luego del minuto y 15 segundos (5 envios), reinicia el contador y vuelve a enviar con la frecuencia inical
  }else{
    //delayTime = 0;
    //lcd.clear();
    //lcd.setCursor(0, 0);
    //lcd.print("No se detecta");
    //lcd.setCursor(0, 1);
    //lcd.print("Objeto");
    //si no detecta nada no hay delay, genera que siempre este midiendo la distancia
  }
  delay(1000); //para ver msj de imagen capturada y no tomar tantas distancias seguidas
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Detectando...");

  //if (delayTime>=60000){
  //  delayTime = 0;
  //}

  //Serial.println(delayTime);
  //delay(2000);
  //delay(delayTime); //la logica del delay no se utiliza por ahora, ya que de por si el envio de imagenes es entre 10 y 20 segundos
}