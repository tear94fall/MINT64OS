#ifndef __ASSEMBLYUTILITY_H__
#define __ASSEMBLYUTITLIY_H__

#include "Types.h"
#include "Task.h"

// 함수
BYTE kInPortByte( WORD wPort );
void kOutPortByte( WORD wPort, BYTE bData );
void kLoadGDTR( QWORD qwGDTRAddress );
void kLoadTR( WORD wTSSSegmentOffset );
void kLoadIDTR( QWORD qwIDTRAddress );
void kEnableInterrupt( void );
void kDisableInterrupt( void );
QWORD kReadRFLAGS( void );
QWORD kReadTSC( void );
void kSwitchContext( CONTEXT* pstCurrentContext, CONTEXT* pstNextContext );
void kHlt( void );

#endif /*__ASSEMBLYUTILITY_H__*/