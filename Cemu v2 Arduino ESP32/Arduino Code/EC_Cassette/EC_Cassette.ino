// GM E&C Cassette emulator
// Copyright Terry Kolody & QwertyChouskie
// 2011-01-26: V0.1 Beta
// 2022: QwertyChouskie revamp
// - Better logging
// - Error correction
// - Bluetooth functionality
// USE AT YOUR OWN RISK. I AM NOT RESPONSIBLE FOR YOU ACTIONS.
// NO WARRANTY IMPLIED
//

#include <inttypes.h>

#ifdef ESP32
// Bluetooth audio support on ESP32-based systems,
// using an I2S DAC to produce the analog output
// Download the BT Audio library here:
// https://github.com/tierneytim/btAudio
#include <btAudio.h>
#include <neotimer.h>
#endif

// Assuming signal inversion in voltage conversion circuit
// If not using voltage inversion, switch these
#define ECHIGH      HIGH
#define ECLOW       LOW
// Number of transmit attempts before timeout
#define TxRetry     10
// Period to wait beforestarting transmission (in microseconds)
#define TxSilence   5000
// Enums
#define TXOK        0
#define TXTMOUT     1
// Pulse lengths
// https://stuartschmitt.com/e_and_c_bus/bus_description.html
#define PulseLENGTH 1000
#define PulseZERO   111
#define PulseONE    667

#ifdef ESP32
#define EC_Rx       19 // Arduino pin for recieving
#define EC_Tx       22 // Arduino pin for transmission
#define button1Pin  34 // Arduino pin for debug button
#define button2Pin  35 // Arduino pin for debug button
#else
#define EC_Rx       5 // Arduino pin for recieving
#define EC_Tx       3 // Arduino pin for transmission
#define button1Pin  2 // Arduino pin for debug button
#define button2Pin  7 // Arduino pin for debug button
#endif

int button1State = 0;
int button2State = 0;
bool radioOn = 1;
bool cassetteLoaded = 0;
bool btAudioActive = 0;

Neotimer disconnect_timer = Neotimer(1000);

int timeout = 1;
uint64_t rxpacket = 0;
int rxpacket_length = 0;

#ifdef ESP32
// Set the name of the Bluetooth audio device
btAudio audio = btAudio("GM Stereo BT");

// Pins for the I2S DAC, e.g. https://www.adafruit.com/product/3678
#define pin_bck  26
#define pin_ws   27
#define pin_dout 25

// We receive the prev/next commands twice or more when pressing once, 
// so don't take action if previous action was less than 2 seconds ago.
Neotimer dupeCatch = Neotimer(2000);

#endif

// Perform initial setup, run unit tests, then insert vitrual cassette
void setup()
{
    // Define modes for Tx/Rx pins
    pinMode(EC_Rx, INPUT);
    pinMode(EC_Tx, OUTPUT);

    // Default Tx to logical low
    digitalWrite(EC_Tx, ECLOW);

    // Buttons for manual testing of functions
    pinMode(button1Pin, INPUT_PULLUP);
    pinMode(button2Pin, INPUT_PULLUP);

    // Bringup serial with 115200 baud
    // NOTE: Lower bauds cause serial printing functions to slow significantly,
    // which can cause delays in important functions,
    // therefore affecting program behavior!
    Serial.begin(115200);

    // Run unit testing
    //unitTestErrorCorrection();

#ifdef ESP32
    // Stream Bluetooth audio to ESP32
    audio.begin();
    btAudioActive = true;

    // Re-connect to last connected device
    audio.reconnect();

    // Output the received data to the I2S DAC
    audio.I2S(pin_bck, pin_dout, pin_ws);

    // Set max volume to fix distortion/popping issue
    audio.volume(0.5);

    // Start timer for catching duplicate prev/next commands
    dupeCatch.start();

    // Delay before we begin bitbanging, to minimize the
    // chance of Bluetooth stuff interferring with timing.
    delay(500);
#endif

    // Tell radio that cassette has been inserted
    insertCassette();
}

