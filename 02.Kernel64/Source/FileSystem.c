#include "FileSystem.h"
#include "HardDisk.h"
#include "DynamicMemory.h"
#include "Task.h"
#include "Utility.h"
#include "CacheManager.h"
#include "RAMDisk.h"

// 파일 시스템 자료구조
static FILESYSTEMMANAGER gs_stFileSystemManager;
// 파일 시스템 임시 버퍼
static BYTE gs_vbTempBuffer[ FILESYSTEM_SECTORSPERCLUSTER * 512 ];

// 하드 디스크 제어에 관련된 함수 포인터 선언
fReadHDDInformation gs_pfReadHDDInformation = NULL;
fReadHDDSector gs_pfReadHDDSector = NULL;
fWriteHDDSector gs_pfWriteHDDSector = NULL;

//  파일 시스템을 초기화
BOOL kInitializeFileSystem( void )
{
    BOOL bCacheEnable = FALSE;

    // 자료구조 초기화와 동기화 객체 초기화
    kMemSet( &gs_stFileSystemManager, 0, sizeof( gs_stFileSystemManager ) );
    kInitializeMutex( &( gs_stFileSystemManager.stMutex ) );

    // 하드 디스크를 초기화
    if( kInitializeHDD() == TRUE )
    {
        // 초기화가 성공하면 함수 포인터를 하드 디스크용 함수로 설정
        gs_pfReadHDDInformation = kReadHDDInformation;
        gs_pfReadHDDSector = kReadHDDSector;
        gs_pfWriteHDDSector = kWriteHDDSector;

        // 캐시를 활성화함
        bCacheEnable = TRUE;
    }
    else if( kInitializeRDD( RDD_TOTALSECTORCOUNT ) == TRUE )
    {
        // 초기화가 성공하면 함수 포인터를 램 디스크용 함수로 설정
        gs_pfReadHDDInformation = kReadRDDInformation;
        gs_pfReadHDDSector = kReadRDDSector;
        gs_pfWriteHDDSector = kWriteRDDSector;

        // 램 디스크는 데이터가 남아 있지 않으므로 매번 파일 시스템을 생성함
        if( kFormat() == FALSE )
        {
            return TRUE;
        }
    }
    else
    {
        return FALSE;
    }

    // 파일 시스템 연결
    if( kMount() == FALSE )
    {
        return FALSE;
    }

    // 핸들을 위한 공간을 할당
    gs_stFileSystemManager.pstHandlePool = ( FILE* ) kAllocateMemory( FILESYSTEM_HANDLE_MAXCOUNT * sizeof( FILE ) );

    // 메모리 할당이 실패하면 하드 디스크가 인식되지 않은 것으로 설정
    if( gs_stFileSystemManager.pstHandlePool == NULL )
    {
        gs_stFileSystemManager.bMounted = FALSE;
        return FALSE;
    }

    // 핸들 풀을 모두 0으로 설정하여 초기화
    kMemSet( gs_stFileSystemManager.pstHandlePool, 0, FILESYSTEM_HANDLE_MAXCOUNT * sizeof( FILE ) );

    // 캐시 활성화
    if( bCacheEnable == TRUE )
    {
        gs_stFileSystemManager.bCacheEnable = kInitializeCacheManager();
    }

    return TRUE;
}

//==============================================================================
//  저수준 함수(Low Level Function )
//==============================================================================
//  하드 디스크의 MBR을 읽어서 MINT 파일 시스템인지 확인
//      MINT 파일 시스템이라면 파일 시스템에 관련된 각종 정보를 읽어서 자료구조에 삽입
BOOL kMount( void )
{
    MBR* pstMBR;

    // 동기화 처리
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // MBR을 읽음
    if( gs_pfReadHDDSector( TRUE, TRUE, 0, 1, gs_vbTempBuffer ) == FALSE )
    {
        // 동기화 처리
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return FALSE;
    }

    // 시그니처를 확인 하여 같다면 자료구조에 각 영역에 대한 정보 삽입
    pstMBR = ( MBR* ) gs_vbTempBuffer;
    if( pstMBR->dwSignature != FILESYSTEM_SIGNATURE )
    {
        // 동기화 처리
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return FALSE;
    }

    // 파일 시스템 인식 성공
    gs_stFileSystemManager.bMounted = TRUE;

    // 각 영역의 시작 LBA 어드레스와 섹터 수를 계산
    gs_stFileSystemManager.dwReservedSectorCount = pstMBR->dwReservedSectorCount;
    gs_stFileSystemManager.dwClusterLinkAreaStartAddress = pstMBR->dwReservedSectorCount + 1;
    gs_stFileSystemManager.dwClusterLinkAreaSize = pstMBR->dwClusterLinkSectorCount;
    gs_stFileSystemManager.dwDataAreaStartAddress = pstMBR->dwReservedSectorCount + pstMBR->dwClusterLinkSectorCount + 1;
    gs_stFileSystemManager.dwTotalClusterCount = pstMBR->dwTotalClusterCount;

    // 동기화 처리
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return TRUE;
}

//  하드 디스크에 파일 시스템을 생성
BOOL kFormat( void )
{
    HDDINFORMATION* pstHDD;
    MBR* pstMBR;
    DWORD dwTotalSectorCount, dwRemainSectorCount;
    DWORD dwMaxClusterCount, dwClusterCount;
    DWORD dwClusterLinkSectorCount;
    DWORD i;

    // 동기화 처리
    kLock( &( gs_stFileSystemManager.stMutex ) );

    //==============================================================================
    //  하드 디스크 정보를 읽어서 메타 영역의 크기와 클러스터의 개수를 계산
    //==============================================================================
    // 하드 디스크의 정보를 얻어서 하드 디스크의 총 섹터 수를 구함
    pstHDD = ( HDDINFORMATION* ) gs_vbTempBuffer;
    if( gs_pfReadHDDInformation( TRUE, TRUE, pstHDD ) == FALSE )
    {
        // 동기화 처리
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return FALSE;
    }
    dwTotalSectorCount = pstHDD->dwTotalSectors;

    // 전체 섹터 수를 4KB, 즉 클러스터 크기로 나누어 최대 클러스터 수를 계산
    dwMaxClusterCount = dwTotalSectorCount / FILESYSTEM_SECTORSPERCLUSTER;

    // 최대 클러스터의 수에 맞춰 클러스터 링크 테이블의 섹터 수를 계산
    // 링크 데이터는 4바이트이므로, 한 섹터에는 128개가 들어감, 따라서 총 개수를
    // 128로 나눈 후 올림하여 클러스터 링크의 섹터 수를 구함
    dwClusterLinkSectorCount = ( dwMaxClusterCount + 127 ) / 128;

    // 예약된영역은 현재 사용하지 않으므로, 디스크 전체 영역에서 MBR 영역과 클러스터
    // 링크 테이블 영역의 크기를 뺀 나머지가 실제 데이터 영역이 됨
    // 해당 영역을 클러스터 크기로 나누어 실제 클러스터의 개수를 구함
    dwRemainSectorCount = dwTotalSectorCount - dwClusterLinkSectorCount - 1;
    dwClusterCount = dwRemainSectorCount / FILESYSTEM_SECTORSPERCLUSTER;

    // 실제 사용 가능한 클러스터 수에 맞춰 다시 한 번 계산
    dwClusterLinkSectorCount = ( dwClusterCount + 127 ) / 128;

    //===========================================================================================
    // 계산된 정보를 MBR에 덮어 쓰고, 루트 디렉토리 영역까지 모두 0으로 초기화하여 파일 시스템을 생성
    //===========================================================================================
    // MBR 영역 읽기
    if( gs_pfReadHDDSector( TRUE, TRUE, 0, 1, gs_vbTempBuffer ) == FALSE )
    {
        // 동기화 처리
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return FALSE;
    }

    // 파티션 정보와 파일 시스템 정보 설정
    pstMBR = ( MBR* ) gs_vbTempBuffer;
    kMemSet( pstMBR->vstPartition, 0, sizeof( pstMBR->vstPartition ) );
    pstMBR->dwSignature = FILESYSTEM_SIGNATURE;
    pstMBR->dwReservedSectorCount = 0;
    pstMBR->dwClusterLinkSectorCount = dwClusterLinkSectorCount;
    pstMBR->dwTotalClusterCount = dwClusterCount;

    // MBR 영역에 1섹터를 씀
    if( gs_pfWriteHDDSector( TRUE, TRUE, 0, 1, gs_vbTempBuffer ) == FALSE )
    {
        // 동기화 처리
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return FALSE;
    }

    // MBR 이후부터 루트 디렉터리까지 모두 0으로 초기화
    kMemSet( gs_vbTempBuffer, 0, 512 );
    for( i = 0 ; i < ( dwClusterLinkSectorCount + FILESYSTEM_SECTORSPERCLUSTER ); i++ )
    {
        // 루트 디렉터리(클러스터 0)는 이미 파일 시스템이 사용하고 있으므로, 할당된 것으로 표시
        if( i == 0 )
        {
            ( ( DWORD* ) ( gs_vbTempBuffer ) )[ 0 ] = FILESYSTEM_LASTCLUSTER;
        }
        else
        {
            ( ( DWORD* ) ( gs_vbTempBuffer ) )[ 0 ] = FILESYSTEM_FREECLUSTER;
        }

        // 1섹터씩 씀
        if( gs_pfWriteHDDSector( TRUE, TRUE, i + 1, 1, gs_vbTempBuffer ) == FALSE )
        {
            // 동기화 처리
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return FALSE;
        }
    }

    // 캐시 버퍼를 모두 버림
    if( gs_stFileSystemManager.bCacheEnable == TRUE )
    {
        kDiscardAllCacheBuffer( CACHE_CLUSTERLINKTABLEAREA );
        kDiscardAllCacheBuffer( CACHE_DATAAREA );
    }

    // 동기화 처리
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return TRUE;
}

