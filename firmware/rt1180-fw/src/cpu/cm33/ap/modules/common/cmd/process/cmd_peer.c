#include "process/cmd_peer.h"
#include "cmd_task.h"

#ifdef _USE_HW_CMD
#include "shared.h"
#include <string.h>


/* CM7 에 묻는 쪽. 명령 번호는 shared.h 의 SHARED_CMD_*. 확인은 CLI 'peer' 로 한다. */
#define CMD_PEER_RESP_TIMEOUT 100           // ms


#if CLI_USE(HW_CMD)
static void cliPeer(cli_args_t *args);
#endif


static cmd_t *p_cmd = NULL;




bool cmdPeerInit(cmd_t *p_cmd_ch)
{
  p_cmd = p_cmd_ch;

#if CLI_USE(HW_CMD)
  cliAdd("peer", cliPeer);
#endif
  return true;
}


#if CLI_USE(HW_CMD)
void cliPeer(cli_args_t *args)
{
  bool ret = false;

  //-- 응답을 기다리는 동안 cmd 모듈의 백그라운드 수신이 읽어 가지 않게 잠근다
  //
  if (cmdTaskLock(1000) != true)
  {
    cliPrintf("E_ cmd channel busy\n");
    return;
  }


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cmd_packet_t *p_packet = &p_cmd->packet;
    uint32_t      pre_us   = micros();
    uint32_t      t;

    cmdSendCmdRxResp(p_cmd, SHARED_CMD_PEER_INFO, NULL, 0, CMD_PEER_RESP_TIMEOUT);
    t = micros() - pre_us;
    if (p_packet->err_code == CMD_OK && p_packet->type == PKT_TYPE_RESP)
    {
      const char *p_name = (const char *)p_packet->data;
      const char *p_ver  = p_name + strlen(p_name) + 1;

      cliPrintf("name : %s\n", p_name);
      cliPrintf("ver  : %s\n", p_ver);
      cliPrintf("time : %d us\n", (int)t);
    }
    else
    {
      cliPrintf("E_ err 0x%04X\n", p_packet->err_code);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "log"))
  {
    //-- CM7 의 부팅 로그를 256 B 씩 가져와 그대로 찍는다 (CM7 hw/driver/log.c)
    cmd_packet_t *p_packet = &p_cmd->packet;
    uint32_t      offset   = 0;

    while (1)
    {
      cmdSendCmdRxResp(p_cmd, SHARED_CMD_PEER_LOG, (uint8_t *)&offset, 4, CMD_PEER_RESP_TIMEOUT);
      if (p_packet->err_code != CMD_OK || p_packet->type != PKT_TYPE_RESP)
      {
        cliPrintf("E_ err 0x%04X\n", p_packet->err_code);
        break;
      }
      if (p_packet->length == 0)
        break;
      for (uint32_t i = 0; i < p_packet->length; i++)
      {
        cliPutch(p_packet->data[i]);
      }
      offset += p_packet->length;
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "cpu"))
  {
    //-- CM7 의 idle 비율. 1 초 간격으로 두 번 받아 런타임 카운터 차이로 구한다.
    //   (CM7 은 이 1 초 동안 cmd 스레드가 이 명령 둘을 처리하는 것 말고는 하는 일이 거의 없다)
    cmd_packet_t *p_packet = &p_cmd->packet;
    uint32_t      c0[2], c1[2];
    bool          ok;

    ok  = cmdSendCmdRxResp(p_cmd, SHARED_CMD_PEER_CPU, NULL, 0, CMD_PEER_RESP_TIMEOUT) &&
          p_packet->err_code == CMD_OK && p_packet->length == 8;
    if (ok) memcpy(c0, p_packet->data, 8);
    cmdTaskUnlock();
    delay(1000);
    cmdTaskLock(1000);
    ok &= cmdSendCmdRxResp(p_cmd, SHARED_CMD_PEER_CPU, NULL, 0, CMD_PEER_RESP_TIMEOUT) &&
          p_packet->err_code == CMD_OK && p_packet->length == 8;
    if (ok)
    {
      memcpy(c1, p_packet->data, 8);
      uint32_t idle  = c1[0] - c0[0];
      uint32_t total = c1[1] - c0[1];

      cliPrintf("CM7 idle : %d.%d %%  (1 s, busy %d.%d %%)\n",
                total ? (int)((uint64_t)idle * 100 / total) : 0,
                total ? (int)((uint64_t)idle * 1000 / total % 10) : 0,
                total ? (int)((uint64_t)(total - idle) * 100 / total) : 0,
                total ? (int)((uint64_t)(total - idle) * 1000 / total % 10) : 0);
    }
    else
    {
      cliPrintf("E_ err 0x%04X\n", p_packet->err_code);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "read"))
  {
    cmd_packet_t *p_packet = &p_cmd->packet;
    uint32_t      addr     = (uint32_t)args->getData(1);
    uint32_t      data;

    cmdSendCmdRxResp(p_cmd, SHARED_CMD_MEM_READ, (uint8_t *)&addr, 4, CMD_PEER_RESP_TIMEOUT);
    if (p_packet->err_code == CMD_OK && p_packet->type == PKT_TYPE_RESP && p_packet->length == 4)
    {
      memcpy(&data, p_packet->data, 4);
      cliPrintf("CM7  0x%08X : 0x%08X\n", (unsigned)addr, (unsigned)data);
    }
    else
    {
      cliPrintf("E_ err 0x%04X\n", p_packet->err_code);
    }
    ret = true;
  }

  if (args->argc >= 2 && args->isStr(0, "echo"))
  {
    static uint8_t tx_buf[CMD_MAX_DATA_LENGTH];
    cmd_packet_t  *p_packet = &p_cmd->packet;
    uint32_t       length   = (uint32_t)args->getData(1);
    uint32_t       count    = (args->argc == 3) ? (uint32_t)args->getData(2) : 1;
    uint32_t       ok = 0, bad = 0, err = 0;
    uint32_t       t, t_min = 0xFFFFFFFFU, t_max = 0, t_sum = 0;

    length = constrain(length, 0, CMD_MAX_DATA_LENGTH);

    for (uint32_t i = 0; i < count; i++)
    {
      uint32_t pre_us;

      for (uint32_t j = 0; j < length; j++)
      {
        tx_buf[j] = (uint8_t)(i + j);
      }

      pre_us = micros();
      cmdSendCmdRxResp(p_cmd, SHARED_CMD_ECHO, tx_buf, length, CMD_PEER_RESP_TIMEOUT);
      t = micros() - pre_us;

      if (p_packet->err_code != CMD_OK || p_packet->type != PKT_TYPE_RESP || p_packet->cmd != SHARED_CMD_ECHO)
      {
        err++;
        continue;
      }
      if (p_packet->length != length || memcmp(p_packet->data, tx_buf, length) != 0)
      {
        bad++;
        continue;
      }
      ok++;
      t_sum += t;
      t_min  = (t < t_min) ? t : t_min;
      t_max  = (t > t_max) ? t : t_max;
    }

    cliPrintf("echo %d B x %d : ok %d  bad %d  err %d (last 0x%04X)\n",
              (int)length, (int)count, (int)ok, (int)bad, (int)err, p_packet->err_code);
    if (ok > 0)
    {
      uint32_t avg = t_sum / ok;

      cliPrintf("rtt min %d us  avg %d us  max %d us", (int)t_min, (int)avg, (int)t_max);
      if (avg > 0)
      {
        //-- 왕복이라 data 가 양쪽으로 한 번씩 지나간다
        cliPrintf("  (%d KB/s each way)", (int)((uint64_t)length * 1000000U / avg / 1024U));
      }
      cliPrintf("\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("peer info\n");
    cliPrintf("peer read addr        (CM7 이 읽는다)\n");
    cliPrintf("peer cpu              (CM7 idle 비율, 1 초)\n");
    cliPrintf("peer log              (CM7 부팅 로그)\n");
    cliPrintf("peer echo 0~%d [count]\n", CMD_MAX_DATA_LENGTH);
  }

  cmdTaskUnlock();
}
#endif

#endif
