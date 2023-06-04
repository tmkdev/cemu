//CEMU on ATTINY85
// T Kolody
// Feb 2011

#include <stdio.h>
#include <stdint.h>
#include <avr/io.h>
#include <string.h>
#include <util/delay.h>


#define F_CPU 16000000UL  // 16 MHz

#define baud 9600
#define bit_delay 1000000/baud

#define ec_rx PB3
#define ec_tx PB4

/*
 *
 * Settings
 *
 */

#define TxRetry    10
#define TXOK       0
#define TXTMOUT    1

//http://pangea.stanford.edu/~schmitt/e_and_c_bus/
#define PulseLENGTH 1000
#define PulseZERO   100
#define PulseONE  650

#define bitRead(var,pos) ((var) & (1<<(pos)))


char mystr[15];


void serialWrite(char bite)
{

PORTB = 0x00;  //signal start bit
_delay_us(bit_delay);

 	for (uint8_t mask = 0x01; mask; mask <<= 1) {
	    if (bite & mask){ // choose bit
	      PORTB = 0x01; // send 1
	    }
	    else{
	      PORTB = 0x00; // send 0
	    }
	    _delay_us(bit_delay);
	  }


	PORTB = 0x01;  //signal end bit
	_delay_us(bit_delay);

}

uint8_t pulseIn( void )
{
	uint8_t returnVal = 0x00;
	// Clear the timer.
	TCNT0 = 0x00;
	TIFR |= (1 << TOV0);

	while (bit_is_set( PINB, ec_rx ) && bit_is_clear( TIFR, TOV0 ) )
 	{ }

	// If the overflow hasent happened, then we must have gone low.
	if ( !bit_is_clear( TIFR, TOV0) )
	{
		return(returnVal);
	}

	//Clear the timer again..
	TCNT0 = 0;

	while (bit_is_clear( PINB, ec_rx ) && bit_is_clear( TIFR, TOV0 ) )
 	{ }

	returnVal = TCNT0;

	// We overflowed
	if ( !bit_is_clear( TIFR, TOV0) )
	{
		return(0x00);
	}

	return(returnVal);

}



void init()
{

   // Set up Counter.
   TCCR0B |= ((1 << CS01) | (1 << CS00));
   DDRB = 0b00010011; // Set outputs. serial_tx and ec_tx
   // todo: fix with right shit.. 
   PORTB = 0x01; //Set PB0 high
   // Set ec_tx low. 
   PORTB &= ~(1 << ec_tx);
   
   //serialWrite('h');
   initCassette();

}



void dumpRxpacket(uint32_t packet)
{
	sprintf(mystr, "%lx\r\n", packet );
	unsigned int string_len = strlen(mystr);

	for (unsigned int i=0; i < string_len; i++)
	{
		serialWrite(mystr[i]);
	}

}

void dumpint(int num)
{
	sprintf(mystr, "%u\r\n", num );
	unsigned int string_len = strlen(mystr);

	for (unsigned int i=0; i < string_len; i++)
	{
		serialWrite(mystr[i]);
	}

}

uint8_t sendE_C(uint32_t packet, int length) 
{
  int retryCount = 0;
  int returnVal = TXTMOUT;
  // Wait for silence period > TxSilence Interval
  
  while (retryCount < TxRetry)
  {
	uint8_t pin = pulseIn();
	  
    if (pin == 0 )
    {
      returnVal = TXOK;
      break;
    }
    retryCount++;
  }
  
  if (returnVal == TXOK)  // We have silence. Let er rip off the bits to the bus.
  {
	
	 for (int i=length; i >= 0; i-- ) {
	  uint32_t mask = (uint32_t)1 << i;

      if ( ((packet) & (mask)) ) 
      {
        // Send ONE
         PORTB |= 1 << ec_tx;
         _delay_us(650);
        PORTB &= ~(1 << ec_tx);
		_delay_us(350);
      }
      else {
        // Send ZERO
        PORTB |= 1 << ec_tx;
		_delay_us(100);
        PORTB &= ~(1 << ec_tx);
		_delay_us(900);
      }

    }

  }

  _delay_ms(4);
  return returnVal;
}


void initCassette()
{
    sendE_C(0x00C31082, 23); //Gets E70E everytime!
    _delay_us(1000);
    sendE_C(0x00C3108B, 23);

}

void processResult(uint32_t packet)
{
  switch (packet) {
  case 0x0000E704:
    //Serial.println("IC");
   //delay(50);
   //sendE_C(0x0030C075, 22);
    initCassette();
    
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

int main (void)
{
   uint32_t rxpacket = 0x00000000;
   uint8_t pulseWidth = 0;
   uint8_t timeout = 0;

   init();

   for (;;)
   {
	        // While pin PB2 is high
			/*
			loop_until_bit_is_clear( PINB, PB2 );
			pulseWidth = pulseInLow();

			sprintf(mystr, "%u\r\n", pulseWidth );

	                unsigned int string_len = strlen(mystr);
			for (unsigned int i=0; i < string_len; i++)
			{
				serialWrite(mystr[i]);
			}
			*
			*/

		   
			uint8_t i;
			i = pulseIn();

			if (i != 0) {
				if (i <= 5) {
					//Serial.println("ShortPulse");
					timeout = 1;
					rxpacket = 0;
				}

			if (i >5 && i < 50) {
				timeout = 0;
				rxpacket = rxpacket << 1;

			}

			if (i >=50 && i < 220) {
				timeout = 0; 
				rxpacket = rxpacket << 1 | (1 << 0);
			}

			if (i >= 220) {
				//Serial.println("LongPulse"); 
				timeout = 1;
				rxpacket = 0;
			}

		} 
		else {
			if (timeout == 0) {
				processResult(rxpacket);
				//dumpRxpacket(rxpacket);
				rxpacket = 0;
				timeout = 1;
			}

		} // read loop
   } //for loop

}