//  파일 시스템에 연결된 하드 디스크의 정보를 반환
BOOL kGetHDDInformation( HDDINFORMATION* pstInformation )
{
    BOOL bResult;

    // 동기화 처리
    kLock( &( gs_stFileSystemManager.stMutex ) );

    bResult = gs_pfReadHDDInformation( TRUE, TRUE, pstInformation );

    // 동기화 처리
    kUnlock( &( gs_stFileSystemManager.stMutex ) );

    return bResult;
}

//  클러스터 링크 테이블 내의 오프셋에서 한 섹터를 읽음
static BOOL kReadClusterLinkTable( DWORD dwOffset, BYTE* pbBuffer )
{
    // 캐시 여부에 따라 다른 읽기 함수 호출
    if( gs_stFileSystemManager.bCacheEnable == FALSE )
    {
        kInternalReadClusterLinkTableWithoutCache( dwOffset, pbBuffer );
    }
    else
    {
        kInternalReadClusterLinkTableWithCache( dwOffset, pbBuffer );
    }
}

//  클러스터 링크 테이블 내의 오프셋에서 한 섹터를 읽음
//      내부적으로 사용하는 함수, 캐시 사용 안함
static BOOL kInternalReadClusterLinkTableWithoutCache( DWORD dwOffset, BYTE* pbBuffer )
{
    // 클러스터 링크 테이블 영역의 시작 어드레스를 더함
    return gs_pfReadHDDSector( TRUE, TRUE, dwOffset + gs_stFileSystemManager.dwClusterLinkAreaStartAddress, 1, pbBuffer );
}

//  클러스터 링크 테이블 내의 오프셋에서 한 섹터를 읽음
//      내부적으로 사용하는 함수, 캐시 사용
static BOOL kInternalReadClusterLinkTableWithCache( DWORD dwOffset, BYTE* pbBuffer )
{
    CACHEBUFFER* pstCacheBuffer;

    // 먼저 캐시에 해당 클러스터 링크 테이블이 있는지 확인
    pstCacheBuffer = kFindCacheBuffer( CACHE_CLUSTERLINKTABLEAREA, dwOffset );

    // 캐시 버퍼에 있다면 캐시의 내용을 복사
    if( pstCacheBuffer != NULL )
    {
        kMemCpy( pbBuffer, pstCacheBuffer->pbBuffer, 512 );
        return TRUE;
    }

    // 캐시 버퍼에 없다면 하드 디스크에서 직접 읽음
    if( kInternalReadClusterLinkTableWithoutCache( dwOffset, pbBuffer ) == FALSE )
    {
        return FALSE;
    }

    // 캐시를 할당받아서 캐시 내용을 갱신
    pstCacheBuffer = kAllocateCacheBufferWithFlush( CACHE_CLUSTERLINKTABLEAREA );
    if( pstCacheBuffer == NULL )
    {
        return FALSE;
    }

    // 캐시 버퍼에 읽은 내용을 복사한 후 태그 정보를 갱신
    kMemCpy( pstCacheBuffer->pbBuffer, pbBuffer, 512 );
    pstCacheBuffer->dwTag = dwOffset;

    // 읽기를 수행했으므로 버퍼의 내용을 수정되지 않은 것으로 표시
    pstCacheBuffer->bChanged = FALSE;
    return TRUE;
}

//  클러스터 링크 테이블 영역의 캐시 버퍼 또는 데이터 영역의 캐시 버퍼에서 할당
//      빈 캐시 버퍼가 없는 경우 오래된 것 중에 하나를 골라서 비운 후 사용
static CACHEBUFFER* kAllocateCacheBufferWithFlush( int iCacheTableIndex )
{
    CACHEBUFFER* pstCacheBuffer;

    // 캐시 버퍼에 없다면 캐시를 할당받아서 캐시 내용을 갱신
    pstCacheBuffer = kAllocateCacheBuffer( iCacheTableIndex );
    // 캐시를 할당받을 수 없다면 캐시 버퍼에서 오래된 것을 찾아 버린 후 사용
    if( pstCacheBuffer == NULL )
    {
        pstCacheBuffer = kGetVictimInCacheBuffer( iCacheTableIndex );
        // 오래된 캐시 버퍼도 할당 받을 수 없으면 오류
        if( pstCacheBuffer == NULL )
        {
            kPrintf( "Cache Allocate Fail~!!!!\n" );
            return NULL;
        }

        // 캐시 버퍼의 데이터가 수정되었다면 하드 디스크로 옮겨야 함
        if( pstCacheBuffer->bChanged == TRUE )
        {
            switch( iCacheTableIndex )
            {
                // 클러스터 링크 테이블 영역의 캐시인 경우
            case CACHE_CLUSTERLINKTABLEAREA:
                // 쓰기가 실패한다면 오류
                if( kInternalWriteClusterLinkTableWithoutCache( pstCacheBuffer->dwTag, pstCacheBuffer->pbBuffer ) == FALSE )
                {
                    kPrintf( "Cache Buffer Write Fail~!!!!\n" );
                    return NULL;
                }
                break;

                // 데이터 영역의 캐시인 경우
            case CACHE_DATAAREA:
                // 쓰기가 실패한다면 오류
                if( kInternalWriteClusterWithoutCache( pstCacheBuffer->dwTag, pstCacheBuffer->pbBuffer ) == FALSE )
                {
                    kPrintf( "Cache Buffer Write Fail~!!!!\n" );
                    return NULL;
                }

                break;

                // 기타는 오류
            default:
                kPrintf( "kAllocateCacheBufferWithFlush Fail\n" );
                return NULL;
                break;
            }
        }
    }

    return pstCacheBuffer;
}

//  클러스터 링크 테이블 내의 오프셋에 한 섹터를 씀
static BOOL kWriteClusterLinkTable( DWORD dwOffset, BYTE* pbBuffer )
{
    // 캐시 여부에 따라 다른 쓰기 함수 호출
    if( gs_stFileSystemManager.bCacheEnable == FALSE )
    {
        return kInternalWriteClusterLinkTableWithoutCache( dwOffset, pbBuffer );
    }
    else
    {
        return kInternalWriteClusterLinkTableWithCache( dwOffset, pbBuffer );
    }
}

//  클러스터 링크 테이블 내의 오프셋에 한 섹터를 씀
//      내부적으로 사용하는 함수, 캐시 사용 안 함
static BOOL kInternalWriteClusterLinkTableWithoutCache( DWORD dwOffset, BYTE* pbBuffer )
{
    // 클러스터 링크 테이블 영역의 시작 어드레스를 더함
    return gs_pfWriteHDDSector( TRUE, TRUE, dwOffset + gs_stFileSystemManager.dwClusterLinkAreaStartAddress, 1, pbBuffer );
}