// ----------- Main program loop -----------
void loop()
{
    if (disconnect_timer.done()) {
        audio.disconnect();
        btAudioActive = false;
        disconnect_timer.stop();
        disconnect_timer.reset();
    }

    // ----------- Debug button 1 -----------
    if (digitalRead(button1Pin) == 0)
    {
        button1State = 5;
    }
    else if (button1State > 1)
    {
        // Wait some loop cycles before taking action, to filter false unpresses
        button1State -= 1;
    }
    else if (button1State == 1) // Button released, run action
    {
        // Debug code here
        insertCassette();
        // End debug code
        // Reset button state
        button1State = 0;
    }

    // ----------- Debug button 2 -----------
    if (digitalRead(button2Pin) == 0)
    {
        button2State = 5;
    }
    else if (button2State > 1)
    {
        // Wait some loop cycles before taking action, to filter false unpresses
        button2State -= 1;
    }
    else if (button2State == 1) // Button released, run action
    {
        // Debug code here
        ejectCassette();
        // End debug code
        // Reset button state
        button2State = 0;
    }

    // ----------- Begin packet read -----------

    int i;      
    i = pulseIn(EC_Rx, ECHIGH, 3000);

    if (i != 0) {
        if (i < 50) {
            Serial.println("Communication error: Short pulse");
            timeout = 1;  
            rxpacket = 0;
            rxpacket_length = 0;
        }

        if (i > 50 && i < 300) {
            timeout = 0;
            // Bitshift right by 1
            rxpacket = rxpacket << 1;
            // New bit is 0 by default, which is already what we want
            // Just increment packet length by 1
            rxpacket_length += 1;
        }

        if (i >= 300 && i < 800) {
            timeout = 0;
            // Bitshift right by 1
            rxpacket = rxpacket << 1;
            // Set new bit to 1
            bitSet(rxpacket, 0);
            // Increment packet length by 1
            rxpacket_length += 1;
        }

        if (i >= 800) {
            Serial.println("Communication error: Long pulse"); 
            timeout = 1;
            rxpacket = 0;
            rxpacket_length = 0;
        }
    } 
    else {
        if (timeout == 0) {
            rxpacket = errorCorrect(rxpacket, rxpacket_length);
            processResult(rxpacket);

            rxpacket = 0;
            rxpacket_length = 0;
            timeout = 1;
        }
    }
}

// Insert virtual cassette tape
void insertCassette()
{
    cassetteLoaded = 1;
    sendE_C(0x00C31082, 23);
    sendE_C(0x00C3108B, 23);
}

// Eject virtual cassette tape
void ejectCassette()
{
    sendE_C(0x00030C43, 17);
    cassetteLoaded = 0;
}

