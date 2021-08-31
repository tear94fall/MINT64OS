#include "RAMDisk.h"
#include "Utility.h"
#include "DynamicMemory.h"

// 램 디스크를 관리하는 자료구조
static RDDMANAGER gs_stRDDManager;

//  램 디스크 디바이스 드라이버 초기화 함수
BOOL kInitializeRDD( DWORD dwTotalSectorCount )
{
    // 자료구조 초기화
    kMemSet( &gs_stRDDManager, 0, sizeof( gs_stRDDManager ) );

    // 램 디스크로 사용할 메모리를 할당
    gs_stRDDManager.pbBuffer = ( BYTE* ) kAllocateMemory( dwTotalSectorCount * 512 );
    if( gs_stRDDManager.pbBuffer == NULL )
    {
        return FALSE;
    }

    // 총 섹터 수와 동기화 객체를 설정
    gs_stRDDManager.dwTotalSectorCount = dwTotalSectorCount;
    kInitializeMutex( &( gs_stRDDManager.stMuex ) );

    return TRUE;
}

//  램 디스크의 정보를 반환
BOOL kReadRDDInformation( BOOL bPrimary, BOOL bMaster, HDDINFORMATION* pstHDDInformation )
{
    // 자료구조 초기화
    kMemSet( pstHDDInformation, 0, sizeof( HDDINFORMATION ) );

    // 총 섹터 수와 시리얼 번호, 모델 번호만 설정
    pstHDDInformation->dwTotalSectors = gs_stRDDManager.dwTotalSectorCount;
    kMemCpy( pstHDDInformation->vwSerialNumber, "0000-0000", 9 );
    kMemCpy( pstHDDInformation->vwModelNumber, "MINT RAM Disk v1.0", 18 );

    return TRUE;
}

//  램 디스크에서 여러 섹터를 읽어서 반환
int kReadRDDSector( BOOL bPrimary, BOOL bMaster, DWORD dwLBA, int iSectorCount, char* pcBuffer )
{
    int iRealReadCount;

    // LBA 어드레스부터 끝까지 읽을 수 있는 섹터 수와 읽어야 할 섹터 수를 비교해서
    // 실제로 읽을 수 있는 수를 계산
    iRealReadCount = MIN( gs_stRDDManager.dwTotalSectorCount - ( dwLBA + iSectorCount ),  iSectorCount );

    // 램 디스크 메모리에서 데이터를 실제로 읽을 섹터 수만큼 복사해서 반환
    kMemCpy( pcBuffer, gs_stRDDManager.pbBuffer + ( dwLBA * 512 ), iRealReadCount * 512 );

    return iRealReadCount;
}

//  램 디스크에 여러 섹터를 씀
int kWriteRDDSector( BOOL bPrimary, BOOL bMaster, DWORD dwLBA, int iSectorCount, char* pcBuffer )
{
    int iRealWriteCount;

    // LBA 어드레스부터 끝까지 쓸 수 있는 섹터 수와 써야 할 섹터 수를 비교해서
    // 실제로 쓸 수 있는 수를 계산
    iRealWriteCount = MIN( gs_stRDDManager.dwTotalSectorCount - ( dwLBA + iSectorCount ), iSectorCount );

    // 데이터를 실제로 쓸 섹터 수만큼 램 디스크 메모리에 복사
    kMemCpy( gs_stRDDManager.pbBuffer + ( dwLBA * 512 ), pcBuffer, iRealWriteCount * 512 );

    return iRealWriteCount;
}