//  클러스터 링크 테이블 내의 오프셋에 한 섹터를 씀
//      내부적으로 사용하는 함수, 캐시 사용
static BOOL kInternalWriteClusterLinkTableWithCache( DWORD dwOffset, BYTE* pbBuffer )
{
    CACHEBUFFER* pstCacheBuffer;

    // 캐시에 해당 클러스터 링크 테이블이 있는지 확인
    pstCacheBuffer = kFindCacheBuffer( CACHE_CLUSTERLINKTABLEAREA, dwOffset );

    // 캐시 버퍼에 있다면 캐시에 씀
    if( pstCacheBuffer != NULL )
    {
        kMemCpy( pstCacheBuffer->pbBuffer, pbBuffer, 512 );

        // 쓰기를 수행했으므로 버퍼의 내용을 수정된 것으로 표시
        pstCacheBuffer->bChanged = TRUE;

        return TRUE;
    }

    // 캐시 버퍼에 없다면 캐시 버퍼를 할당 받아서 캐시 내용을 갱신
    pstCacheBuffer = kAllocateCacheBufferWithFlush( CACHE_CLUSTERLINKTABLEAREA );
    if( pstCacheBuffer == NULL )
    {
        return FALSE;
    }

    // 캐시 버퍼에 쓰고, 태그 정보를 갱신
    kMemCpy( pstCacheBuffer->pbBuffer, pbBuffer, 512 );
    pstCacheBuffer->dwTag = dwOffset;

    // 쓰기를 수행했으므로 버퍼의 내용을 수정된 것으로 표시
    pstCacheBuffer->bChanged = TRUE;

    return TRUE;
}

//  데이터 영역의 오프셋에서 한 클러스터를 읽음
static BOOL kReadCluster( DWORD dwOffset, BYTE* pbBuffer )
{
    // 캐시 여부에 따라 다른 읽기 함수 호출
    if( gs_stFileSystemManager.bCacheEnable == FALSE )
    {
        kInternalReadClusterWithoutCache( dwOffset, pbBuffer );
    }
    else
    {
        kInternalReadClusterWithCache( dwOffset, pbBuffer );
    }
}

//  데이터 영역의 오프셋세어 한 클러스터를 읽음
//      내부적으로 사용하는 함수, 캐시 사용 안 함
static BOOL kInternalReadClusterWithoutCache( DWORD dwOffset, BYTE* pbBuffer )
{
    // 데이터 영역의 시작 어드레스를 더함
    return gs_pfReadHDDSector( TRUE, TRUE, ( dwOffset * FILESYSTEM_SECTORSPERCLUSTER ) + gs_stFileSystemManager.dwDataAreaStartAddress, FILESYSTEM_SECTORSPERCLUSTER, pbBuffer );
}

//  데이터 영역의 오프셋에서 한 클러스터를 읽음
//      내부적으로 사용하는 함수, 캐시 사용
static BOOL kInternalReadClusterWithCache( DWORD dwOffset, BYTE* pbBuffer )
{
    CACHEBUFFER* pstCacheBuffer;

    // 캐시에 해당 데이터 클러스터가 있는지 확인
    pstCacheBuffer = kFindCacheBuffer( CACHE_DATAAREA, dwOffset );

    // 캐시 버퍼에 있다면 캐시의 내용을 복사
    if( pstCacheBuffer != NULL )
    {
        kMemCpy( pbBuffer, pstCacheBuffer->pbBuffer, FILESYSTEM_CLUSTERSIZE );
        return TRUE;
    }

    // 캐시 버퍼에 없다면 하드 디스크에서 직접 읽음
    if( kInternalReadClusterWithoutCache( dwOffset, pbBuffer ) == FALSE )
    {
        return FALSE;
    }

    // 캐시 버퍼를 할당받아서 캐시 내용을 갱신
    pstCacheBuffer = kAllocateCacheBufferWithFlush( CACHE_DATAAREA );
    if( pstCacheBuffer == NULL )
    {
        return FALSE;
    }

    // 캐시 버퍼에 읽은 내용을 복사한 후 태그 정보를 갱신
    kMemCpy( pstCacheBuffer->pbBuffer, pbBuffer, FILESYSTEM_CLUSTERSIZE );
    pstCacheBuffer->dwTag = dwOffset;

    // 읽기를 수행했으므로 버퍼의 내용을 수정되지 않은 것으로 표시
    pstCacheBuffer->bChanged = FALSE;
    return TRUE;
}

//  데이터 영역의 오프셋에 한 클러스터를 씀
static BOOL kWriteCluster( DWORD dwOffset, BYTE* pbBuffer )
{
    // 캐시 여부에 따라 다른 쓰기 함수 호출
    if( gs_stFileSystemManager.bCacheEnable == FALSE )
    {
        kInternalWriteClusterWithoutCache( dwOffset, pbBuffer );
    }
    else
    {
        kInternalWriteClusterWithCache( dwOffset, pbBuffer );
    }
}

//  데이터 영역의 오프셋에 한 클러스터를 씀
//      내부적으로 사용하는 함수, 캐시 사용 안함
static BOOL kInternalWriteClusterWithoutCache( DWORD dwOffset, BYTE* pbBuffer )
{
    return gs_pfWriteHDDSector( TRUE, TRUE, ( dwOffset * FILESYSTEM_SECTORSPERCLUSTER ) + gs_stFileSystemManager.dwDataAreaStartAddress, FILESYSTEM_SECTORSPERCLUSTER, pbBuffer );
}

//  데이터 영역의 오프셋에 한 클러스터를 씀
//      내부적으로 사용하는 함수, 캐시 사용
static BOOL kInternalWriteClusterWithCache( DWORD dwOffset, BYTE* pbBuffer )
{
    CACHEBUFFER* pstCacheBuffer;

    // 캐시 버퍼에 해당 데이터 클러스터가 있는지 확인
    pstCacheBuffer = kFindCacheBuffer( CACHE_DATAAREA, dwOffset );

    // 캐시 버퍼에 있다면 캐시에 씀
    if( pstCacheBuffer != NULL )
    {
        kMemCpy( pstCacheBuffer->pbBuffer, pbBuffer, FILESYSTEM_CLUSTERSIZE );

        // 쓰기를 수행했으므로 버퍼의 내용을 수정된 것으로 표시
        pstCacheBuffer->bChanged = TRUE;

        return TRUE;
    }

    // 캐시 버퍼에 없다면 캐시를 할당받아서 캐시 내용을 갱신
    pstCacheBuffer = kAllocateCacheBufferWithFlush( CACHE_DATAAREA );
    if( pstCacheBuffer == NULL )
    {
        return FALSE;
    }

    // 캐시 버퍼에 쓰고, 태그 정보를 갱신
    kMemCpy( pstCacheBuffer->pbBuffer, pbBuffer, FILESYSTEM_CLUSTERSIZE );
    pstCacheBuffer->dwTag = dwOffset;

    // 쓰기를 수행했으므로 버퍼의 내용을 수정된 것으로 표시
    pstCacheBuffer->bChanged = TRUE;

    return TRUE;
}

//  클러스터 링크 테이블 영역에서 빈 클러스터를 검색함
static DWORD kFindFreeCluster( void )
{
    DWORD dwLinkCountInSector;
    DWORD dwLastSectorOffset, dwCurrentSectorOffset;
    DWORD i, j;

    // 파일 시스템을 인식하지 못했으면 실패
    if( gs_stFileSystemManager.bMounted == FALSE )
    {
        return FILESYSTEM_LASTCLUSTER;
    }

    // 마지막으로 클러스터를 할당한 클러스터 링크 테이블의 섹터 오프셋을 가져옴
    dwLastSectorOffset = gs_stFileSystemManager.dwLastAllocatedClusterLinkSectorOffset;

    // 마지막으로 할당한 위치부터 루프를 돌면서 빈 클러스터를 검색
    for( i = 0 ; i < gs_stFileSystemManager.dwClusterLinkAreaSize ; i++ )
    {
        // 클러스터 링크 테이블의 마지막 섹터이면 전체 섹터만큼 도는 것이 아니라
        // 남은 클러스터의 수만큼 루프를 돌아야 함
        if( ( dwLastSectorOffset + i ) == ( gs_stFileSystemManager.dwClusterLinkAreaSize - 1 ) )
        {
            dwLinkCountInSector = gs_stFileSystemManager.dwTotalClusterCount % 128;
        }
        else
        {
            dwLinkCountInSector = 128;
        }

        // 이번에 읽어야 할 클러스터 링크 테이블의 섹터 오프셋을 구해서 읽음
        dwCurrentSectorOffset = ( dwLastSectorOffset + i ) % gs_stFileSystemManager.dwClusterLinkAreaSize;
        if( kReadClusterLinkTable( dwCurrentSectorOffset, gs_vbTempBuffer ) == FALSE )
        {
            return FILESYSTEM_LASTCLUSTER;
        }

        // 섹터 내에서 루프를 돌면서 빈 클러스터를 검색
        for( j = 0 ; j < dwLinkCountInSector ; j++ )
        {
            if( ( ( DWORD* ) gs_vbTempBuffer )[ j ] == FILESYSTEM_FREECLUSTER )
            {
                break;
            }
        }

        // 찾았다면 클러스터 인덱스를 반환
        if( j != dwLinkCountInSector )
        {
            // 마지막으로 클러스터를 할당한 클러스터 링크 내의 섹터 오프셋을 저장
            gs_stFileSystemManager.dwLastAllocatedClusterLinkSectorOffset = dwCurrentSectorOffset;

            // 현재 클러스터 링크 테이블의 오프셋을 감안하여 클러스터 인덱스를 계산
            return ( dwCurrentSectorOffset * 128 ) + j;
        }
    }

    return FILESYSTEM_LASTCLUSTER;
}


