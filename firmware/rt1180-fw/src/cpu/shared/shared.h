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
 *    CM7 은 D-cache 를 켜고 이 구간만 MPU 로 non-cacheable 로 잡는다(cm7/bsp/mpu.c).
 *
 * 3. 메시지 (MU1)
 *    공유 블록은 상태를 두는 곳이고, 알림은 MU 로 보낸다. 메시지는 id + data 32 bit 두 개다.
 *    보내는 쪽이 TR1(data) 을 먼저, TR0(id) 를 나중에 쓰고, 받는 쪽은 RX0 full 인터럽트에서
 *    RR1 → RR0 순으로 읽는다. id 0 은 쓰지 않는다. 아래 SHARED_MSG_* 가 예약된 id 다.
 *
 * 4. 스트림 (공유 블록의 링 버퍼 + MU doorbell)
 *    방향마다 링 버퍼가 하나씩 있다(shared_t.m2p, p2m). 쓰는 쪽은 head 만, 읽는 쪽은 tail 만
 *    고친다(단일 생산자/소비자). 쓰는 쪽은 데이터 → __DMB() → head → MU GP0 를 울린다.
 *    GP0 는 여러 번 울려도 하나로 합쳐지므로 받는 쪽은 빌 때까지 다 읽는다.
 *    두 링은 주 코어(CM33)가 상대 코어를 깨우기 전에 비운다.
 *    이 스트림 위에 cmd 패킷(STX 02 FD)을 올린다. 명령 번호는 아래 SHARED_CMD_*.
 *
 * 구조체를 고치면 SHARED_VERSION 을 올리고 두 코어를 함께 다시 빌드한다.
 */
#define SHARED_CM7_IMAGE_ADDR   0x04800000UL      // QSPI CM7 슬롯 (docs/02 5절)
#define SHARED_CM7_IMAGE_MAX    (256U * 1024U)    // CM7 ITCM 크기
#define SHARED_CM7_IMAGE_MAGIC  0x37434D52UL      // "RMC7"
#define SHARED_CM7_TCM_ADDR     0x303C0000UL      // CM33 에서 본 CM7 ITCM (Secure alias)

#define SHARED_BLOCK_ADDR       0x20500000UL      // OCRAM2
#define SHARED_BLOCK_SIZE       0x1000U           // 4 KB. CM7 MPU 가 이 구간을 non-cacheable 로 잡는다 (2^N, 주소는 크기 배수)
#define SHARED_MAGIC            0x48535452UL      // "RTSH"
#define SHARED_VERSION          2

//-- 메시지 id. 0x0000~0x00FF 는 드라이버가 쓰고, 그 위는 앱이 쓴다.
//
#define SHARED_MSG_PING         0x0001U           // CM33 → CM7. data 를 그대로 돌려준다
#define SHARED_MSG_PONG         0x0002U           // CM7 → CM33. PING 의 data
#define SHARED_MSG_USER         0x0100U

#define SHARED_RING_SIZE        1024U             // 2^N. 방향마다 하나

//-- 코어 간 cmd 패킷 명령 번호
//
#define SHARED_CMD_PEER_INFO    0x0001U           // 상대 코어 이름 · 버전 (응답 data = "name\0ver\0")
#define SHARED_CMD_ECHO         0x0002U           // 받은 data 를 그대로 응답한다

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
  volatile uint32_t head;                     // 쓰는 쪽만 고친다
  volatile uint32_t tail;                     // 읽는 쪽만 고친다
  uint8_t           buf[SHARED_RING_SIZE];
} shared_ring_t;


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

  //-- 스트림. m2p = 주 코어 → 상대 코어, p2m = 상대 코어 → 주 코어
  shared_ring_t     m2p;
  shared_ring_t     p2m;

} shared_t;

_Static_assert(sizeof(shared_t) <= SHARED_BLOCK_SIZE, "shared_t must fit in the shared block");
_Static_assert((SHARED_RING_SIZE & (SHARED_RING_SIZE - 1)) == 0, "SHARED_RING_SIZE must be 2^N");


#ifdef __cplusplus
}
#endif

#endif
