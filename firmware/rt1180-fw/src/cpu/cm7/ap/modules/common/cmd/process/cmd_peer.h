#ifndef CMD_PEER_H_
#define CMD_PEER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_CMD

bool cmdPeerProcess(cmd_t *p_cmd);

#endif

#ifdef __cplusplus
}
#endif

#endif