//  클러스터 링크 테이블에 값을 설정
static BOOL kSetClusterLinkData( DWORD dwClusterIndex, DWORD dwData )
{
    DWORD dwSectorOffset;

    // 파일 시스템을 인식하지 못했으면 실패
    if( gs_stFileSystemManager.bMounted == FALSE )
    {
        return FALSE;
    }

    // 한 섹터에 128개의 클러스터 링크가 들어가므로 128로 나누면 섹터 오프셋을 구할 수 있음
    dwSectorOffset = dwClusterIndex / 128;

    // 해당 섹터를 읽어서 링크 정보를 설정한 후 다시 저장
    if( kReadClusterLinkTable( dwSectorOffset, gs_vbTempBuffer ) == FALSE )
    {
        return FALSE;
    }

    ( ( DWORD* ) gs_vbTempBuffer )[ dwClusterIndex % 128 ] = dwData;

    if( kWriteClusterLinkTable( dwSectorOffset, gs_vbTempBuffer ) == FALSE )
    {
        return FALSE;
    }

    return TRUE;
}

//  클러스터 링크 테이블의 값을 반환
static BOOL kGetClusterLinkData( DWORD dwClusterIndex, DWORD* pdwData )
{
    DWORD dwSectorOffset;

    // 파일 시스템을 인식하지 못했으면 실패
    if( gs_stFileSystemManager.bMounted == FALSE )
    {
        return FALSE;
    }

    // 한 섹터에 128개의 클러스터 링크가 들어가므로 128로 나누면 섹터 오프셋을 구할 수 있음
    dwSectorOffset = dwClusterIndex / 128;

    if( dwSectorOffset > gs_stFileSystemManager.dwClusterLinkAreaSize )
    {
        return FALSE;
    }

    // 해당 섹터를 읽어서 링크 정보를 반환
    if( kReadClusterLinkTable( dwSectorOffset, gs_vbTempBuffer ) == FALSE )
    {
        return FALSE;
    }

    *pdwData = ( ( DWORD* ) gs_vbTempBuffer )[ dwClusterIndex % 128 ];
    return TRUE;
}


//  루트 디렉터리에서 빈 디렉터리 엔트리를 반환
static int kFindFreeDirectoryEntry( void )
{
    DIRECTORYENTRY* pstEntry;
    int i;

    // 파일 시스템을 인식하지 못했으면 실패
    if( gs_stFileSystemManager.bMounted == FALSE )
    {
        return -1;
    }

    // 루트 디렉터리를 읽음
    if( kReadCluster( 0, gs_vbTempBuffer ) == FALSE )
    {
        return -1;
    }

    // 루트 디렉터리 안에서 루프를 돌면서 빈 엔트리, 즉 시작 클러스터 번호가 0인 엔트리 검색
    pstEntry = ( DIRECTORYENTRY* ) gs_vbTempBuffer;
    for( i = 0 ; i < FILESYSTEM_MAXDIRECTORYENTRYCOUNT ; i++ )
    {
        if( pstEntry[ i ].dwStartClusterIndex == 0 )
        {
            return i;
        }
    }

    return -1;
}

//  루트 디렉터리의 해당 인덱스에 디렉터리 엔트리를 설정
static BOOL kSetDirectoryEntryData( int iIndex, DIRECTORYENTRY* pstEntry )
{
    DIRECTORYENTRY* pstRootEntry;

    // 파일 시스템을 인식하지 못했거나 인덱스가 올바르지 않으면 실패
    if( ( gs_stFileSystemManager.bMounted == FALSE ) || ( iIndex < 0 ) || ( iIndex >= FILESYSTEM_MAXDIRECTORYENTRYCOUNT ) )
    {
        return FALSE;
    }

    // 루트 디렉터리를 읽음
    if( kReadCluster( 0, gs_vbTempBuffer ) == FALSE )
    {
        return TRUE;
    }

    // 루트 디렉터리에 있는 해당 데이터를 갱신
    pstRootEntry = ( DIRECTORYENTRY* ) gs_vbTempBuffer;
    kMemCpy( pstRootEntry + iIndex, pstEntry, sizeof( DIRECTORYENTRY ) );

    // 루트 디렉터리에 씀
    if( kWriteCluster( 0, gs_vbTempBuffer ) == FALSE )
    {
        return FALSE;
    }

    return TRUE;
}

//  루트 디렉터리의 해당 인덱스에 위치하는 디렉터리 엔트리를 반환
BOOL kGetDirectoryEntryData( int iIndex, DIRECTORYENTRY* pstEntry )
{
    DIRECTORYENTRY* pstRootEntry;

    // 파일 시스템을 인식하지 못했거나 인덱스가 올바르지 않으면 실패
    if( ( gs_stFileSystemManager.bMounted == FALSE ) || ( iIndex < 0 ) || ( iIndex >= FILESYSTEM_MAXDIRECTORYENTRYCOUNT ) )
    {
        return FALSE;
    }

    // 루트 디렉터리를 읽음
    if( kReadCluster( 0, gs_vbTempBuffer ) == FALSE )
    {
        return FALSE;
    }

    // 루트 디렉터리에 있는 해당 데이터를 갱신
    pstRootEntry = ( DIRECTORYENTRY* ) gs_vbTempBuffer;
    kMemCpy( pstEntry, pstRootEntry + iIndex, sizeof( DIRECTORYENTRY ) );
    return TRUE;
}

//  루트 디렉터리에서 파일 이름이 일치하는 엔트리를 찾아서 인덱스를 반환
int kFindDirectoryEntry( const char* pcFileName, DIRECTORYENTRY* pstEntry )
{
    DIRECTORYENTRY* pstRootEntry;
    int i;
    int iLength;

    // 파일 시스템을 인식하지 못했으면 실패
    if( gs_stFileSystemManager.bMounted == FALSE )
    {
        return -1;
    }

    // 루트 디렉터리를 읽음
    if( kReadCluster( 0, gs_vbTempBuffer ) == FALSE )
    {
        return -1;
    }

    iLength = kStrLen( pcFileName );
    // 루트 디렉터리 안에서 루프를 돌면서 파일 이름이 일치하는 엔트리를 반환
    pstRootEntry = ( DIRECTORYENTRY* ) gs_vbTempBuffer;
    for( i = 0 ; i < FILESYSTEM_MAXDIRECTORYENTRYCOUNT ; i++ )
    {
        if( kMemCmp( pstRootEntry[ i ].vcFileName, pcFileName, iLength ) == 0 )
        {
            kMemCpy( pstEntry, pstRootEntry + i, sizeof( DIRECTORYENTRY ) );
            return i;
        }
    }
    return -1;
}

// 파일 시스템의 정보를 반환
void kGetFileSystemInformation( FILESYSTEMMANAGER* pstManger )
{
    kMemCpy( pstManger, &gs_stFileSystemManager, sizeof( gs_stFileSystemManager ) );
}

//==============================================================================
//  고수준 함수(High Level Function)
//==============================================================================
//  비어 있는 핸들을 할당
static void* kAllocateFileDirectoryHandle( void )
{
    int i;
    FILE* pstFile;

    // 핸들 풀을 모두 검색하여 비어있는 핸들을 반환
    pstFile = gs_stFileSystemManager.pstHandlePool;
    for( i = 0 ; i < FILESYSTEM_HANDLE_MAXCOUNT ; i++ )
    {
        // 비어 있다면 반환
        if( pstFile->bType == FILESYSTEM_TYPE_FREE )
        {
            pstFile->bType = FILESYSTEM_TYPE_FILE;
            return pstFile;
        }

        // 다음으로 이동
        pstFile++;
    }

    return NULL;
}