// Run error correction on recieved packet
// See https://stuartschmitt.com/e_and_c_bus/bus_description.html
// Returns corrected data, or 0 if unable to correct
uint64_t errorCorrect(uint64_t packet, int packet_length)
{
    uint64_t corrected_packet = packet;

    if (packet_length % 2 != 0) // If odd
    {
        Serial.println("Packet warning: Length odd, prepending start bit");
        // Prepend a 1 to the data (start bit is always 1)
        // bitSet position parameter start at 0, packet_length starts at 1
        // so packet_length would set the next bit
        bitSet(corrected_packet, packet_length);
        // Update packet_length
        packet_length += 1;

        // The parity bit is determined such that there are always an
        // even number of 1s that follow the start bit. Since the start
        // bit is always a 1, there are always an odd number of 1s in a frame.
        if (!checkParityValid(corrected_packet)) // packet parity not valid
        {
            // We have lost more than 3 bytes of data, or had a bitflip
            // somewhere, just cut our losses
            Serial.println("Packet error: Unable to reconstruct, dropping packet");
            packet_length = 0;
            return 0;
        }
    }

    // The parity bit is determined such that there are always an
    // even number of 1s that follow the start bit. Since the start
    // bit is always a 1, there are always an odd number of 1s in a frame.
    if (!checkParityValid(corrected_packet)) // packet parity not valid
    {
        Serial.println("Packet error: Parity mismatch, prepending 10");
        // Prepend 10 to the data
        // bitSet position parameter start at 0, packet_length starts at 1
        // so packet_length would set the next bit
        // next bit is already supposed to be 0, so only set the bit before to 1
        //bitSet(corrected_packet, packet_length+1);
        // Update packet_length
        //packet_length += 2;

        // For now, just cut our losses
        Serial.println("Packet error: Unable to reconstruct, dropping packet");
        packet_length = 0;
        return 0;
    }
    //else if (data starts with 00)
    //{
    //    prepend 11 to the data;
    //    // Update packet_length
    //    packet_length += 2;
    //    return;
    //}
    // else return, datagram is error-free

    // Sanity check for at least 2 bits of actual data in the packet
    if (packet_length < 12)
        {
            Serial.println("Packet error: Too short, dropping packet");
            return 0;
        }

    //if (packet != corrected_packet)
    //{
    //    Serial.print("Corrected packet: ");
    //    printPacket(corrected_packet, packet_length);
    //}

    rxpacket_length = packet_length; // FIXME: global vars bad
    return corrected_packet;
}

// Process recieved E&C packets
void processResult(uint64_t packet)
{
    // If virtual cassette is unloaded, don't do anything
    // if (!cassetteLoaded)
    //     return;
    
    switch (packet) {
    //case 0x0000E704:
        //Serial.println("IC");
        //delay(50);
        //sendE_C(0x0030C075, 22);
        //break;
    
    // Seen when:
    // - radio powered on via button
    // - ignition turned off
    case 0x00000E6F: // Radio powered on or ignition turned off
        Serial.print("Radio powered on or ignition turned off ");
        printPacket(packet, rxpacket_length);
        //insertCassette();
        break;

    case 0x000C2FE4: // Ignition turned off while radio on
        Serial.println("Ignition turned off while radio on");
        radioOn = false;

        #ifdef ESP32
        // Pause playback
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);
        
        // Disable Bluetooth
        disconnect_timer.start();
        #endif

        break;

    case 0x0000C2FF: // Ignition turned off while radio off
        Serial.println("Ignition turned off while radio off");

        #ifdef ESP32
        // Pause playback
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);

        // Disable Bluetooth
        disconnect_timer.start();
        #endif

        break;

    // Seen when:
    // - key to from off to acc
    // - key from off to on
    // - radio turned on via button
    case 0x0030BF95: // Radio powered on
        Serial.println("Radio powered on");
        radioOn = true;
        #ifdef ESP32
        if (!btAudioActive) {
            audio.reconnect();
            btAudioActive = true;
        }
        disconnect_timer.stop();
        disconnect_timer.reset();
        #endif
        insertCassette();
        break;

    // Seen when:
    // - radio turned off using button
    // - ignition turn on but radio is off
    case 0x0030BF84: // Radio powered off
        Serial.println("Radio powered off");
        radioOn = false;

        #ifdef ESP32
        // Pause playback
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);
        #endif

        break;

    // Case Cold Start
    case 0x0000E70E: // Theres a tape dear liza.. 
    case 0x0000E70D: // Theres a tape dear liza.. 
        Serial.println("Case Cold Start");
        sendE_C(0x0030C602, 21);
        sendE_C(0x0030C064, 21);
        sendE_C(0x0030C075, 21);
        break;
 
    case 0x0000E716: // Next
        Serial.println("Button pressed: Next");

#ifdef ESP32
        if (dupeCatch.done())
        {
            esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
            esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
            dupeCatch.reset();
            dupeCatch.start();
        }
