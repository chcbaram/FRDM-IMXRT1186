#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "ipc.h"
#include "psram.h"
#include "cmd.h"
#include "log.h"
#include "cli.h"                    // CLI_USE() 매크로만 쓴다. CM7 에는 CLI 가 없다 (_USE_HW_CLI 없음)
#include "osal/thread.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
