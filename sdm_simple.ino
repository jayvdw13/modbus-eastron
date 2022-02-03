#include <SDM.h>   //import SDM library
#include <WiFi.h>
#include <HTTPClient.h>
#include <stdio.h>
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <HttpsOTAUpdate.h>
#include <Update.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/periph_ctrl.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "WiFiManager.H"
#include "ESPAsyncWebServer.h"
#define NOT_A_PIN 22
#define ESP32
#define PULSE_LENGTH 6
#define BUFSIZE 155


// Configuration
unsigned long lastTimeOTA = 0;
const unsigned long timerDelayOTA = 1800000;
uint32_t chipId = 0;
const String version = "1.0";
const String updateURL = "http://flexgreen.eu/esp/update";

const String registerServer = "http://flexgreen.eu/pulse/pulse_register";

unsigned int pulses[PULSE_LENGTH] = {};
String payloads[PULSE_LENGTH] = {};
long p1Data[7] = {};
bool readnextLine = false;
int bufpos = 0;

char buffer[BUFSIZE];
int messageCount = 0;


// Maak een async server op poort 80
AsyncWebServer asyncServer(80);
WiFiServer wifiServer(80);
WiFiManager wifiManager;


// function headers
void setupWebServer();
void checkUpdate();
void executeUpdate(uint8_t * data, int dataLength, int currentLength, int totalLength);
void readData();
void sendP1Data();


