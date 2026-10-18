#include "qspi.h"


#ifdef _USE_HW_QSPI
#include "cli.h"
#include "log.h"
#include "fsl_romapi.h"
#include <string.h>

/*
 * QSPI NOR — W25Q128JV, FlexSPI2 (docs/26-qspi-nor.md)
 *
 * BootROM 의 FlexSPI NOR API 를 부른다. probe-rs 플래시 알고리즘과 SDK fsl_romapi 예제가
 * 쓰는 길과 같다 (docs/10 6절). SDK 예제는 XIP 로 돌면서 같은 플래시를 지우고 쓴다.
 *
 *   설정 블록 : 부팅에 쓴 FCB(0x0400_0400) 를 RAM 으로 복사해 그대로 넘긴다. 부팅 때와 같은
 *               LUT · 클럭이 되고, 지우기(5, 8) · 쓰기(9) 시퀀스도 FCB 에 들어 있다.
 *   인터럽트  : ROM 함수가 지우고 쓰는 동안 ISR 이 플래시에서 명령어를 가져오면 안 된다.
 *               ROM 호출마다 막는다. 섹터 지우기(4 KB) 는 수십 ms 걸리고 그동안 SysTick 도 멈춘다.
 *   쓰기 원본 : ROM 에 넘기는 원본은 RAM 이어야 한다. 페이지 버퍼(256 B)로 옮기고, 페이지를
 *               다 채우지 않는 쪽은 0xFF 로 둔다 (NOR 는 1 을 써도 바뀌지 않는다).
 *   읽기      : 메모리 맵(0x0400_0000) 에서 바로 읽는다. 지우거나 쓴 뒤에는 FlexSPI AHB 버퍼와
 *               CM33 코드 버스 캐시(XCACHE_PC) 를 비운다.
 */

#define QSPI_INSTANCE         2U            // ROM API 의 FlexSPI2
#define QSPI_XIP_BASE         0x04000000UL  // FlexSPI2 AHB 창 (NS 주소, 링커와 같다)
#define QSPI_FCB_ADDR         (QSPI_XIP_BASE + 0x400U)
#define QSPI_PAGE_MAX         256U


#if CLI_USE(HW_QSPI)
static void cliQspi(cli_args_t *args);
#endif
static bool qspiReadId(uint8_t *p_id);
static void qspiCacheClear(void);


static bool                 is_init = false;
static flexspi_nor_config_t nor_cfg;
static uint8_t              device_id[4];
static uint32_t             page_buf[QSPI_PAGE_MAX / 4];
static status_t             last_status = kStatus_Success;




bool qspiInit(void)
{
  uint32_t primask;

  _Static_assert(sizeof(flexspi_nor_config_t) == 512, "FCB is 512 B");

  is_init = false;

  ROM_API_Init();
  memcpy(&nor_cfg, (const void *)QSPI_FCB_ADDR, sizeof(nor_cfg));

  if (nor_cfg.memConfig.tag == FLEXSPI_CFG_BLK_TAG)
  {
    primask = __get_PRIMASK();
    __disable_irq();
    last_status = ROM_FLEXSPI_NorFlash_Init(QSPI_INSTANCE, &nor_cfg);
    ROM_FLEXSPI_NorFlash_ClearCache(QSPI_INSTANCE);
    qspiCacheClear();
    __set_PRIMASK(primask);

    if (last_status == kStatus_Success && qspiReadId(device_id) == true)
    {
      is_init = (device_id[0] == kSerialFlash_Winbond_ManufacturerID);
    }
  }

  logPrintf("[%s] qspiInit()\n", is_init ? "OK":"E_");
  if (is_init)
  {
    logPrintf("     W25Q128JV  ID %02X %02X %02X  %d KB\n",
              device_id[0], device_id[1], device_id[2], (int)(qspiGetLength() / 1024));
  }

#if CLI_USE(HW_QSPI)
  cliAdd("qspi", cliQspi);
#endif
  return is_init;
}

bool qspiIsInit(void)
{
  return is_init;
}

bool qspiRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (addr + length > qspiGetLength())
    return false;

  memcpy(p_data, (const void *)(QSPI_XIP_BASE + addr), length);
  return true;
}