#endif

        sendE_C(0x0030C692, 21);
        sendE_C(0x000C301C, 19);
        sendE_C(0x0030C703, 21);
        sendE_C(0x000C301F, 19);
        // Call Reverse function here. 
        break;

    case 0x0000E715: // Prev
        Serial.println("Button pressed: Previous");

#ifdef ESP32
        if (dupeCatch.done())
        {
            esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
            esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
            dupeCatch.reset();
            dupeCatch.start();
        }
#endif

        sendE_C(0x0030C68A, 21);
        sendE_C(0x000C301C, 19);
        sendE_C(0x0030C703, 21);
        sendE_C(0x000C301F, 19);
        break;
    
    case 0x0000E71A: // Fast Forward
        Serial.println("Button pressed: Fast Forward");
#ifdef ESP32
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_FAST_FORWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_FAST_FORWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
#endif
        sendE_C(0x0030C613, 21); // ACK
        sendE_C(0x000C301C, 19); // ?
        sendE_C(0x0030C643, 21); // Resume playback
        break;
    
    case 0x000039C7: // Rewind
        Serial.println("Button pressed: Rewind");
#ifdef ESP32
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_REWIND, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_REWIND, ESP_AVRC_PT_CMD_STATE_RELEASED);
#endif
        sendE_C(0x0030C60B, 21); // ACK
        sendE_C(0x000C301C, 19); // ?
        sendE_C(0x0030C643, 21); // Resume playback
        break;
    
    case 0x0000E71C: // Send the play and the flip..   
        sendE_C(0x000C305D, 19);
        sendE_C(0x0030C643, 21);
        break;
    
    case 0x0000E71F: // Theres a tape dear liza.. 
        sendE_C(0x00030C26, 17);
        //sendE_C(0x000C309D, 19);
        break;

    // Seen when:
    // - key to from off to acc
    // - key from off to on
    case 0x00039B82:
        sendE_C(0x000C309D, 19);
        break;
    
    case 0x00000E72: // Stop playing.. 
        sendE_C(0x0030C075, 21);
        sendE_C(0x0030C064, 21);
        break;

#ifdef ESP32
    case 0x0000E70B: // Dolby on
        // Use this as pause command
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);
        break;

    case 0x0000E707: // Dolby off
        // Use this as resume command
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_RELEASED);
        break;
#endif

    default: 
        // Packet not handled, just print
        Serial.print("Unknown packet:   ");
        printPacket(packet, rxpacket_length);
        break;
    }
}

/* Send a packet. 
    packet encoded in a uint64_t, with a length
    Returns status. (Timeout or OK)   
 */
int sendE_C(uint64_t packet, int length) 
{
    // If virtual cassette is unloaded, don't do anything
    if (!cassetteLoaded)
        return TXTMOUT;

    int retryCount = 0;
    int returnVal = TXTMOUT;

    // Wait for silence period > TxSilence interval
    while (retryCount < TxRetry)
    {
        if (pulseIn(EC_Rx, ECHIGH, TxSilence) == 0 )
        {
            returnVal = TXOK; 
            break;  
        }
        Serial.println("Warning sending command: Bus is busy, retrying...");
        retryCount++;
    }

    if (returnVal == TXOK)  // We have silence. Let er rip off the bits to the bus.
    {
        #ifdef ESP32
        // Make sending the packet a critical section, to prevent
        // interference from other processes messing up the timing.
        portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
        taskENTER_CRITICAL(&myMutex);
        #endif

        for (int i = length; i >=0; i--)
        {
            if (bitRead(packet, i) == 1 ) 
            {
                // Send ONE 
                digitalWrite(EC_Tx, ECHIGH);
                delayMicroseconds(PulseONE);
                digitalWrite(EC_Tx, ECLOW);
                delayMicroseconds(PulseLENGTH - PulseONE); 
            } 
            else {
                // Send ZERO
                digitalWrite(EC_Tx, ECHIGH);
                delayMicroseconds(PulseZERO);
                digitalWrite(EC_Tx, ECLOW);
                delayMicroseconds(PulseLENGTH - PulseZERO); 
            }
        }

        #ifdef ESP32
        // Packet sending complete, exit critical section.
        taskEXIT_CRITICAL(&myMutex);
        #endif

        Serial.write("Sent packet:      ");
        printPacket(packet, length+1);
    }
    else if (returnVal == TXTMOUT)
    {
        Serial.println("Error sending command: Timeout");
    }

    return returnVal;
}

