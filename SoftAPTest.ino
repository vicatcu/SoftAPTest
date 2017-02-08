#include <ESP8266_AT_Client.h>
#include "jsmn.h"

const int esp8266_en = 23;
const int esp8266_gpio0 = 21;
Stream * at_command_interface = &Serial1; 
ESP8266_AT_Client esp(esp8266_en, at_command_interface);

#define ESP8266_INPUT_BUFFER_SIZE (1500)
uint8_t esp8266_input_buffer[ESP8266_INPUT_BUFFER_SIZE] = {0};
#define SCRATCH_BUFFER_SIZE (512)
char scratch[SCRATCH_BUFFER_SIZE] = { 0 };  // scratch buffer, for general use

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial1.begin(115200);

  esp.reset();

  esp.setTcpKeepAliveInterval(10); // 10 seconds
  esp.setInputBuffer(esp8266_input_buffer, ESP8266_INPUT_BUFFER_SIZE); // connect the input buffer up   

//  esp.setDebugStream(&Serial);
//  esp.enableDebug();
  
  doSoftApModeConfigBehavior();
} 

void loop() {
  
}

#define EEPROM_MAC_ADDRESS    (E2END + 1 - 6)    // MAC address, i.e. the last 6-bytes of EEPROM

void doSoftApModeConfigBehavior(void){  
  
  randomSeed(millis());
  char random_password[16] = {0}; // 8 characters randomly chosen
  const uint8_t random_password_length = 8;;
  static const char whitelist[] PROGMEM = {
    '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  
    'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  
    'J',  'K',  'L',  'M',  'N',  'P',  'R',  'S',
    'T',  'U',  'V',  'W',  'X',  'Y',  'Z',  'a',  
    'b',  'c',  'd',  'e',  'f',  'g',  'h',  'i',  
    'j',  'k',  'm',  'n',  'o',  'p',  'q',  'r',  
    's',  't',  'u',  'v',  'w',  'x',  'y',  'z'
  };
  uint8_t _mac_address[6] = {0};
  eeprom_read_block((void *)_mac_address, (const void *) EEPROM_MAC_ADDRESS, 6);
  char egg_ssid[16] = {0};
  sprintf(egg_ssid, "egg-%02x%02x%02x", _mac_address[3], _mac_address[4], _mac_address[5]);  

  uint8_t ii = 0;
  while(ii < random_password_length){
    uint8_t idx = random(0, 'z' - '0' + 1);
    char c = (char) ('0' + idx); 
    
    // if it's in the white list allow it
    boolean in_whitelist = false;
    for(uint8_t jj = 0; jj < sizeof(whitelist); jj++){
      char wl_char = pgm_read_byte(&(whitelist[jj]));      
      if(wl_char == c){
        in_whitelist = true;
        break;
      }
    }

    if(in_whitelist){
      random_password[ii++] = c;     
    }
  }

  Serial.print("SSID: "); Serial.println(egg_ssid);
  Serial.print("PASS: "); Serial.println(random_password);
  
  uint32_t seconds_remaining_in_softap_mode = 12000UL; // stay in softap for 2 minutes max  
  boolean settings_accepted_via_softap = false;
  const uint16_t softap_http_port = 80;
  char ssid[33] = {0};
  char pwd[33] = {0};
  
  if(esp.setNetworkMode(3)){ // means softAP mode
    Serial.println(F("Info: Enabled Soft AP"));    
    if(esp.configureSoftAP(egg_ssid, random_password, 5, 3)){ // channel = 5, sec = WPA      
      // open a port and listen for config data messages, for up to two minutes
      if(esp.listen(softap_http_port)){
        Serial.print(F("Info: Listening for connections on port "));
        Serial.print(softap_http_port);
        Serial.println(F("..."));
        unsigned long previousMillis = 0;
        const long interval = 1000;        
        uint16_t scratch_idx = 0;
        boolean got_opening_brace = false;        
        boolean got_closing_brace = false;
                
        while((seconds_remaining_in_softap_mode != 0) && (!settings_accepted_via_softap)){
          unsigned long currentMillis = millis();

          if (currentMillis - previousMillis >= interval) {            
            previousMillis = currentMillis;
            if(seconds_remaining_in_softap_mode != 0){
              seconds_remaining_in_softap_mode--;
            }            
          }
          
          // pay attention to incoming traffic
          while(esp.available()){
            char c = esp.read();
            if(got_opening_brace){
              if(c == '}'){
                got_closing_brace = true;    
                scratch[scratch_idx++] = c;
                break;
              }
              else{
                if(scratch_idx < SCRATCH_BUFFER_SIZE - 1){
                  scratch[scratch_idx++] = c;                  
                }
                else{
                  Serial.println("Warning: scratch buffer out of memory");
                }
              }
            }
            else if(c == '{'){              
              got_opening_brace = true;
              scratch[scratch_idx++] = c;
            }            
          }                     

          if(got_closing_brace){
            Serial.println("Message Body: ");
            Serial.println(scratch);  
            
            // send back an HTTP response 
            // Then send a few headers to identify the type of data returned and that
            // the connection will not be held open.                            
            char response_template[] = "HTTP/1.1 200 OK\r\n"         
              "Content-Type: application/json; charset=utf-8\r\n"
              "Connection: close\r\n"
              "Server: air quality egg\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s";
              
            char good_response_body_template[] = "{\"serial\":\"%s\"}";
            char bad_response_body_template[] = "{\"error\":\"no ssid and password supplied\"}";
            char response_body[64] = {0};              
            char serial_number[] = "egg00802e891c2b0521";
            
            if(parseConfigurationMessageBody(scratch, &(ssid[0]), &(pwd[0]))){
              Serial.print("SSID: "); Serial.println(ssid);
              Serial.print("PWD: "); Serial.println(pwd);

              sprintf(response_body, good_response_body_template, serial_number);             
              memset(scratch, 0, SCRATCH_BUFFER_SIZE);              
              sprintf(scratch, response_template, strlen(response_body), response_body);
                                         
              settings_accepted_via_softap = true;
            }
            else{
              // send back an HTTP response indicating an error              
              memset(scratch, 0, SCRATCH_BUFFER_SIZE);              
              sprintf(scratch, response_template, strlen(bad_response_body_template), bad_response_body_template);              
            }
            Serial.print("Responding With: ");
            Serial.println(scratch);  
            esp.print(scratch);

            // and wait 100ms to make sure it gets back to the caller
            delay(100); 
            
            memset(scratch, 0, SCRATCH_BUFFER_SIZE);
            scratch_idx = 0;

            // if the parse failed we're back to waiting for a message body
            got_closing_brace = false;
            got_opening_brace = false;            
          }          
        }
  
        if(settings_accepted_via_softap){
          Serial.println(F("Info: Exiting Soft AP Mode, changes were accepted"));           
        }
        else{
          Serial.println(F("Info: Exiting Soft AP Mode, no changes were made"));           
        }              
      }
      else{
        Serial.print(F("Error: Failed to start TCP server on port "));
        Serial.println(softap_http_port);
      }

      esp.setNetworkMode(1);
    }
    else{
      Serial.println(F("Error: Failed to configure Soft AP"));
    }
  }
  else{
    Serial.println(F("Error: Failed to start Soft AP Mode"));  
  }  

  Serial.println(F("Info: Exiting SoftAP Mode"));
}