bool qspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  uint32_t page = nor_cfg.pageSize;
  uint32_t primask;

  if (is_init != true || addr + length > qspiGetLength())
    return false;

  while (length > 0)
  {
    uint32_t page_addr = addr & ~(page - 1);
    uint32_t offset    = addr - page_addr;
    uint32_t n         = page - offset;

    if (n > length)
      n = length;

    memset(page_buf, 0xFF, page);
    memcpy((uint8_t *)page_buf + offset, p_data, n);

    primask = __get_PRIMASK();
    __disable_irq();
    last_status = ROM_FLEXSPI_NorFlash_ProgramPage(QSPI_INSTANCE, &nor_cfg, page_addr, page_buf);
    ROM_FLEXSPI_NorFlash_ClearCache(QSPI_INSTANCE);
    qspiCacheClear();
    __set_PRIMASK(primask);

    if (last_status != kStatus_Success)
      return false;

    addr   += n;
    p_data += n;
    length -= n;
  }
  return true;
}

bool qspiErase(uint32_t addr, uint32_t length)
{
  uint32_t sector = nor_cfg.sectorSize;
  uint32_t block  = nor_cfg.blockSize;
  uint32_t end;

  if (is_init != true || length == 0 || addr + length > qspiGetLength())
    return false;

  //-- 섹터 단위로 넓혀 지운다. 64 KB 로 맞는 구간은 블록 지우기로 한다 (훨씬 빠르다).
  //
  end  = (addr + length + sector - 1) & ~(sector - 1);
  addr = addr & ~(sector - 1);

  while (addr < end)
  {
    bool ret;

    if ((addr & (block - 1)) == 0 && (end - addr) >= block)
    {
      ret   = qspiEraseBlock(addr);
      addr += block;
    }
    else
    {
      ret   = qspiEraseSector(addr);
      addr += sector;
    }
    if (ret != true)
      return false;
  }
  return true;
}

bool qspiEraseBlock(uint32_t block_addr)
{
  uint32_t primask;

  if (is_init != true || block_addr >= qspiGetLength())
    return false;

  primask = __get_PRIMASK();
  __disable_irq();
  last_status = ROM_FLEXSPI_NorFlash_EraseBlock(QSPI_INSTANCE, &nor_cfg, block_addr);
  ROM_FLEXSPI_NorFlash_ClearCache(QSPI_INSTANCE);
  qspiCacheClear();
  __set_PRIMASK(primask);

  return (last_status == kStatus_Success);
}

bool qspiEraseSector(uint32_t sector_addr)
{
  uint32_t primask;

  if (is_init != true || sector_addr >= qspiGetLength())
    return false;

  primask = __get_PRIMASK();
  __disable_irq();
  last_status = ROM_FLEXSPI_NorFlash_EraseSector(QSPI_INSTANCE, &nor_cfg, sector_addr);
  ROM_FLEXSPI_NorFlash_ClearCache(QSPI_INSTANCE);
  qspiCacheClear();
  __set_PRIMASK(primask);

  return (last_status == kStatus_Success);
}

bool qspiGetInfo(qspi_info_t* p_info)
{
  if (is_init != true)
    return false;

  memset(p_info, 0, sizeof(qspi_info_t));
  p_info->FlashSize          = qspiGetLength();
  p_info->EraseSectorSize    = nor_cfg.sectorSize;
  p_info->EraseSectorsNumber = qspiGetLength() / nor_cfg.sectorSize;
  p_info->ProgPageSize       = nor_cfg.pageSize;
  p_info->ProgPagesNumber    = qspiGetLength() / nor_cfg.pageSize;
  memcpy(p_info->device_id, device_id, sizeof(device_id));
  return true;
}

uint32_t qspiGetAddr(void)
{
  return QSPI_XIP_BASE;
}

uint32_t qspiGetLength(void)
{
  return nor_cfg.memConfig.sflashA1Size;
}

