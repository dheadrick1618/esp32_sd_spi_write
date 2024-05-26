#ifndef CONFIG_H
#define CONFIG_H

// Define the version macro to choose the pinout configuration
// Uncomment one of the following lines to select the device version
#define ESP32_WROOM
// #define OTHER

#ifdef ESP32_WROOM
    #define PIN_NUM_MISO 2  // D0
    #define PIN_NUM_MOSI 15 // D3
    #define PIN_NUM_CLK 14  // SCK
    #define PIN_NUM_CS 13   // CMD

#elif OTHER
    #define PIN_NUM_MISO 19  // D0
    #define PIN_NUM_MOSI 23 // D3
    #define PIN_NUM_CLK 18  // SCK
    #define PIN_NUM_CS 5   // CMD
#else
    #error "Device version not defined. Please define DEVICE_VERSION_1 or DEVICE_VERSION_2."
#endif

#endif // CONFIG_H