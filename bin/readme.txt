ne301_FSBL_signed.bin       --> use for stm32n6 FSBL        --> flash addr 0x70000000
ne301_App_signed_pkg.bin    --> use for stm32n6 App         --> flash addr 0x70100000
ne301_Web_pkg.bin           --> use for web gui             --> flash addr 0x70400000
ne301_Model_pkg.bin         --> use for AI model            --> flash addr 0x70900000
ne301_WakeCore.bin          --> use for stm32u0 wakecore    --> flash addr 0x08000000 

flash tools supported 
    1、 STM32CubeProgrammer 
    2、 Script/maker.sh flash <bin-name> <flash-addr> 
    3、 make flash           