//  사용한 핸들을 반환
static void kFreeFileDirectoryHandle( FILE* pstFile )
{
    // 전체 영역을 초기화
    kMemSet( pstFile, 0, sizeof( FILE ) );

    // 비어 있는 타입으로 설정
    pstFile->bType = FILESYSTEM_TYPE_FREE;
}

//  파일을 생성
static BOOL kCreateFile( const char* pcFileName, DIRECTORYENTRY* pstEntry, int* piDirectoryEntryIndex )
{
    DWORD dwCluster;

    // 빈 클러스터를 찾아서 할당된 것으로 설정
    dwCluster = kFindFreeCluster();
    if( ( dwCluster == FILESYSTEM_LASTCLUSTER ) || ( kSetClusterLinkData( dwCluster, FILESYSTEM_LASTCLUSTER ) == FALSE ) )
    {
        return FALSE;
    }

    // 빈 디렉터리 엔트리를 검색
    *piDirectoryEntryIndex = kFindFreeDirectoryEntry();
    if( *piDirectoryEntryIndex == -1 )
    {
        // 실패하면 할당 받은 클러스터를 반환해야 함
        kSetClusterLinkData( dwCluster, FILESYSTEM_FREECLUSTER );
        return FALSE;
    }

    // 디렉터리 엔트리를 설정
    kMemCpy( pstEntry->vcFileName, pcFileName, kStrLen( pcFileName ) + 1 );
    pstEntry->dwStartClusterIndex = dwCluster;
    pstEntry->dwFileSize = 0;

    // 디렉터리 엔트리를 등록
    if( kSetDirectoryEntryData( *piDirectoryEntryIndex, pstEntry ) == FALSE )
    {
        // 실패하면 할당 받은 클러스터를 반환해야 함
        kSetClusterLinkData( dwCluster, FILESYSTEM_FREECLUSTER );
        return FALSE;
    }
    return TRUE;
}

//  파라미터로 넘어온 클러스터부터 파일의 끝까지 연결된 클러스터를 모두 반환
static BOOL kFreeClusterUntilEnd( DWORD dwClusterIndex )
{
    DWORD dwCurrentClusterIndex;
    DWORD dwNextClusterIndex;

    // 클러스터 인덱스를 초기화
    dwCurrentClusterIndex = dwClusterIndex;

    while( dwCurrentClusterIndex != FILESYSTEM_LASTCLUSTER )
    {
        // 다음 클러스터의 인덱스를 가져옴
        if( kGetClusterLinkData( dwCurrentClusterIndex, &dwNextClusterIndex ) == FALSE )
        {
            return FALSE;
        }

        // 현재 클러스터를 빈 것으로 설정하여 해제
        if( kSetClusterLinkData( dwCurrentClusterIndex, FILESYSTEM_FREECLUSTER ) == FALSE )
        {
            return FALSE;
        }

        // 현재 클러스터 인덱스를 다음 클러스터의 인덱스로 바꿈
        dwCurrentClusterIndex = dwNextClusterIndex;
    }

    return TRUE;
}

//  파일을 열거나 생성
FILE* kOpenFile( const char* pcFileName, const char* pcMode )
{
    DIRECTORYENTRY stEntry;
    int iDirectoryEntryOffset;
    int iFileNameLength;
    DWORD dwSecondCluster;
    FILE* pstFile;

    // 파일 이름 검사
    iFileNameLength = kStrLen( pcFileName );
    if( ( iFileNameLength > ( sizeof( stEntry.vcFileName ) - 1 ) ) || ( iFileNameLength == 0 ) )
    {
        return NULL;
    }

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    //==============================================================================
    // 파일이 먼저 존재하는지 확인하고, 없다면 옵션을 보고 파일을 생성
    //==============================================================================
    iDirectoryEntryOffset = kFindDirectoryEntry( pcFileName, &stEntry );
    if( iDirectoryEntryOffset == -1 )
    {
        // 파일이 없다면 읽기(r, r+) 옵션은 실패
        if( pcMode[ 0 ] == 'r' )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return NULL;
        }

        // 나머지 옵션은 파일을 생성
        if( kCreateFile( pcFileName, &stEntry, &iDirectoryEntryOffset ) == FALSE )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return NULL;
        }
    }
    //============================================================================================
    // 파일의 내용을 비워야 하는 옵션이면 파일에 연결된 클러스터를 모두 제거하고 파일 크기를 0으로 설정
    //============================================================================================
    else if( pcMode[ 0 ] == 'w' )
    {
        // 시작 클러스터의 다음 클러스터를 찾음
        if( kGetClusterLinkData( stEntry.dwStartClusterIndex, &dwSecondCluster ) == FALSE )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return NULL;
        }

        // 시작 클러스터를 마지막 클러스터로 만듦
        if( kSetClusterLinkData( stEntry.dwStartClusterIndex, FILESYSTEM_LASTCLUSTER ) == FALSE )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return NULL;
        }

        // 다음 클러스터부터 마지막 클러스터까지 모두 해제
        if( kFreeClusterUntilEnd( dwSecondCluster ) == FALSE )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return NULL;
        }

        // 파일의 내용이 모두 비워졌으므로 크기를 모두 0으로 설정
        stEntry.dwFileSize = 0;
        if( kSetDirectoryEntryData( iDirectoryEntryOffset, &stEntry ) == FALSE )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return NULL;
        }
    }

    //==============================================================================
    // 파일 핸들을 할당받아 데이터를 설정한 후 반환
    //==============================================================================
    // 파일 핸들을 할당 받아 데이터 설정
    pstFile = kAllocateFileDirectoryHandle();
    if( pstFile == NULL )
    {
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return NULL;
    }

    // 파일 핸들에 파일 정보를 생성
    pstFile->bType = FILESYSTEM_TYPE_FILE;
    pstFile->stFileHandle.iDirectoryEntryOffset = iDirectoryEntryOffset;
    pstFile->stFileHandle.dwFileSize = stEntry.dwFileSize;
    pstFile->stFileHandle.dwStartClusterIndex = stEntry.dwStartClusterIndex;
    pstFile->stFileHandle.dwCurrentClusterIndex = stEntry.dwStartClusterIndex;
    pstFile->stFileHandle.dwPreviousClusterIndex = stEntry.dwStartClusterIndex;
    pstFile->stFileHandle.dwCurrentOffset = 0;

    // 만약 추가 옵션(a)이 설정되어 있으면 파일의 끝으로 이동
    if( pcMode[ 0 ] == 'a' )
    {
        kSeekFile( pstFile, 0, FILESYSTEM_SEEK_END );
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return pstFile;
}

//  파일을 읽어 버퍼로 복사
DWORD kReadFile( void* pvBuffer, DWORD dwSize, DWORD dwCount, FILE* pstFile )
{
    DWORD dwTotalCount;
    DWORD dwReadCount;
    DWORD dwOffsetInCluster;
    DWORD dwCopySize;
    FILEHANDLE* pstFileHandle;
    DWORD dwNextClusterIndex;

    // 핸들이 파일 타입이 아니면 실패
    if( ( pstFile == NULL ) || ( pstFile->bType != FILESYSTEM_TYPE_FILE ) )
    {
        return 0;
    }
    pstFileHandle = &( pstFile->stFileHandle );

    // 파일의 끝이거나 마지막 클러스터면 종료
    if( ( pstFileHandle->dwCurrentOffset == pstFileHandle->dwFileSize ) || ( pstFileHandle->dwCurrentClusterIndex == FILESYSTEM_LASTCLUSTER ) )
    {
        return 0;
    }

    // 파일 끝과 비교해서 실제로 읽을 수 있는 값을 계산
    dwTotalCount = MIN( dwSize * dwCount, pstFileHandle->dwFileSize - pstFileHandle->dwCurrentOffset );

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 계산된 값만큼 다 읽을 때까지 반복
    dwReadCount = 0;
    while( dwReadCount != dwTotalCount )
    {
        //==============================================================================
        // 클러스터를 읽어서 버퍼에 복사
        //==============================================================================
        // 현재 클러스터를 읽음
        if( kReadCluster( pstFileHandle->dwCurrentClusterIndex, gs_vbTempBuffer ) == FALSE )
        {
            break;
        }

        // 클러스터 내에서 파일 포인터가 존재하는 오프셋을 계산
        dwOffsetInCluster = pstFileHandle->dwCurrentOffset % FILESYSTEM_CLUSTERSIZE;

        // 여러 클러스터에 걸쳐져 있다면 현재 클러스터에서 남은 만큼 읽고 다음 클러스터로 이동
        dwCopySize = MIN( FILESYSTEM_CLUSTERSIZE - dwOffsetInCluster, dwTotalCount - dwReadCount );
        kMemCpy( ( char* ) pvBuffer + dwReadCount, gs_vbTempBuffer + dwOffsetInCluster, dwCopySize );

        // 읽은 바이트 수와 파일 포인터의 위치를 갱싱
        dwReadCount += dwCopySize;
        pstFileHandle->dwCurrentOffset += dwCopySize;

        //==============================================================================
        // 현재 클러스터를 끝까지 다 읽었으면 다음 클러스터로 이동
        //==============================================================================
        if( ( pstFileHandle->dwCurrentOffset % FILESYSTEM_CLUSTERSIZE ) == 0 )
        {
            // 현재 클러스터의 링크 데이터를 찾아 다음 클러스터를 얻음
            if( kGetClusterLinkData( pstFileHandle->dwCurrentClusterIndex, &dwNextClusterIndex ) == FALSE )
            {
                break;
            }

            // 클러스터 정보를 갱신
            pstFileHandle->dwPreviousClusterIndex = pstFileHandle->dwCurrentClusterIndex;
            pstFileHandle->dwCurrentClusterIndex = dwNextClusterIndex;
        }
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );

    // 읽은 바이트 수를 반환
    return ( dwReadCount / dwSize );
}

