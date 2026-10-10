#ifndef SHARED_H_
#define SHARED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


/*
 * 코어 간 규약 — CM33 과 CM7 이 바이트 단위로 합의해야 하는 것.
 *
 * titan-mini 의 src/cpu/shared/shared.h 와 같은 설계다. 이유도 같다.
 *   - common/ 은 MCU 가 바뀌어도 그대로 가는 자리라 여기 두지 않는다.
 *   - 코어별로 두 벌을 두면 같다는 것을 강제할 방법이 없다.
 *
 * 1. CM7 이미지 헤더 (cm7_image_t)
 *    QSPI 의 CM7 슬롯(SHARED_CM7_IMAGE_ADDR) 맨 앞에 붙는다. tools/mkcm7img.py 가
 *    CM7 빌드 결과에 붙이고, CM33 이 기동 전에 검사한다.
 *
 * 2. 공유 블록 (shared_t)
 *    OCRAM2 맨 앞(SHARED_BLOCK_ADDR). 두 코어가 같은 주소로 본다(docs/02 1절).
 *    두 링커 스크립트가 .shared (NOLOAD) 를 이 주소에 놓는다.
 *    CM7 의 D-cache 는 아직 꺼져 있다. 캐시를 켤 때(로드맵 25b) 이 구간을 MPU 로
 *    non-cacheable 로 잡아야 한다.
 *
 * 구조체를 고치면 SHARED_VERSION 을 올리고 두 코어를 함께 다시 빌드한다.
 */
#define SHARED_CM7_IMAGE_ADDR   0x04800000UL      // QSPI CM7 슬롯 (docs/02 5절)
#define SHARED_CM7_IMAGE_MAX    (256U * 1024U)    // CM7 ITCM 크기
#define SHARED_CM7_IMAGE_MAGIC  0x37434D52UL      // "RMC7"
#define SHARED_CM7_TCM_ADDR     0x303C0000UL      // CM33 에서 본 CM7 ITCM (Secure alias)

#define SHARED_BLOCK_ADDR       0x20500000UL      // OCRAM2
#define SHARED_MAGIC            0x48535452UL      // "RTSH"
#define SHARED_VERSION          1

#define SHARED_NAME_MAX         32
#define SHARED_VER_MAX          16


typedef struct
{
  uint32_t magic;           // SHARED_CM7_IMAGE_MAGIC
  uint32_t size;            // 헤더 뒤 이미지 바이트 수
  uint32_t crc32;           // 이미지의 CRC-32 (IEEE 802.3, 반사형)
  uint32_t reserved;
} cm7_image_t;              // 16 B. 이미지 본체가 바로 뒤에 온다


typedef struct
{
  volatile uint32_t magic;
  volatile uint32_t version;

  //-- 상대 코어가 살아 있음을 알린다. 주 코어가 증가를 확인한다.
  volatile uint32_t peer_alive;
  volatile uint32_t peer_tick;

  //-- 상대 코어의 자기소개. magic 이 서기 전에 채워진다.
  volatile uint32_t peer_clock;               // Hz
  char              peer_name[SHARED_NAME_MAX];
  char              peer_fw_ver[SHARED_VER_MAX];

} shared_t;


#ifdef __cplusplus
}
#endif

#endif
