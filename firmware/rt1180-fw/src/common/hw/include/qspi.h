#ifndef QSPI_H_
#define QSPI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_QSPI

/*
 * QSPI NOR. 주소(addr)는 플래시 안의 오프셋이다 (0 ~ qspiGetLength()-1).
 * 메모리 맵 주소는 qspiGetAddr() + addr 이다.
 *
 * 코드가 같은 플래시에서 XIP 로 돌고 있어도 지우고 쓸 수 있다. 그동안 인터럽트가 막힌다.
 * 돌고 있는 펌웨어 영역을 지우지 않는 것은 부르는 쪽의 책임이다.
 */
typedef struct {
  uint32_t FlashSize;          /*!< Size of the flash */
  uint32_t EraseSectorSize;    /*!< Size of sectors for the erase operation */
  uint32_t EraseSectorsNumber; /*!< Number of sectors for the erase operation */
  uint32_t ProgPageSize;       /*!< Size of pages for the program operation */
  uint32_t ProgPagesNumber;    /*!< Number of pages for the program operation */
  uint8_t  device_id[20];
} qspi_info_t;


bool qspiInit(void);
bool qspiIsInit(void);
bool qspiRead(uint32_t addr, uint8_t *p_data, uint32_t length);
bool qspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length);
bool qspiErase(uint32_t addr, uint32_t length);
bool qspiEraseBlock(uint32_t block_addr);
bool qspiEraseSector(uint32_t sector_addr);
bool qspiGetInfo(qspi_info_t* p_info);
uint32_t qspiGetAddr(void);
uint32_t qspiGetLength(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
