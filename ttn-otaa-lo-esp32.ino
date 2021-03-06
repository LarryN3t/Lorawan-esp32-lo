/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 * Copyright (c) 2018 Terry Moore, MCCI
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with payload "Hello,
 * world!", using frequency and encryption settings matching those of
 * the The Things Network.
 *
 * This uses OTAA (Over-the-air activation), where where a DevEUI and
 * application key is configured, which are used in an over-the-air
 * activation procedure where a DevAddr and session keys are
 * assigned/generated for use with all further communication.
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
 * g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
 * violated by this sketch when left running for longer)!

 * To use this sketch, first register your application and device with
 * the things network, to set or generate an AppEUI, DevEUI and AppKey.
 * Multiple devices can use the same AppEUI, but each device has its own
 * DevEUI and AppKey.
 *
 * Do not forget to define the radio type correctly in
 * arduino-lmic/project_config/lmic_project_config.h or from your BOARDS.txt.
 *
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
Adafruit_BMP280 bme; // I2C
#define SEALEVELPRESSURE_HPA (1013.25)
#define vbat 4
#define vext 27
#define vpan 15



//
// For normal use, we require that you edit the sketch to replace FILLMEIN
// with values assigned by the TTN console. However, for regression tests,
// we want to be able to compile these scripts. The regression tests define
// COMPILE_REGRESSION_TEST, and in that case we define FILLMEIN to a non-
// working but innocuous value.
//
#ifdef COMPILE_REGRESSION_TEST
# define FILLMEIN 0
#else
// warning "You must replace the values marked FILLMEIN with real values from the TTN control panel!"
# define FILLMEIN (#dont edit this, edit the lines that use FILLMEIN)
#endif

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8]={ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]={ 0xC4, 0x5A, 0x7C, 0x0A, 0x00, 0xF0, 0x07, 0x00 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

//static uint8_t mydata[] = "Hello, world!";
static char mydata1[40];
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;
char ptrTaskList[250];

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 5,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {33, 32, 35},
};

// Define the single channel and data rate (SF) to use
int channel = 0;
int Maxpac = 60;
int txpac = 0;
int dr = DR_SF10;

// Disables all channels, except for the one defined above, and sets the
// data rate (SF). This only affects uplinks; for downlinks the default
// channels or the configuration from the OTAA Join Accept are used.
//
// Not LoRaWAN compliant; FOR TESTING ONLY!
//
void forceTxSingleChannelDr() {
    for(int i=0; i<9; i++) { // For EU; for US use i<71
        if(i != channel) {
            LMIC_disableChannel(i);
        }
    }
    // Set data rate (SF) and transmit power for uplink
    LMIC_setDrTxpow(dr, 14);
}

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            {
              
              u4_t netid = 0;
              devaddr_t devaddr = 0;
              u1_t nwkKey[16];
              u1_t artKey[16];
              LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
              Serial.print("netid: ");
              Serial.println(netid, DEC);
              Serial.print("devaddr: ");
              Serial.println(devaddr, HEX);
              Serial.print("artKey: ");
              for (int i=0; i<sizeof(artKey); ++i) {
                Serial.print(artKey[i], HEX);
              }
              Serial.println("");
              Serial.print("nwkKey: ");
              for (int i=0; i<sizeof(nwkKey); ++i) {
                Serial.print(nwkKey[i], HEX);
              }
              Serial.println("");
            }
            // Disable link check validation (automatically enabled
            // during join, but because slow data rates change max TX
	          // size, we don't use it in this example.
            LMIC_setLinkCheckMode(0);
            forceTxSingleChannelDr();
            break;
        /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_RFU1:
        ||     Serial.println(F("EV_RFU1"));
        ||     break;
        */
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            } 
            // Schedule next transmission
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
        /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_SCAN_FOUND:
        ||    Serial.println(F("EV_SCAN_FOUND"));
        ||    break;
        */
        case EV_TXSTART:
            txpac = txpac + 1;
            Serial.print(F("TXPac: "));
            Serial.println(txpac);
            if (txpac == Maxpac ){
                ESP.restart();
            } 
            forceTxSingleChannelDr();
            Serial.println(F("EV_TXSTART"));
            break;
        default:
            Serial.print(F("Unknown event: "));
            Serial.println((unsigned) ev);
            break;
    }
}

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        int t = bme.readTemperature() *10 ; 
        //float h = bme.readHumidity(); 
        int p = bme.readPressure() * 10; 
        int a = bme.readAltitude(SEALEVELPRESSURE_HPA);
        int b =  analogRead(vbat);
        int e =  analogRead(vext);
        int pa =  analogRead(vpan);
        
        sprintf(mydata1,"|%.2f|%.2f|%.2f|%i|%i|%i",bme.readTemperature(),bme.readPressure(),bme.readAltitude(SEALEVELPRESSURE_HPA),b,e,pa);
        LMIC_setTxData2(1, (uint8_t*)mydata1, sizeof(mydata1)-1, 0);  
        Serial.println(F("Packet queued"));

 
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {

xTaskCreate(
    ttnTask,           /* Task function. */
    "TTN Task",        /* name of task. */
    4096,                    /* Stack size of task */
    NULL,                     /* parameter of the task */
    1,                        /* priority of the task */
    NULL);                    /* Task handle to keep track of created task */
}






void ttnTask( void * parameter ) {
    Serial.begin(9600);
    if (!bme.begin()) {  
        Serial.println("Could not find a valid BMP280 sensor, check wiring!");
      }
    Serial.println(F("Starting"));
    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();
    pinMode(vbat, INPUT);
    pinMode(vext, INPUT);
    pinMode(vpan, INPUT);
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF10),  BAND_CENTI);
     //LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF10), BAND_CENTI);      // g-band
     //LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF10),  BAND_CENTI);      // g-band
     //LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
     //LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
     //LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
     //LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
     //LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
     //LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MI




    
    //LMIC_setClockError(MAX_CLOCK_ERROR);
    int channel = 0;
    int dr = DR_SF10;
    for(int i=0; i<9; i++) { // For EU; for US use i<71
        if(i != channel) {
    LMIC_disableChannel(i);
        }
    }

    // Select frequencies range
    //LMIC_selectSubBand(0);
    // Disable link check validation
    //LMIC_setLinkCheckMode(0);
    // This tells LMIC to make the receive windows bigger, in case your clock is 1% faster or slower.
    //LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);
    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;
    LMIC_setDrTxpow(dr, 14);
    LMIC.txChnl = channel;
    LMIC.datarate = dr;
    LMIC_DEBUG_LEVEL > 2;
    LMIC_startJoining();
    // LMiC will already have decided to send on one of the 3 default
    // channels; ensure it uses the one we want
    // Start job (sending automatically starts OTAA too)
    do_send(&sendjob);
    for (;;) {
        os_runloop_once();
        vTaskDelay(1);
    }
}

void loop() {

}