static bool qspiReadId(uint8_t *p_id)
{
  uint32_t       lut_seq[4] = {0, };
  uint32_t       id = 0;
  flexspi_xfer_t xfer;
  uint32_t       primask;

  //-- JEDEC ID 0x9F. FCB 가 비워 둔 LUT 7 번(NOR_CMD_LUT_SEQ_IDX_READID)에 넣는다.
  //
  lut_seq[0] = FSL_ROM_FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x9F, READ_SDR, FLEXSPI_1PAD, 0x04);

  xfer.operation            = kFLEXSPIOperation_Read;
  xfer.seqId                = NOR_CMD_LUT_SEQ_IDX_READID;
  xfer.seqNum               = 1U;
  xfer.baseAddress          = 0U;
  xfer.isParallelModeEnable = false;
  xfer.txBuffer             = NULL;
  xfer.txSize               = 0;
  xfer.rxBuffer             = &id;
  xfer.rxSize               = 4U;

  primask = __get_PRIMASK();
  __disable_irq();
  last_status = ROM_FLEXSPI_NorFlash_UpdateLut(QSPI_INSTANCE, NOR_CMD_LUT_SEQ_IDX_READID, lut_seq, 1U);
  if (last_status == kStatus_Success)
  {
    last_status = ROM_FLEXSPI_NorFlash_CommandXfer(QSPI_INSTANCE, &xfer);
  }
  __set_PRIMASK(primask);

  memcpy(p_id, &id, 4);
  return (last_status == kStatus_Success);
}

static void qspiCacheClear(void)
{
  //-- CM33 코드 버스 캐시를 통째로 무효화한다 (system_MIMXRT1186_cm33.c 와 같은 방법).
  //   쓰기 버퍼가 없는 읽기 캐시라 버릴 것이 없다.
  //
  if ((XCACHE_PC->CCR & XCACHE_CCR_ENCACHE_MASK) != 0U)
  {
    XCACHE_PC->CCR |= XCACHE_CCR_INVW0_MASK | XCACHE_CCR_INVW1_MASK | XCACHE_CCR_GO_MASK;
    while ((XCACHE_PC->CCR & XCACHE_CCR_GO_MASK) != 0U)
    {
    }
  }
  __DSB();
  __ISB();
}


#if CLI_USE(HW_QSPI)
/*
 * CLI 의 지우기 · 쓰기는 데이터 영역(0x00C0_0000~, docs/02 5절)만 허용한다.
 * 부트 슬롯 · 앱 · CM7 슬롯을 시험하다 지우지 않게.
 * 시간은 DWT 사이클로 잰다. 지우는 동안 인터럽트가 막혀 SysTick(millis) 이 밀린다.
 */
#define QSPI_CLI_WR_MIN       0x00C00000UL

static uint32_t cycStart(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  return DWT->CYCCNT;
}

static uint32_t cycToUs(uint32_t start)
{
  return (DWT->CYCCNT - start) / (SystemCoreClock / 1000000U);
}

static bool cliWrAllowed(uint32_t addr, uint32_t length)
{
  if (addr < QSPI_CLI_WR_MIN || addr + length > qspiGetLength())
  {
    cliPrintf("E_ CLI 는 0x%08X ~ 0x%08X 만 지우고 쓴다\n", (unsigned)QSPI_CLI_WR_MIN, (unsigned)qspiGetLength());
    return false;
  }
  return true;
}