//HTML CSS
const char index_page[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<HTML>
  <HEAD>
    <META name='viewport' content='width=device-width, initial-scale=1'>
    <TITLE>SDM live POWER table</TITLE>
    <SCRIPT>
    var xmlHttp=createXmlHttpObject();
    function createXmlHttpObject(){
     if(window.XMLHttpRequest){
        xmlHttp=new XMLHttpRequest();
     }else{
        xmlHttp=new ActiveXObject('Microsoft.XMLHTTP');
     }
     return xmlHttp;
    }
    function process(){
      if(xmlHttp.readyState==0 || xmlHttp.readyState==4){
        xmlHttp.open('PUT','xml',true);
        xmlHttp.onreadystatechange=handleServerResponse;
        xmlHttp.send(null);
      }
      setTimeout('process()',2000);
    }
    function handleServerResponse(){
     if(xmlHttp.readyState==4 && xmlHttp.status==200){
       xmlResponse=xmlHttp.responseXML;
       for(i=0;i<5;i++){
        xmldoc=xmlResponse.getElementsByTagName('response'+i)[0].firstChild.nodeValue;
        document.getElementById('resp'+i).innerHTML=xmldoc;
       }
       xmldoc=xmlResponse.getElementsByTagName('sdmcnt')[0].firstChild.nodeValue;
       document.getElementById('sdmreadcnt').innerHTML=xmldoc;
       xmldoc=xmlResponse.getElementsByTagName('errtotal')[0].firstChild.nodeValue;
       document.getElementById('readerrtotal').innerHTML=xmldoc;
       xmldoc=xmlResponse.getElementsByTagName('errcode')[0].firstChild.nodeValue;
       document.getElementById('readerrcode').innerHTML=xmldoc;
       xmldoc=xmlResponse.getElementsByTagName('upt')[0].firstChild.nodeValue;
       document.getElementById('uptime').innerHTML=xmldoc;
       xmldoc=xmlResponse.getElementsByTagName('freeh')[0].firstChild.nodeValue;
       document.getElementById('freeheap').innerHTML=xmldoc;
     }
    }
    </SCRIPT>
    <STYLE>
      h1 {
        font-size: 120%;
        color: blue;
        margin: 0 0 10px 0;
      }
       table{
        border-collapse: collapse;
      }     
      table, th, td {
        text-align: center;
        border: 1px solid blue;
      }
      tr:nth-child(even) {background-color: #f2f2f2}
    </STYLE>
  </HEAD>
  <BODY onload='process()'>
    <CENTER>
      <H1>SDM live POWER table</H1>
      <TABLE BORDER=1>
        <TR><TH title="VOLTAGE">VOLTAGE</TH><TD><A id='resp0'></A></TD><TD>V</TD></TR>
        <TR><TH title="CURRENT">CURRENT</TH><TD><A id='resp1'></A></TD><TD>A</TD></TR>
        <TR><TH title="POWER">POWER</TH><TD><A id='resp2'></A></TD><TD>W</TD></TR>
        <TR><TH title="POWER FACTOR">POWER FACTOR</TH><TD><A id='resp3'></A></TD><TD>PF</TD></TR>
        <TR><TH title="FREQUENCY">FREQUENCY</TH><TD><A id='resp4'></A></TD><TD>Hz</TD></TR>
        <TR><TH title="SDM READ OK   COUNT">SDM READ OK   COUNT</TH><TD><A id='sdmreadcnt'></A></TD><TD>total</TD></TR>
        <TR><TH title="SDM READ ERR. COUNT">SDM READ ERR. COUNT</TH><TD><A id='readerrtotal'></A></TD><TD>total</TD></TR>
        <TR><TH title="SDM READ ERR. CODE ">SDM READ ERR. CODE </TH><TD><A id='readerrcode'></A></TD><TD>code</TD></TR>
        <TR><TH title="UPTIME">UPTIME</TH><TD><A id='uptime'></A></TD><TD>d h:m:s</TD></TR>
        <TR><TH title="FREE HEAP">FREE HEAP</TH><TD><A id='freeheap'></A></TD><TD>bytes</TD></TR>
      </TABLE>
    </CENTER>
  </BODY>
</HTML>
)=====";

 
#if defined ( USE_HARDWARESERIAL )                                              //for HWSERIAL
#if defined ( ESP8266 )                                                         //for ESP8266
  SDM sdm(Serial1, SDM_UART_BAUD, NOT_A_PIN, SERIAL_8N1);                                  //config SDM
#elif defined ( ESP32 )                                                         //for ESP32
  SDM sdm(Serial1, SDM_UART_BAUD, NOT_A_PIN, SERIAL_8N1, SDM_RX_PIN, SDM_TX_PIN);          //config SDM
#else                                                                           //for AVR
  SDM sdm(Serial1, SDM_UART_BAUD, NOT_A_PIN);                                              //config SDM on Serial1 (if available!)
#endif

#else                                                                           //for SWSERIAL

#include <SoftwareSerial.h>                                                     //import SoftwareSerial library
#if defined ( ESP8266 ) || defined ( ESP32 )                                    //for ESP
  SoftwareSerial swSerSDM;                                                                 //config SoftwareSerial
  SDM sdm(swSerSDM, SDM_UART_BAUD, NOT_A_PIN, SWSERIAL_8N1, SDM_RX_PIN, SDM_TX_PIN);       //config SDM
#else                                                                           //for AVR
  SoftwareSerial swSerSDM(SDM_RX_PIN, SDM_TX_PIN);                                         //config SoftwareSerial
  SDM sdm(swSerSDM, SDM_UART_BAUD, NOT_A_PIN);                                             //config SDM
#endif
#endif


void setup() {
  Serial.begin(9600);                                                          //initialize serial
  sdm.begin();                                                                   //initialize SDM communication
  wifiManager.autoConnect("FlexGreen-Counter", "FlexGreen");


  // Maak het chipId
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  Serial.println(String(chipId));
}


void setupWebServer() {
  // Laad HTML, CSS en JS op de webserver
  asyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_page);
  });


  // Start de webserver
  asyncServer.begin();
}


