/*
 * FlexSPI NOR 부트 헤더 — FCB 와 컨테이너
 *
 * BootROM 이 QSPI 에서 찾는 두 구조체를 여기서 직접 만든다. 서명 도구(spsdk)를
 * 쓰지 않는다. 출하 LIFE_CYCLE(OEM Open) 에서는 인증 오류가 무시되므로 서명 없는
 * 컨테이너로 부팅된다. 근거와 배치는 docs/01-boot-sequence.md 4절.
 *
 *   0x0400_0400  FCB        → 링커 .boot_hdr.conf
 *   0x0400_1000  컨테이너   → 링커 .boot_hdr.container
 *
 * FCB 의 타입 정의는 SDK 보드 헤더(frdmimxrt1186_flexspi_nor_config.h)를 쓰고,
 * 값은 SDK 의 frdmimxrt1186_flexspi_nor_config.c 와 같다 (W25Q128JV, Quad I/O 100 MHz).
 * 컨테이너 구조체는 RM 12.6.2 Table 82~86 을 보고 여기서 정의한다.
 */
#include "frdmimxrt1186_flexspi_nor_config.h"


//-- FCB (Flash Configuration Block, 512 B)
//
//   ROM 은 30 MHz 1-bit 읽기(0x03)로 이 블록을 읽은 뒤, 여기 적힌 LUT 와 클럭으로
//   FlexSPI2 를 다시 설정하고 XIP 를 시작한다. LUT 0 번(읽기)이 XIP 에 쓰인다.
//
#define FLASH_DUMMY_CYCLES  0x06    // W25Q128JV 0xEB : 모드 비트 2 클럭 + 더미 4 클럭

__attribute__((section(".boot_hdr.conf"), used))
const flexspi_nor_config_t boot_fcb =
{
  .memConfig =
  {
    .tag                  = FLEXSPI_CFG_BLK_TAG,       // "FCFB"
    .version              = FLEXSPI_CFG_BLK_VERSION,   // V1.4.0
    .readSampleClkSrc     = kFlexSPIReadSampleClk_LoopbackFromDqsPad,
    .csHoldTime           = 3u,
    .csSetupTime          = 3u,
    .controllerMiscOption = 0x10,
    .deviceType           = kFlexSpiDeviceType_SerialNOR,
    .sflashPadType        = kSerialFlash_4Pads,
    .serialClkFreq        = kFlexSpiSerialClk_100MHz,
    .sflashA1Size         = 16u * 1024u * 1024u,
    .configModeType[0]    = kDeviceConfigCmdType_Generic,

    .lookupTable =
    {
      // [0] 읽기 : Quad I/O Fast Read 0xEB, 24bit 주소 4 pad
      [0]          = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0xEB, RADDR_SDR, FLEXSPI_4PAD, 0x18),
      [1]          = FLEXSPI_LUT_SEQ(DUMMY_SDR, FLEXSPI_4PAD, FLASH_DUMMY_CYCLES, READ_SDR, FLEXSPI_4PAD, 0x04),

      // [1] 상태 읽기 0x05
      [4 * 1 + 0]  = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0x05, READ_SDR, FLEXSPI_1PAD, 0x04),

      // [3] 쓰기 허가 0x06
      [4 * 3 + 0]  = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0x06, STOP, FLEXSPI_1PAD, 0x0),

      // [5] 섹터(4 KB) 지우기 0x20
      [4 * 5 + 0]  = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0x20, RADDR_SDR, FLEXSPI_1PAD, 0x18),

      // [8] 블록(64 KB) 지우기 0xD8
      [4 * 8 + 0]  = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0xD8, RADDR_SDR, FLEXSPI_1PAD, 0x18),

      // [9] 페이지 쓰기 0x02
      [4 * 9 + 0]  = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0x02, RADDR_SDR, FLEXSPI_1PAD, 0x18),
      [4 * 9 + 1]  = FLEXSPI_LUT_SEQ(WRITE_SDR, FLEXSPI_1PAD, 0x04, STOP, FLEXSPI_1PAD, 0x0),

      // [11] 칩 전체 지우기 0x60
      [4 * 11 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR,   FLEXSPI_1PAD, 0x60, STOP, FLEXSPI_1PAD, 0x0),
    },
  },
  .pageSize           = 256u,
  .sectorSize         = 4u * 1024u,
  .ipcmdSerialClkFreq = 0x1,
  .blockSize          = 64u * 1024u,
  .isUniformBlockSize = false,
};