boolean parseConfigurationMessageBody(char * body, char * ssid, char * pwd){
  jsmn_parser parser;
  jsmntok_t tokens[32];
  jsmn_init(&parser);

  boolean found_ssid = false;
  boolean found_pwd = false;
  
  uint16_t r = jsmn_parse(&parser, body, strlen(body), tokens, 10);
  Serial.print("Found ");
  Serial.print(r);
  Serial.println(" JSON tokens");
  char key[32] = {0};
  char value[32] = {0};
  for(uint8_t ii = 1; ii < r; ii+=2){    
    memset(key, 0, 32);
    memset(value, 0, 32);
    uint16_t keylen = tokens[ii].end - tokens[ii].start;
    uint16_t valuelen = tokens[ii+1].end - tokens[ii+1].start;

    if(keylen < 32){
      strncpy(key, body + tokens[ii].start, keylen);      
    }

    if(valuelen < 32){
      strncpy(value, body + tokens[ii+1].start, valuelen);
    }
    
    Serial.print(key);
    Serial.print(" => ");
    Serial.print(value);
    Serial.println();   

    if(strcmp(key, "ssid") == 0){
      found_ssid = true;
      strcpy(ssid, value);
    }
    else if(strcmp(key, "pwd") == 0){
      found_pwd = true;
      strcpy(pwd, value);
    }
  }

  return found_ssid && found_pwd;
}

