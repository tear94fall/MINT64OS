#ifndef __GUITASK_H__
#define __GUITASK_H__

#include "Types.h"

// 매크로
// 태스크가 보내는 유저 이벤트 타입 정의
#define EVENT_USER_TESTMESSAGE              0x80000001

//  함수
void kBaseGUITask( void );
void kHelloWorldGUITask( void );

#endif /*__GUITASK_H__*/