//-- 컨테이너 (RM 12.6.2)
//
//   헤더 16 B + 이미지 엔트리 128 B + 서명 블록 16 B = 0xA0 B.
//   이미지 하나, 서명 없음. 공장 데모 이미지와 같은 모양이다.
//
typedef struct __attribute__((packed))
{
  uint8_t  version;           // 0x00
  uint16_t length;            // 컨테이너 전체 길이 (서명 블록 포함)
  uint8_t  tag;               // 0x87
  uint32_t flags;             // [1:0] SRK set : 0 = 인증하지 않음
  uint16_t sw_version;
  uint8_t  fuse_version;
  uint8_t  num_images;
  uint16_t sign_block_offset; // 컨테이너 시작 기준
  uint16_t reserved;
} cnt_header_t;               // RM Table 82

typedef struct __attribute__((packed))
{
  uint32_t offset;            // 컨테이너 시작 기준 이미지 위치
  uint32_t size;
  uint32_t load_addr;         // 64 bit 중 하위
  uint32_t load_addr_hi;
  uint32_t entry;             // 64 bit 중 하위
  uint32_t entry_hi;
  uint32_t flags;
  uint32_t metadata;
  uint8_t  hash[64];          // 이미지가 하나면 ELE 가 검증하지 않는다 (RM 12.6.2 NOTE)
  uint8_t  iv[32];
} cnt_image_t;                // RM Table 84

typedef struct __attribute__((packed))
{
  uint8_t  version;           // 0x00
  uint16_t length;
  uint8_t  tag;               // 0x90
  uint16_t cert_offset;       // 0 = 없음
  uint16_t srk_offset;        // 0 = SRK 테이블 없음 (인증하지 않는 컨테이너)
  uint16_t sign_offset;
  uint16_t blob_offset;       // 0 = 암호화 안 함
  uint32_t reserved;
} cnt_sign_block_t;           // RM Table 86

typedef struct __attribute__((packed))
{
  cnt_header_t     header;
  cnt_image_t      image[1];
  cnt_sign_block_t sign_block;
} boot_container_t;

_Static_assert(sizeof(cnt_header_t)     == 0x10, "container header");
_Static_assert(sizeof(cnt_image_t)      == 0x80, "image entry");
_Static_assert(sizeof(cnt_sign_block_t) == 0x10, "signature block");


//-- 이미지 플래그 (RM Table 85)
//
#define IMG_TYPE_EXECUTABLE   (0x3u << 0)
#define IMG_CORE_CM33         (0x1u << 4)
#define IMG_HASH_SHA512       (0x2u << 8)


//-- 링커가 계산하는 값 (bsp/ldscript/rt1180-fw-cm33.ld)
//
//   주소가 곧 값인 절대 심볼이다. &심볼 로 읽는다.
//
extern uint32_t __CONTAINER_IMG_OFFSET[];   // 벡터 테이블 - 컨테이너 시작 = 0xA000
extern uint32_t __CONTAINER_IMG_SIZE[];     // 벡터부터 마지막 로드 바이트까지
extern uint32_t __VECTOR_TABLE[];           // 0x0400_B000 (XIP : load = entry = 여기)


__attribute__((section(".boot_hdr.container"), used))
const boot_container_t boot_container =
{
  .header =
  {
    .version           = 0x00,
    .length            = sizeof(boot_container_t),
    .tag               = 0x87,
    .flags             = 0x00000000,
    .sw_version        = 0,
    .fuse_version      = 0,
    .num_images        = 1,
    .sign_block_offset = sizeof(cnt_header_t) + 1 * sizeof(cnt_image_t),
  },
  .image =
  {
    {
      .offset    = (uint32_t)__CONTAINER_IMG_OFFSET,
      .size      = (uint32_t)__CONTAINER_IMG_SIZE,
      .load_addr = (uint32_t)__VECTOR_TABLE,
      .entry     = (uint32_t)__VECTOR_TABLE,
      .flags     = IMG_TYPE_EXECUTABLE | IMG_CORE_CM33 | IMG_HASH_SHA512,
    },
  },
  .sign_block =
  {
    .version = 0x00,
    .length  = sizeof(cnt_sign_block_t),
    .tag     = 0x90,
  },
};