void loop() {
  if (millis() - lastTimeOTA > timerDelayOTA) {
    checkUpdate();
  }
  
  char bufout[10];
  sprintf(bufout, "%c[1;0H", 27);
  Serial.print(bufout);
  
  Serial.print("Voltage:   "); 
  Serial.print(sdm.readVal(SDM_PHASE_1_VOLTAGE), 2);                            //display voltage
  Serial.println("V");

  Serial.print("Current:   ");
  Serial.print(sdm.readVal(SDM_PHASE_1_CURRENT), 2);                            //display current
  Serial.println("A");

  Serial.print("Power:     ");
  Serial.print(sdm.readVal(SDM_PHASE_1_POWER), 2);                              //display power
  Serial.println("W");

  Serial.print("Frequency: ");
  Serial.print(sdm.readVal(SDM_FREQUENCY), 2);                                  //display frequency
  Serial.println("Hz");
  
  Serial.print("Tot. Active Energy: ");
  Serial.print(sdm.readVal(SDM_TOTAL_ACTIVE_ENERGY), 2);                                  //display frequency
  Serial.println("kWh");
  

  delay(12000);                                                                //wait a while before next loop
}


void sendP1Data() {
  // looks if the wifi is connected.
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // serial print to confirm the connection.
    Serial.println("P1 Poort");
    // preparation HTTP GET request.
    String serverPath = "http://flexgreen.eu/p1_poort/p1_data_register?chip_id=" + String
    (chipId) + String(SDM_PHASE_1_VOLTAGE) + String(SDM_PHASE_1_CURRENT) + String(SDM_PHASE_1_POWER) + String(SDM_FREQUENCY) + String(SDM_TOTAL_ACTIVE_ENERGY);
    Serial.println(serverPath);
    // Your Domain name with URL path or IP address with path.
    http.begin(serverPath.c_str());

    // Send HTTP GET request.
    int httpResponseCode = http.GET();

  // confirms the arrival of a response from the server or sends an error.
if (httpResponseCode > 0) {
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
  String payloadP1 = http.getString();
   Serial.println(payloadP1);
  } else {
  Serial.print("Error code: ");
  Serial.println(httpResponseCode);
}
  
  // close the http connection.
  http.end();
  } else {
  Serial.println("WiFi Disconnected");
}
}


void checkUpdate() {

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // server path for checking file version
    const String serverPath = updateURL + "?chip_id=" + String(chipId) + "&type=pulse_p1-poort&version=" + version;

    // Your Domain name with URL path or IP address with path
    http.begin(serverPath.c_str());

    // Send HTTP GET request
    const int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      const String payloadOTA = http.getString();

      Serial.println(payloadOTA);

      if (payloadOTA != "False") {
        int totalLength = http.getSize();
        int currentLength = 0;

        // this is required to start firmware update process
        Update.begin(UPDATE_SIZE_UNKNOWN);
        Serial.printf("FW Size: %u\n", totalLength);

        // get tcp stream
        WiFiClient * stream = http.getStreamPtr();

        // read all data from server
        Serial.println("Updating firmware...");
        while (http.connected() && totalLength > 0) {
          // get available data size
          if (stream->available() > 0) {
            // create buffer for read
            uint8_t buff[128] = {
              0
            };
            const int size = stream->available();
            // read up to 128 byte
            int gottenBytes = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));

            // pass to function
            executeUpdate(buff, gottenBytes, currentLength, totalLength);

            totalLength -= gottenBytes;
          }
          delay(1);
        }
      }
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
  lastTimeOTA = millis();
}


void executeUpdate(uint8_t * data, int dataLength, int currentLength, int totalLength) {

  Update.write(data, dataLength);
  currentLength += dataLength;
  // Print dots while waiting for update to finish
  Serial.print('.');

  // if current length of written firmware is not equal to total firmware size, repeat
  if (currentLength != totalLength) return;

  currentLength = 0;

  Update.end(true);
  Serial.printf("\nUpdate Success, Total Size: %u\nRebooting...\n", dataLength);

  // Restart ESP32 to see changes 
  ESP.restart();
}