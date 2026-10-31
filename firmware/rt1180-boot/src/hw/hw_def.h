#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "B261031R3"
#define _DEF_BOARD_NAME           "FRDM-IMXRT1186-BOOT"


#define _USE_HW_INFO

#define _USE_HW_QSPI
#define      HW_QSPI_XIP            0

#define _USE_HW_LED
#define      HW_LED_MAX_CH          3
#define      HW_LED_DEF_DUTY        10

#define _USE_HW_RTC
#define      HW_RTC_BOOT_MODE       0
#define      HW_RTC_RESET_BITS      1
#define      HW_RTC_RESET_CNT       2
#define      HW_RTC_BOOT_TRY        3
#define      HW_RTC_FAULT_CNT       4
#define      HW_RTC_ECC_ADDR        5
#define      HW_RTC_ECC_VALID       6

#define _USE_HW_RESET
#define      HW_RESET_BOOT          1
#define      HW_RESET_DBLCLK_MS     300
#define      HW_RESET_DBLCLK_CNT    2

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_CLI         _DEF_UART1
#define      HW_UART_CH_LOG         _DEF_UART1

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CMD
#define      HW_CMD_MAX_DATA_LENGTH 1024
#define      HW_CMD_UART_CH         HW_UART_CH_CLI

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_LOG
#define      HW_LOG_BOOT_BUF_MAX    1024
#define      HW_LOG_LIST_BUF_MAX    1024


#define HW_DEV_MODE                 HW_DEV_MODE_BOOT

#define FLASH_ADDR_BOOT             0x04000000
#define FLASH_SIZE_BOOT             (256*1024)
#define FLASH_ADDR_FIRM             0x04040000
#define FLASH_SIZE_FIRM             (FLASH_ADDR_CM7 - FLASH_ADDR_FIRM)
#define FLASH_SIZE_TAG              0x1000
#define FLASH_SIZE_VEC              0x400
#define FLASH_ADDR_FIRM_VEC         (FLASH_ADDR_FIRM + FLASH_SIZE_TAG)
#define FLASH_ADDR_CM7              0x04800000
#define FLASH_SIZE_CM7              (4*1024*1024)
#define FLASH_ADDR_DATA             0x04C00000
#define FLASH_SIZE_DATA             (4*1024*1024)


//-- CLI
//
#define _USE_CLI_HW_UART             1
#define _USE_CLI_HW_LOG              1
#define _USE_CLI_HW_INFO             1
#define _USE_CLI_HW_RTC              1
#define _USE_CLI_HW_RESET            1
#define _USE_CLI_HW_QSPI             1
#define _USE_CLI_HW_MODULE           1
#define _USE_CLI_HW_BOOT             1


#endif
