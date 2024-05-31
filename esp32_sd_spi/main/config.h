#ifndef CONFIG_H
#define CONFIG_H

// Define the version macro to choose the pinout configuration
// Uncomment one of the following lines to select the device version
#define ESP32_WROOM
// #define NODEMCU

#ifdef ESP32_WROOM
    //Currently using HSPI line for SD card interfacing
    #define PIN_SD_MISO 2  // D0
    #define PIN_SD_MOSI 15 // D3
    #define PIN_SD_CLK 14  // SCK
    #define PIN_SD_CS 13   // CMD

    // Using VSPI line for data source (FPGA) interfacing 
    #define PIN_FPGA_MOSI 1
    #define PIN_FPGA_MISO 2
    #define PIN_FPGA_CLK  3
    #define PIN_FPGA_CS   4

#elif NODEMCU
    #define PIN_NUM_MISO 19  // D0
    #define PIN_NUM_MOSI 23 // D3
    #define PIN_NUM_CLK 18  // SCK
    #define PIN_NUM_CS 5   // CMD
#else
    #error "Device version not defined. Please define DEVICE_VERSION_1 or DEVICE_VERSION_2."
#endif

#endif // CONFIG_H