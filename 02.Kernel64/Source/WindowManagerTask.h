#ifndef __WINDOWMANAGER_H__
#define __WINDOWMANAGER_H__

// 매크로
// 윈도우 매니저 태스크가 처리할 데이터나 이벤트를 통합하는 최대 개수
#define WINDOWMANAGER_DATAACCUMULATECOUNT       20

// 함수
void kStartWindowManager( void );
BOOL kProcessMouseData( void );
BOOL kProcessKeyData( void );
BOOL kProcessEventQueueData( void );

#endif /*__WINDOWMANAGER_H__*/