void cliQspi(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    qspi_info_t info;

    cliPrintf("init      : %s (rom status %d)\n", is_init ? "OK" : "E_", (int)last_status);
    if (qspiGetInfo(&info))
    {
      cliPrintf("id        : %02X %02X %02X\n", info.device_id[0], info.device_id[1], info.device_id[2]);
      cliPrintf("size      : %d KB @ 0x%08X\n", (int)(info.FlashSize / 1024), (unsigned)qspiGetAddr());
      cliPrintf("sector    : %d B x %d\n", (int)info.EraseSectorSize, (int)info.EraseSectorsNumber);
      cliPrintf("block     : %d B\n", (int)nor_cfg.blockSize);
      cliPrintf("page      : %d B\n", (int)info.ProgPageSize);
      cliPrintf("cli write : 0x%08X ~\n", (unsigned)QSPI_CLI_WR_MIN);
    }
    ret = true;
  }

  if (args->argc >= 2 && args->isStr(0, "read"))
  {
    uint32_t addr   = (uint32_t)args->getData(1);
    uint32_t length = (args->argc == 3) ? (uint32_t)args->getData(2) : 64;
    uint8_t  buf[16];

    for (uint32_t i = 0; i < length; i += 16)
    {
      uint32_t n = (length - i) < 16 ? (length - i) : 16;

      if (qspiRead(addr + i, buf, n) != true)
      {
        cliPrintf("E_ read 0x%08X\n", (unsigned)(addr + i));
        break;
      }
      cliPrintf("0x%08X :", (unsigned)(addr + i));
      for (uint32_t j = 0; j < n; j++)
      {
        cliPrintf(" %02X", buf[j]);
      }
      cliPrintf("\n");
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "erase"))
  {
    uint32_t addr   = (uint32_t)args->getData(1);
    uint32_t length = (uint32_t)args->getData(2);
    uint32_t t;

    if (cliWrAllowed(addr, length))
    {
      t = cycStart();
      bool ok = qspiErase(addr, length);
      cliPrintf("erase 0x%08X %d B : %s  %d us\n", (unsigned)addr, (int)length, ok ? "OK" : "E_", (int)cycToUs(t));
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write"))
  {
    uint32_t addr = (uint32_t)args->getData(1);
    uint32_t data = (uint32_t)args->getData(2);

    if (cliWrAllowed(addr, 4))
    {
      bool ok = qspiWrite(addr, (uint8_t *)&data, 4);
      cliPrintf("write 0x%08X 0x%08X : %s\n", (unsigned)addr, (unsigned)data, ok ? "OK" : "E_");
    }
    ret = true;
  }

  if (args->argc >= 1 && args->isStr(0, "test"))
  {
    static uint8_t buf[4096];
    uint32_t addr = (args->argc == 2) ? (uint32_t)args->getData(1) : QSPI_CLI_WR_MIN;
    uint32_t sector = nor_cfg.sectorSize;
    uint32_t t, bad;
    bool     ok;

    addr &= ~(nor_cfg.blockSize - 1);
    if (cliWrAllowed(addr, nor_cfg.blockSize))
    {
      //-- 1. 섹터 지우기 → 전부 0xFF
      t  = cycStart();
      ok = qspiEraseSector(addr);
      cliPrintf("erase sector 4 KB   : %s  %6d us\n", ok ? "OK" : "E_", (int)cycToUs(t));
      qspiRead(addr, buf, sector);
      bad = 0;
      for (uint32_t i = 0; i < sector; i++) bad += (buf[i] != 0xFF);
      cliPrintf("  blank check       : %s  (%d B not FF)\n", bad == 0 ? "OK" : "E_", (int)bad);

      //-- 2. 4 KB 쓰기 → 읽어 비교
      for (uint32_t i = 0; i < sector; i++) buf[i] = (uint8_t)(i * 7 + (i >> 8));
      t  = cycStart();
      ok = qspiWrite(addr, buf, sector);
      cliPrintf("write 4 KB          : %s  %6d us\n", ok ? "OK" : "E_", (int)cycToUs(t));
      bad = 0;
      for (uint32_t i = 0; i < sector; i++) bad += (((volatile uint8_t *)(qspiGetAddr() + addr))[i] != (uint8_t)(i * 7 + (i >> 8)));
      cliPrintf("  verify            : %s  (%d B differ)\n", bad == 0 ? "OK" : "E_", (int)bad);

      //-- 3. 읽기 속도 (메모리 맵, 64 KB)
      t = cycStart();
      for (uint32_t i = 0; i < nor_cfg.blockSize; i += sizeof(buf)) qspiRead(addr + i, buf, sizeof(buf));
      uint32_t us = cycToUs(t);
      cliPrintf("read 64 KB          : OK  %6d us  (%d KB/s)\n", (int)us, us ? (int)(64U * 1000000U / us) : 0);

      //-- 4. 블록 지우기 64 KB → 다시 0xFF
      t  = cycStart();
      ok = qspiEraseBlock(addr);
      cliPrintf("erase block 64 KB   : %s  %6d us\n", ok ? "OK" : "E_", (int)cycToUs(t));
      qspiRead(addr, buf, sector);
      bad = 0;
      for (uint32_t i = 0; i < sector; i++) bad += (buf[i] != 0xFF);
      cliPrintf("  blank check       : %s  (%d B not FF)\n", bad == 0 ? "OK" : "E_", (int)bad);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("qspi info\n");
    cliPrintf("qspi read  addr [length]\n");
    cliPrintf("qspi erase addr length\n");
    cliPrintf("qspi write addr data32\n");
    cliPrintf("qspi test  [addr]          (0x%08X~, 64 KB 를 지운다)\n", (unsigned)QSPI_CLI_WR_MIN);
  }
}
#endif

#endif
