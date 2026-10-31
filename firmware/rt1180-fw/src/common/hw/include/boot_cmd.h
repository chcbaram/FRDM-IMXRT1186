#ifndef BOOT_CMD_H_
#define BOOT_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


/*
 * 부트로더 cmd 셋 (docs/43). 부트로더(rt1180-boot)와 앱(rt1180-fw) 이 같은 정의를 쓴다.
 * 명령 코드 · 응답 구조는 NUCLEO-N657X0 (weact-h750) 과 같아 download.py 를 그대로 쓴다.
 * 대상(target) 3 = CM7 슬롯은 이 보드에서 더했다.
 */
#define BOOT_CMD_INFO             0x0000
#define BOOT_CMD_VERSION          0x0001
#define BOOT_CMD_FW_BEGIN         0x0002    // [size:4] [target:1] [offset:4 (DATA)]
#define BOOT_CMD_FW_ERASE         0x0003
#define BOOT_CMD_FW_WRITE         0x0004    // [offset:4] [data]
#define BOOT_CMD_FW_READ          0x0005    // [offset:4] [len:4] [target:1]
#define BOOT_CMD_FW_END           0x0006    // -> [size:4] [crc:4]
#define BOOT_CMD_FW_VERIFY        0x0007
#define BOOT_CMD_FW_UPDATE        0x0008
#define BOOT_CMD_FW_JUMP          0x0009
#define BOOT_CMD_BAUD             0x0020    // [baud:4] -> 응답 뒤 보율을 바꾼다 (UART)
#define BOOT_CMD_RESET            0x0021

#define BOOT_TARGET_FW            0
#define BOOT_TARGET_BOOT          1
#define BOOT_TARGET_DATA          2
#define BOOT_TARGET_CM7           3

#define BOOT_CMD_VER              2         // 1 : N657X0 확장, 2 : + CM7 슬롯


/*
 * INFO 응답. 앞 104 바이트는 weact 와 같다. cmd_ver 부터가 확장이다.
 */
typedef struct
{
  uint32_t magic;
  uint32_t mode;              // HW_DEV_MODE_BOOT / HW_DEV_MODE_APP
  uint32_t boot_addr;         // 부트 슬롯
  uint32_t boot_size;
  uint32_t firm_addr;         // TAG 시작
  uint32_t firm_vec_addr;     // 앱 벡터 시작
  uint32_t firm_size;
  uint32_t tag_size;
  uint32_t max_fw_size;
  uint32_t family_id;
  char     name[32];
  char     version[32];

  uint32_t cmd_ver;           // 여기부터 확장
  uint32_t boot2_addr;        // 두 번째 부트 슬롯 (이 보드는 없다 : 0)
  uint32_t data_addr;
  uint32_t data_size;
  uint32_t baud;              // 지금 보율

  uint32_t cm7_addr;          // cmd_ver 2
  uint32_t cm7_size;
} __attribute__((packed)) boot_info_t;

typedef struct
{
  uint8_t  img_type;          // BootImgType_t
  uint8_t  rsv[3];
  uint32_t fw_size;
  uint32_t fw_crc;
  char     name[32];
  char     version[32];
} __attribute__((packed)) boot_version_t;


#ifdef __cplusplus
}
#endif

#endif
