#ifndef __UTILITY_H__
#define __UTILTIY_H__

#include "Types.h"

//  함수
void kMemSet( void* pvDestination, BYTE bData, int iSize );
int kMemCpy( void* pvDestination, const void* pvSource, int iSize );
int kMemCmp( const void* pvDestination, const void* pvSource, int iSize );
BOOL kSetInterruptFlag( BOOL bEnableInterrupt );

#endif /*__UTILITY_H__*/