// GM EC Scanner
// Terry Kolody
// 2011-01-26
// V0.1 Beta. 
// USE AT YOUR OWN RISK. I AM NOT RESPONSIBLE FOR YOU ACTIONS.
// NO WARRANTY IMPLIED
// See schematic in Eagle folder for info
//


int timeout = 1;
unsigned long packet;

void setup() {
    pinMode(3,INPUT);
    Serial.begin(57600);
    Serial.println("Starting");
}

void loop()
{
      int i;      
      i = pulseIn(3,LOW, 1200);
      
      if (i != 0) {
        if (i < 50) {
            Serial.println("ShortPulse");
            timeout = 1;  
            packet = 0;
        }
        
        if (i >50 && i < 300) {
             timeout = 0;
             packet = packet << 1;
            
        }
        
        if (i >300 && i < 800) {
             timeout = 0; 
             packet = packet << 1;
             bitSet(packet,0);
             
        }
        
        if (i > 800) {
           Serial.println("LongPulse"); 
           timeout = 1;
           packet = 0;
        }
        
      } else {
        if (timeout == 0) {
          Serial.print(packet, HEX);
          Serial.print(" "); 
          Serial.println(packet, BIN);
          packet = 0; 
          timeout = 1;
        } 
        
      }

}