//  루트 디렉터리에서 디렉터리 엔트리 값을 갱신
static BOOL kUpdateDirectoryEntry( FILEHANDLE* pstFileHandle )
{
    DIRECTORYENTRY stEntry;

    // 디렉터리 엔트리 검색
    if( ( pstFileHandle == NULL ) || ( kGetDirectoryEntryData( pstFileHandle->iDirectoryEntryOffset, &stEntry ) == FALSE ) )
    {
        return FALSE;
    }

    // 파일 크기와 시작 클러스터를 변경
    stEntry.dwFileSize = pstFileHandle->dwFileSize;
    stEntry.dwStartClusterIndex = pstFileHandle->dwStartClusterIndex;

    // 변경된 디렉터리 엔트리를 설정
    if( kSetDirectoryEntryData( pstFileHandle->iDirectoryEntryOffset, &stEntry ) == FALSE )
    {
        return FALSE;
    }

    return TRUE;
}

//  버퍼의 데이터를 파일에 씀
DWORD kWriteFile( const void* pvBuffer, DWORD dwSize, DWORD dwCount, FILE* pstFile )
{
    DWORD dwWriteCount;
    DWORD dwTotalCount;
    DWORD dwOffsetInCluster;
    DWORD dwCopySize;
    DWORD dwAllocatedClusterIndex;
    DWORD dwNextClusterIndex;
    FILEHANDLE* pstFileHandle;

    // 핸들이 파일 타입이 아니면 실패
    if( ( pstFile == NULL ) || ( pstFile->bType != FILESYSTEM_TYPE_FILE ) )
    {
        return 0;
    }
    pstFileHandle = &( pstFile->stFileHandle );

    // 총 바이트 수
    dwTotalCount = dwSize * dwCount;

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 다 쓸 때까지 반복
    dwWriteCount = 0;
    while( dwWriteCount != dwTotalCount )
    {
        //==============================================================================
        // 현재 클러스터가 파일의 끝이면 클러스터를 할당하여 연결
        //==============================================================================
        if( pstFileHandle->dwCurrentClusterIndex == FILESYSTEM_LASTCLUSTER )
        {
            // 빈 클러스터 검색
            dwAllocatedClusterIndex = kFindFreeCluster();
            if( dwAllocatedClusterIndex == FILESYSTEM_LASTCLUSTER )
            {
                break;
            }

            // 검색된 클러스터를 마지막 클러스터로 설정
            if( kSetClusterLinkData( dwAllocatedClusterIndex, FILESYSTEM_LASTCLUSTER ) == FALSE )
            {
                break;
            }

            // 파일의 마지막 클러스터에 할당된 클러스터를 연결
            if( kSetClusterLinkData( pstFileHandle->dwPreviousClusterIndex, dwAllocatedClusterIndex ) == FALSE )
            {
                // 실패하면 할당된 클러스터를 해제
                kSetClusterLinkData( dwAllocatedClusterIndex, FILESYSTEM_FREECLUSTER );
                break;
            }

            // 현재 클러스터를 할당된 것으로 변경
            pstFileHandle->dwCurrentClusterIndex = dwAllocatedClusterIndex;

            // 새로 할당 받았으니 임시 클러스터 버퍼를 0으로 채움
            kMemSet( gs_vbTempBuffer, 0, FILESYSTEM_LASTCLUSTER );
        }
        //==============================================================================
        // 한 클러스터를 채우지 못하면 클러스터를 읽어서 임시 클러스터 버퍼로 복사
        //==============================================================================
        else if( ( ( pstFileHandle->dwCurrentOffset % FILESYSTEM_CLUSTERSIZE ) != 0 ) || ( ( dwTotalCount - dwWriteCount ) < FILESYSTEM_CLUSTERSIZE ) )
        {
            // 전체 클러스터를 덮어쓰는 경우가 아니면 부부만 덮어써야 하므로 현재 클러스터를 읽음
            if( kReadCluster( pstFileHandle->dwCurrentClusterIndex, gs_vbTempBuffer ) == FALSE )
            {
                break;
            }
        }

        // 클러스터 내에서 파일 포인터가 존재하는 오프셋을 계산
        dwOffsetInCluster = pstFileHandle->dwCurrentOffset % FILESYSTEM_CLUSTERSIZE;

        // 여러 클러스터에 걸쳐져 있다면 현재 클러스터에서 남은 만큼 쓰고 다음 클러스터로 이도 
        dwCopySize = MIN( FILESYSTEM_CLUSTERSIZE - dwOffsetInCluster, dwTotalCount - dwWriteCount );
        kMemCpy( gs_vbTempBuffer + dwOffsetInCluster, ( char* ) pvBuffer + dwWriteCount, dwCopySize );

        // 임시 버퍼에 삽입된 값을 디스크에 씀
        if( kWriteCluster( pstFileHandle->dwCurrentClusterIndex, gs_vbTempBuffer ) == FALSE )
        {
            break;
        }

        // 쓴 바이트 수와 파일 포인터의 위치를 갱신
        dwWriteCount += dwCopySize;
        pstFileHandle->dwCurrentOffset += dwCopySize;

        //==============================================================================
        // 현재 클러스터의 끝까지 다 썼으면 다음 클러스터로 이동
        //==============================================================================
        if( ( pstFileHandle->dwCurrentOffset % FILESYSTEM_CLUSTERSIZE ) == 0 )
        {
            // 현재 클러스터의 링크 데이터로 다음 클러스터를 얻음
            if( kGetClusterLinkData( pstFileHandle->dwCurrentClusterIndex, &dwNextClusterIndex ) == FALSE )
            {
                break;
            }

            // 클러스터 정보를 갱신
            pstFileHandle->dwPreviousClusterIndex = pstFileHandle->dwCurrentClusterIndex;
            pstFileHandle->dwCurrentClusterIndex = dwNextClusterIndex;
        }
    }

    //==============================================================================
    // 파일 크기가 변했다면 루트 디렉터리에 있는 디렉터리 엔트리 정보를 갱신
    //==============================================================================
    if( pstFileHandle->dwFileSize < pstFileHandle->dwCurrentOffset )
    {
        pstFileHandle->dwFileSize = pstFileHandle->dwCurrentOffset;
        kUpdateDirectoryEntry( pstFileHandle );
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return dwWriteCount;
}

//  파일을 Count 만큼 0으로 채움
BOOL kWriteZero( FILE* pstFile, DWORD dwCount )
{
    BYTE* pbBuffer;
    DWORD dwRemainCount;
    DWORD dwWriteCount;

    // 핸들이 NULL이면 실패
    if( pstFile == NULL )
    {
        return FALSE;
    }

    // 속도 향상을 위해 메모리를 할당 받아 클러스터 단위로 쓰기 수행 메모리를 할당
    pbBuffer = ( BYTE* ) kAllocateMemory( FILESYSTEM_CLUSTERSIZE );
    if( pbBuffer == NULL )
    {
        return FALSE;
    }

    // 0으로 채움
    kMemSet( pbBuffer, 0, FILESYSTEM_CLUSTERSIZE );
    dwRemainCount = dwCount;

    // 클러스터 단위로 반복해서 쓰기 수행
    while( dwRemainCount != 0 )
    {
        dwWriteCount = MIN( dwRemainCount, FILESYSTEM_CLUSTERSIZE );
        if( kWriteFile( pbBuffer, 1, dwWriteCount, pstFile ) != dwWriteCount )
        {
            kFreeMemory( pbBuffer );
            return FALSE;
        }
        dwRemainCount -= dwWriteCount;
    }
    kFreeMemory( pbBuffer );
    return TRUE;
}

//  파일 포인터의 위치를 이동
int kSeekFile( FILE* pstFile, int iOffset, int iOrigin )
{
    DWORD dwRealOffset;
    DWORD dwClusterOffsetToMove;
    DWORD dwCurrentClusterOffset;
    DWORD dwLastClusterOffset;
    DWORD dwMoveCount;
    DWORD i;
    DWORD dwStartClusterIndex;
    DWORD dwPreviousClusterIndex;
    DWORD dwCurrentClusterIndex;
    FILEHANDLE* pstFileHandle;

    // 핸들이 파일 타입이 아니면 나감
    if( ( pstFile == NULL ) || ( pstFile->bType != FILESYSTEM_TYPE_FILE ) )
    {
        return 0;
    }
    pstFileHandle = &( pstFile->stFileHandle );

    //===============================================================================
    // Origin과 Offset을 조합하여 파일 시작을 기준으로 파일 포인터를 옮겨야 할 위치 계산
    //===============================================================================
    // 옵션에 따라서 실제 위치를 계산
    // 음수면 파일의 시작 방향으로 이동하며, 양수면 파일의 끝 방향으로 이동
    switch( iOrigin )
    {
    // 파일의 시작을 기준으로 이동
    case FILESYSTEM_SEEK_SET:
        // 파일의 처음이므로 이동할 오프셋이 음수면 0으로 설정
        if( iOffset <= 0 )
        {
            dwRealOffset = 0;
        }
        else
        {
            dwRealOffset = iOffset;
        }
        break;

    // 현재 위치를 기준으로 이동
    case FILESYSTEM_SEEK_CUR:
        // 이동할 오프셋이 음수이고, 현재 파일 포인터의 값보다 크면
        // 더 이상 갈 수 없으므로 파일의 처음으로 이동
        if( ( iOffset < 0 ) && ( pstFileHandle->dwCurrentOffset <= ( DWORD ) - iOffset ) )
        {
            dwRealOffset = 0;
        }
        else
        {
            dwRealOffset = pstFileHandle->dwCurrentOffset + iOffset;
        }
        break;
    
    // 파일의 끝부분을 기준으로 이동
    case FILESYSTEM_SEEK_END:
        // 이동할 오프셋이 음수이고, 현재 파일 포인터의 값보다 크면
        // 더 이상 갈 수 없으므로 파일의 처음으로 이동
        if( ( iOffset < 0 ) && ( pstFileHandle->dwFileSize <= ( DWORD ) - iOffset ) )
        {
            dwRealOffset = 0;
        }
        else
        {
            dwRealOffset = pstFileHandle->dwFileSize + iOffset;
        }
        break;
    }

    //==============================================================================
    // 파일을 구성하는 클러스터의 개수와 현재 파일 포인터의 위치를 고려하여
    // 옮겨질 파일 포인터가 위치하는 클러스터까지 클러스터 링크를 탐색
    //==============================================================================
    // 파일의 마지막 클러스터의 오프셋
    dwLastClusterOffset = pstFileHandle->dwFileSize / FILESYSTEM_CLUSTERSIZE;
    // 파일 포인터가 옮겨질 위치의 클러스터 오프셋
    dwClusterOffsetToMove = dwRealOffset / FILESYSTEM_CLUSTERSIZE;
    // 현재 파일 포인터가 있는 클러스터의 오프셋
    dwCurrentClusterOffset = pstFileHandle->dwCurrentOffset / FILESYSTEM_CLUSTERSIZE;

    // 이동하는 클러스터의 위치가 파일의 마지막 클러스터의 오프셋을 넘어서면
    // 현재 클러스터에서 마지막까지 이동한 후 Write 함수를 이용해서 공백으로 나머지를 채움
    if( dwLastClusterOffset < dwClusterOffsetToMove )
    {
        dwMoveCount = dwLastClusterOffset - dwCurrentClusterOffset;
        dwStartClusterIndex = pstFileHandle->dwCurrentClusterIndex;
    }
    // 이동하는 클러스터의 위치가 현재 클러스터와 같거나 다음에 위치해 있다면
    // 현재 클러스터를 기준으로 차이만큼만 이동하면 된다.
    else if( dwCurrentClusterOffset <= dwClusterOffsetToMove )
    {
        dwMoveCount = dwClusterOffsetToMove - dwCurrentClusterOffset;
        dwStartClusterIndex = pstFileHandle->dwCurrentClusterIndex;
    }
    // 이동하는 클러스터의 위치가 현재 클러스터 이전에 있다면, 첫 번째 클러스터부터 이동하면서 검색
    else
    {
        dwMoveCount = dwClusterOffsetToMove;
        dwStartClusterIndex = pstFileHandle->dwStartClusterIndex;
    }

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 클러스터를 이동
    dwCurrentClusterIndex = dwStartClusterIndex;
    for( i = 0 ; i < dwMoveCount ; i++ )
    {
        // 값을 보관
        dwPreviousClusterIndex = dwCurrentClusterIndex;

        // 다음 클러스터의 인덱스를 읽음
        if( kGetClusterLinkData( dwPreviousClusterIndex, &dwCurrentClusterIndex ) == FALSE )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return -1;
        }
    }

    // 클러스터를 이동했으면 클러스터 정보를 갱신
    if( dwMoveCount > 0 )
    {
        pstFileHandle->dwPreviousClusterIndex = dwPreviousClusterIndex;
        pstFileHandle->dwCurrentClusterIndex = dwCurrentClusterIndex;
    }
    // 첫 번째 클러스터로 이동하는 경우는 핸들의 클러스터 값을 시작 클러스터로 설정
    else if( dwStartClusterIndex == pstFileHandle->dwStartClusterIndex )
    {
        pstFileHandle->dwPreviousClusterIndex = pstFileHandle->dwStartClusterIndex;
        pstFileHandle->dwCurrentClusterIndex = pstFileHandle->dwStartClusterIndex;
    }

    //============================================================================================
    // 파일 포인터를 갱신하고 파일 오프셋이 파일의 크기를 넘었다면 나머지 부분을
    // 0으로 채워서 파일의 크기를 늘림
    //============================================================================================
    // 실제 파일의 크기를 넘어서 파일 포인터가 이동했다면, 파일 끝에서 부터 남은 크기만큼 0으로 채워줌
    if( dwLastClusterOffset < dwClusterOffsetToMove )
    {
        pstFileHandle->dwCurrentOffset = pstFileHandle->dwFileSize;
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );

        // 남은 크기만큼 0으로 채움
        if( kWriteZero( pstFile, dwRealOffset - pstFileHandle->dwFileSize ) == FALSE )
        {
            return 0;
        }
    }

    pstFileHandle->dwCurrentOffset = dwRealOffset;

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );

    return 0;
}

