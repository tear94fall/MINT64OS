#ifndef __GUITASK_H__
#define __GUITASK_H__

#include "Types.h"
#include "Window.h"

// 매크로
// 태스크가 보내는 유저 이벤트 타입 정의
#define EVENT_USER_TESTMESSAGE              0x80000001

// 시스템 모니터 태스크 매크로
// 프로세서 정보를 표시하는 막대의 너비
#define SYSTEMMONITOR_PROCESSOR_WIDTH       150
// 프로세서 정보를 표시하는 영역과 영역 사이의 여백
#define SYSTEMMONITOR_PROCESSOR_MARGIN      20
// 프로세서 정보를 표시하는 영역의 높이
#define SYSTEMMONITOR_PROCESSOR_HEIGHT      150
// 시스템 모니터 윈도우의 높이
#define SYSTEMMONITOR_WINDOW_HEIGHT         310
// 메모리 정보를 표시하는 영역의 높이
#define SYSTEMMONITOR_MEMORY_HEIGHT         100
// 막대의 색깔
#define SYSTEMMONITOR_BAR_COLOR             RGB( 55, 215, 47 )

//  함수
//  기본 GUI 태스크와 Hello World GUI 태스크
void kBaseGUITask( void );
void kHelloWorldGUITask( void );

// 시스템 모니터 태스크 함수
void kSystemMonitorTask( void );
static void kDrawProcessorInformation( QWORD qwWindowID, int iX, int iY, BYTE bAPICID );
static void kDrawMemoryInformation( QWORD qwWindowID, int iY, int iWindowWidth );

// GUI 콘솔 쉘 태스크 함수
void kGUIConsoleShellTask( void );
static void kProcessConsoleBuffer( QWORD qwWindowID );

// 이미지 뷰어 태스크 함수
void kImageViewerTask( void );
static void kDrawFileName( QWORD qwWindowID, RECT* pstArea, char *pcFileName, int iNameLength );
static BOOL kCreateImageViewerWindowAndExecute( QWORD qwMainWindowID, const char* pcFileName );

#endif /*__GUITASK_H__*/