// Function to extract k bits from p position (starts at 1)
// and returns the extracted value as integer
int bitExtracted(uint64_t number, int k, int p)
{
    return (((1 << k) - 1) & (number >> (p - 1)));
}

// Print packet data, including Hex and binary representations, and packet length
void printPacket(uint64_t packet, int length)
{
    uint32_t lsb = packet & 0xffffffff;
    uint32_t msb = packet >> 32;

    char s1 [16];
    char s2 [16];
    //sprintf(s, "0x%llX", packet); // uint64 printf not supported by AVR libc
    sprintf(s1, "0x%08" PRIX32 " ", msb);
    sprintf(s2, "%08" PRIX32, lsb);
    Serial.print(s1); Serial.print(s2);

    uint8_t priority = bitExtracted(packet, 2, length-2);
    uint8_t address  = bitExtracted(packet, 6, length-8);
    Serial.write("   "); Serial.print(priority);
    Serial.write("-");   Serial.print(address);
    uint8_t data1 = 0, data2 = 0;
    if (length <= 18) {
        data1 = bitExtracted(packet, length-9, 2);
        Serial.write("-");   Serial.print(data1);
    } else {
        data1 = bitExtracted(packet, 8, length-16);
        data2 = bitExtracted(packet, length-9-8, 2);
        Serial.write("-");   Serial.print(data1);
        Serial.write("-");   Serial.print(data2);
    }

    Serial.write("   ");
    Serial.print(msb, BIN);
    Serial.write(" ");
    Serial.print(lsb, BIN);
    Serial.write(" ");
    Serial.println(length);
    //Serial.println(packet, BIN); // unit64_t not supported
}

// Returns true if odd number of 1s in variable
bool checkParityValid(uint64_t packet)
{
    unsigned int count = 0;
    while (packet) {
        count += packet & 1;
        packet >>= 1;
    }
    return count % 2 != 0; // Returns 0 (false) if even, 1 (true) if odd
}

// A handful of example packets that should be handled properly
void unitTestErrorCorrection()
{
    Serial.println("\n******Unit testing start******\n");

    Serial.println("Recieved packet:  0x00000000 0000270D  0 10011100001101 14");
    errorCorrect(0x0000270D, 14);
    Serial.println("Should be no correction above\n");

    Serial.println("Recieved packet:  0x00000000 D1B64824  0 11010001101101100100100000100100 32");
    errorCorrect(0xD1B64824, 32);
    Serial.println("Should be no correction above\n");

    Serial.println("Recieved packet:  0x00000000 51B60425  0 1010001101101100000010000100101 31");
    errorCorrect(0x51B60425, 31);
    Serial.println("Should prepend 1 above");
    Serial.println("Correct packet  : 0x00000000 D1B60425  0 11010001101101100000010000100101 32\n");

    Serial.println("Recieved packet:  0x00000000 D1B64424  0 11010001101101100100010000100100 32");
    errorCorrect(0xD1B64424, 32);
    Serial.println("Should be no correction above\n");

    Serial.println("Recieved packet:  0x00000000 11B64424  0 10001101101100100010000100100 29");
    errorCorrect(0x11B64424, 29);
    Serial.println("Should dump packet above\n");

    Serial.println("******Unit testing done******\n");
}
