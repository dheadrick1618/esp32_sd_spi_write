#ifndef CONFIG_H
#define CONFIG_H

// Define the version macro to choose the pinout configuration
// Uncomment one of the following lines to select the device version
// #define ESP32_WROOM
#define NODEMCU

#ifdef ESP32_WROOM
    //Currently using HSPI line for SD card interfacing
    #define PIN_SD_MISO 2  // D0
    #define PIN_SD_MOSI 15 // D3
    #define PIN_SD_CLK 14  // SCK
    #define PIN_SD_CS 13   // CMD

    // Using VSPI line for data source (FPGA) interfacing 
    #define PIN_FPGA_MOSI 23
    #define PIN_FPGA_MISO 19
    #define PIN_FPGA_CLK  18
    #define PIN_FPGA_CS   5

#elif defined(NODEMCU)
    #define PIN_SD_MISO 19  // D0
    #define PIN_SD_MOSI 23 // D3
    #define PIN_SD_CLK 18  // SCK
    #define PIN_SD_CS 5   // CMD

    #define PIN_FPGA_MOSI 13
    #define PIN_FPGA_MISO 12
    #define PIN_FPGA_CLK  14
    #define PIN_FPGA_CS   15
#else
    #error "Device version not defined. Please define DEVICE_VERSION_1 or DEVICE_VERSION_2."
#endif

#endif // CONFIG_H