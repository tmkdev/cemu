// GM EC Cassette emulator
// Terry Kolody
// 2011-01-26
// V0.1 Beta. 
// USE AT YOUR OWN RISK. I AM NOT RESPONSIBLE FOR YOU ACTIONS.
// NO WARRANTY IMPLIED
// See schematic in Eagle folder for info
//

#include  <util/parity.h> 

#define ECHIGH     LOW
#define ECLOW      HIGH
#define TxRetry    10
#define TxSilence  5000
#define TXOK       0
#define TXTMOUT    1

//http://pangea.stanford.edu/~schmitt/e_and_c_bus/
#define PulseLENGTH 1000
#define PulseZERO   100
#define PulseONE  650

const int EC_Rx = 3;
const int EC_Tx = 4;

// Header Info 
const unsigned int cassetteHeader = 0x0000;
const unsigned int radioCmdHeader = 0x0000;

/*
Initialize sequence. 
 Send 8B
 Send 
 E704 -> Respond with 30C06
 
 */
int timeout = 1;
unsigned long rxpacket;

void setup()
{
  pinMode(EC_Rx, INPUT);
  pinMode(EC_Tx, OUTPUT);  
  digitalWrite(EC_Tx, ECHIGH); 
  Serial.begin(9600); 

  initCassette();

}

void loop()
{
  // Returns 1 if val as odd number of bits.
  //parity_even_bit(0xFF00FF00); 


  int i;      
  i = pulseIn(EC_Rx,LOW, 1200);

  if (i != 0) {
    if (i < 50) {
      Serial.println("ShortPulse");
      timeout = 1;  
      rxpacket = 0;
    }

    if (i >50 && i < 300) {
      timeout = 0;
      rxpacket = rxpacket << 1;

    }

    if (i >300 && i < 800) {
      timeout = 0; 
      rxpacket = rxpacket << 1;
      bitSet(rxpacket,0);

    }

    if (i > 800) {
      Serial.println("LongPulse"); 
      timeout = 1;
      rxpacket = 0;
    }

  } 
  else {
    if (timeout == 0) {
      processResult(rxpacket);
      //Serial.println(rxpacket, HEX);
      rxpacket = 0; 
      timeout = 1;
    } 

  }
}


/*
Initialize sequence. 
 Send 8B
 Send 
 
 Subsequent cassette poll responses 
 E704 -> Respond with 30C06
 */

void initCassette()
{
    sendE_C(0x00C31082, 23); //Gets E70E everytime!
    sendE_C(0x00C3108B, 23);

}


void processResult(unsigned long packet)
{
  switch (packet) {
  case 0x0000E704:
    //Serial.println("IC");
   //delay(50);
   //sendE_C(0x0030C075, 22);
    
    break;
    
  // Case Cold Start
  case 0x0000E70E: // Theres a tape dear liza.. 
    sendE_C(0x0030C602, 21);
    sendE_C(0x0030C064, 21);
    sendE_C(0x0030C075, 21);
    break;
  
  case 0x0000E70D: // Theres a tape dear liza.. 
    sendE_C(0x0030C602, 21);
    sendE_C(0x0030C064, 21);
    sendE_C(0x0030C075, 21);
    break;
 
  case 0x0000E716: // Fast Forwards
    sendE_C(0x0030C692, 21);
    sendE_C(0x000C301C, 19);
    sendE_C(0x0030C703, 21);
    sendE_C(0x000C301F, 19);
    // Call Reverse function here. 
    break;

  case 0x0000E715: // Reverse
    sendE_C(0x0030C68A, 21);
    sendE_C(0x000C301C, 19);
    sendE_C(0x0030C703, 21);
    sendE_C(0x000C301F, 19);
    break;
    
  case 0x0000E71A: // FFWD
    sendE_C(0x0030C613, 21);
    sendE_C(0x000C301C, 19);
    sendE_C(0x0030C643, 21);
    break;
    
  case 0x000039C7: // RRWD
    sendE_C(0x0030C60B, 21);
    sendE_C(0x000C301C, 19);
    sendE_C(0x0030C643, 21);
    break;
    
 case 0x0000E71C: // Send the play and the flip..   
    sendE_C(0x000C305D, 19);
    sendE_C(0x0030C643, 21);
    break;
    
  case 0x0000E71F: // Theres a tape dear liza.. 
    sendE_C(0x00030C26, 17);
    //sendE_C(0x000C309D, 19);
    break;

  case 0x00039B82:
    sendE_C(0x000C309D, 19);
    break;
    
  case 0x00000E72: // Stop playing.. 
    sendE_C(0x0030C075, 21);
    sendE_C(0x0030C064, 21);
    break;

  default: 
    // if nothing else matches, do the default
    // default is optional
    break;
  }

}


/* Send a packet. 
 packet encoded in a long, with a length
 
 
 
 Returns status. (Timeout or OK)  
 
 */


int sendE_C(unsigned long packet, int length) 
{
  int retryCount = 0;
  int returnVal = TXTMOUT;

  // Wait for silence period > TxSilence Interval
  while (retryCount < TxRetry)
  {
    if (pulseIn(EC_Rx, LOW, TxSilence) == 0 )
    {
      returnVal = TXOK; 
      break;  
    }
    retryCount++;
  }

  if (returnVal == TXOK)  // We have silence. Let er rip off the bits to the bus.
  {
    for(int i = length; i >=0; i--)
    {
      if (bitRead(packet, i) == 1 ) 
      {
        // Send ONE 
        digitalWrite(EC_Tx, ECLOW);
        delayMicroseconds(PulseONE);
        digitalWrite(EC_Tx, ECHIGH);
        delayMicroseconds(PulseLENGTH - PulseONE); 

      } 
      else {
        // Send ZERO
        digitalWrite(EC_Tx, ECLOW);
        delayMicroseconds(PulseZERO);
        digitalWrite(EC_Tx, ECHIGH);
        delayMicroseconds(PulseLENGTH - PulseZERO); 
      }

    } 

  } 

  return returnVal; 
}