//  파일을 닫음
int kCloseFile( FILE* pstFile )
{
    // 핸들 타입이 파일이 아니면 실패
    if( ( pstFile == NULL ) || ( pstFile->bType != FILESYSTEM_TYPE_FILE ) )
    {
        return -1;
    }

    // 핸들을 반환
    kFreeFileDirectoryHandle( pstFile );
    return 0;
}

//  핸들 풀을 검사하여 파일이 열려 있는지를 확인
BOOL kIsFileOpened( const DIRECTORYENTRY* pstEntry )
{
    int i;
    FILE* pstFile;

    // 핸들 풀의 시작 어드레스부터 끝까지 열린 파일만 검색
    pstFile = gs_stFileSystemManager.pstHandlePool;
    for( i = 0 ; i < FILESYSTEM_HANDLE_MAXCOUNT ; i++ )
    {
        // 파일 타입 중에서 시작 클러스터가 일치하면 반환
        if( ( pstFile[ i ].bType == FILESYSTEM_TYPE_FILE ) && ( pstFile[ i ].stFileHandle.dwStartClusterIndex == pstEntry->dwStartClusterIndex ) )
        {
            return TRUE;
        }
    }

    return FALSE;
}

//  파일을 삭제
int kRemoveFile( const char* pcFileName )
{
    DIRECTORYENTRY stEntry;
    int iDirectoryEntryOffset;
    int iFileNameLength;

    // 파일 이름 검사
    iFileNameLength = kStrLen( pcFileName );
    if( ( iFileNameLength > ( sizeof( stEntry.vcFileName ) - 1 ) ) || ( iFileNameLength == 0 ) )
    {
        return NULL;
    }

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 파일이 존재하는지 확인
    iDirectoryEntryOffset = kFindDirectoryEntry( pcFileName, &stEntry );
    if( iDirectoryEntryOffset == -1 )
    {
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return -1;
    }

    // 다른 태스크에서 해당 파일을 열고 있는지 핸들 풀을 검색하여 확인 파일이 열려 있으면 삭제할 수 없음
    if( kIsFileOpened( &stEntry ) == TRUE )
    {
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return -1;
    }

    // 파일을 구성하는 클러스터를 모두 해제
    if( kFreeClusterUntilEnd( stEntry.dwStartClusterIndex ) == FALSE )
    {
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return -1;
    }

    // 디렉터리 엔트리를 빈 것으로 설정
    kMemSet( &stEntry, 0, sizeof( stEntry ) );
    if( kSetDirectoryEntryData( iDirectoryEntryOffset, &stEntry ) == FALSE )
    {
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return -1;
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return 0;
}

//  디렉터리를 엶
DIR* kOpenDirectory( const char* pcDirectoryName )
{
    DIR* pstDirectory;
    DIRECTORYENTRY* pstDirectoryBuffer;

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 루트 디렉터리 밖에 없으므로 디렉터리 이름은 무시하고 핸들만 할당받아서 반환
    pstDirectory = kAllocateFileDirectoryHandle();
    if( pstDirectory == NULL )
    {
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return NULL;
    }

    // 루트 디렉터리를 저장할 버퍼를 할당
    pstDirectoryBuffer = ( DIRECTORYENTRY* ) kAllocateMemory( FILESYSTEM_CLUSTERSIZE );
    if( pstDirectory == NULL )
    {
        // 실패하면 핸들을 반환해야 함
        kFreeFileDirectoryHandle( pstDirectory );
        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return NULL;
    }

    // 루트 디렉터리를 읽음
    if( kReadCluster( 0, ( BYTE* ) pstDirectoryBuffer ) == FALSE )
    {
        // 실패하면 핸들과 메모리를 모두 반환해야 함
        kFreeFileDirectoryHandle( pstDirectory );
        kFreeMemory( pstDirectoryBuffer );

        // 동기화
        kUnlock( &( gs_stFileSystemManager.stMutex ) );
        return NULL;
    }

    // 디렉터리 타입으로 설정하고 현재 디렉터리 엔트리의 오프셋을 초기화
    pstDirectory->bType = FILESYSTEM_TYPE_DIRECTORY;
    pstDirectory->stDirectoryHandle.iCurrentOffset = 0;
    pstDirectory->stDirectoryHandle.pstDirectoryBuffer = pstDirectoryBuffer;

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return pstDirectory;
}

//  디렉터리 엔트리를 반환하고 다음으로 이동
struct kDirectoryEntryStruct* kReadDirectory( DIR* pstDirectory )
{
    DIRECTORYHANDLE* pstDirectoryHandle;
    DIRECTORYENTRY* pstEntry;

    // 핸들 타입이 디렉터리가 아니면 실패
    if( ( pstDirectory == NULL ) || ( pstDirectory->bType != FILESYSTEM_TYPE_DIRECTORY ) )
    {
        return NULL;
    }

    pstDirectoryHandle = &( pstDirectory->stDirectoryHandle );

    // 오프셋의 범위가 클러스터에 존재하는 최댓값을 넘어서면 실패
    if( ( pstDirectoryHandle->iCurrentOffset < 0 ) || ( pstDirectoryHandle->iCurrentOffset >= FILESYSTEM_MAXDIRECTORYENTRYCOUNT ) )
    {
        return NULL;
    }

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 루트 디렉터리에 있는 최대 디렉터리 엔트리의 개수만큼 검색
    pstEntry = pstDirectoryHandle->pstDirectoryBuffer;
    while( pstDirectoryHandle->iCurrentOffset < FILESYSTEM_MAXDIRECTORYENTRYCOUNT )
    {
        // 파일이 존재하면 해당 디렉터리 엔트리를 반환
        if( pstEntry[ pstDirectoryHandle->iCurrentOffset ].dwStartClusterIndex != 0 )
        {
            // 동기화
            kUnlock( &( gs_stFileSystemManager.stMutex ) );
            return &( pstEntry[ pstDirectoryHandle->iCurrentOffset++ ] );
        }

        pstDirectoryHandle->iCurrentOffset++;
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return NULL;
}

//  디렉터리 포인터를 디렉터리의 처음으로 이동
void kRewindDirectory( DIR* pstDirectory )
{
    DIRECTORYHANDLE* pstDirectoryHandle;

    // 핸들 타입이 디렉터리가 아니면 실패
    if( ( pstDirectory == NULL ) || ( pstDirectory->bType != FILESYSTEM_TYPE_DIRECTORY ) )
    {
        return ;
    }

    pstDirectoryHandle = &( pstDirectory->stDirectoryHandle );

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 디렉터리 엔트리의 포인터만 0으로 바꿔줌
    pstDirectoryHandle->iCurrentOffset = 0;

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
}

//  열린 디렉터리를 닫음
int kCloseDirectory( DIR* pstDirectory )
{
    DIRECTORYHANDLE* pstDirectoryHandle;

    // 핸들 타입이 디렉터리가 아니면 실패
    if( ( pstDirectory == NULL ) || ( pstDirectory->bType != FILESYSTEM_TYPE_DIRECTORY ) )
    {
        return -1;
    }
    pstDirectoryHandle = &( pstDirectory->stDirectoryHandle );

    // 동기화
    kLock( &( gs_stFileSystemManager.stMutex ) );

    // 루트 디렉터리의 버퍼를 해제하고 핸들을 반환
    kFreeMemory( pstDirectoryHandle->pstDirectoryBuffer );

    kFreeFileDirectoryHandle( pstDirectory );

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );

    return 0;
}

//  파일 시스템 캐시를 모두 하드 디스크에 씀
BOOL kFlushFileSystemCache( void )
{
    CACHEBUFFER* pstCacheBuffer;
    int iCacheCount;
    int i;

    // 캐시가 비활성화되었다면 함수를 수행할 필요가 없음
    if( gs_stFileSystemManager.bCacheEnable == FALSE )
    {
        return TRUE;
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );

    // 클러스터 링크 테이블 영역의 캐시 정보를 얻어서 내용이 변한 캐시 버퍼를 모두 디스크에 씀
    kGetCacheBufferAndCount( CACHE_CLUSTERLINKTABLEAREA, &pstCacheBuffer, &iCacheCount );
    for( i = 0 ; i < iCacheCount ; i++ )
    {
        // 캐시의 내용이 변했다면 태그에 저정된 위치에 직접 씀
        if( pstCacheBuffer[ i ].bChanged == TRUE )
        {
            if( kInternalWriteClusterLinkTableWithoutCache( pstCacheBuffer[ i ].dwTag, pstCacheBuffer[ i ].pbBuffer ) == FALSE )
            {
                return FALSE;
            }

            // 버퍼의 내용을 하드 디스크에 썼으므로 변경되지 않은 것으로 설정
            pstCacheBuffer[ i ].bChanged = FALSE;
        }
    }

    // 데이터 영역의 캐시 정보를 얻어서 내용이 변한 캐시 버퍼를 모두 디스크에 씀
    kGetCacheBufferAndCount( CACHE_DATAAREA, &pstCacheBuffer, &iCacheCount );
    for( i = 0 ; i < iCacheCount ; i++ )
    {
        // 캐시의 내용이 변했다면 태그에 저장된 위치에 직접 씀
        if( pstCacheBuffer[ i ].bChanged == TRUE )
        {
            if( kInternalWriteClusterWithoutCache( pstCacheBuffer[ i ].dwTag, pstCacheBuffer[ i ].pbBuffer ) == FALSE )
            {
                return FALSE;
            }
            // 버퍼의 내용을 하드 디스크에 썼으므로 변경되지 않은 것으로 설정
            pstCacheBuffer[ i ].bChanged = FALSE;
        }
    }

    // 동기화
    kUnlock( &( gs_stFileSystemManager.stMutex ) );
    return TRUE;
}