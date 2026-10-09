#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V261009R2"
#define _DEF_BOARD_NAME           "FRDM-IMXRT1186-CM33"


#define _USE_HW_LED
#define      HW_LED_MAX_CH          3

#define _USE_HW_SWTIMER
#define      HW_SWTIMER_MAX_CH      8

#define _USE_HW_BUTTON
#define      HW_BUTTON_MAX_CH       BUTTON_PIN_MAX

#define _USE_HW_UART
#define      HW_UART_MAX_CH         1
#define      HW_UART_CH_CLI         _DEF_UART1
#define      HW_UART_CH_LOG         _DEF_UART1

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_LOG
#define      HW_LOG_BOOT_BUF_MAX    1024
#define      HW_LOG_LIST_BUF_MAX    1024


//-- CLI
//
#define _USE_CLI_HW_UART             1
#define _USE_CLI_HW_LOG              1
#define _USE_CLI_HW_BOOT             1
#define _USE_CLI_HW_CLOCK            1
#define _USE_CLI_HW_BUTTON           1


// 사용자 버튼 SW4 (GPIO_AD_12)
//
typedef enum
{
  BTN_USER,

  BUTTON_PIN_MAX
} ButtonPinName_t;


#endif
