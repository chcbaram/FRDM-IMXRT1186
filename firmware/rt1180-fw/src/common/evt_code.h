#ifndef EVT_CODE_H_
#define EVT_CODE_H_


/*
 * 시스템 이벤트 코드.
 *
 * 발행하는 쪽은 무엇이 받는지 모르고, 받는 쪽은 누가 보냈는지 모른다.
 * 예를 들어 이더넷 드라이버는 링크가 붙으면 EVENT_ETH_LINK 를 던지기만 하고,
 * 그걸 LED 로 보여줄지 로그로 남길지는 각 모듈이 알아서 정한다.
 */
typedef enum
{
  EVENT_NONE = 0,

  EVENT_BOOT_DONE,        // 부팅 완료
  EVENT_ERROR,            // data: err_code

  EVENT_ETH_LINK,         // data: 1 = up, 0 = down
  EVENT_ETH_IP,           // data: IPv4 주소
  EVENT_USB_CONNECT,      // data: 1 = connect, 0 = disconnect
  EVENT_SD_DETECT,        // data: 1 = insert, 0 = remove
  EVENT_BUTTON,           // data: 버튼 번호

  EVENT_MAX,
} EventCode_t;


#endif
