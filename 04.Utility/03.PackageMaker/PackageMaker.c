#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

// 매크로 정의
// 한 섹터의 바이트 수
#define BYTESOFSECTOR           512

// 패키지의 시그니처
#define PACKAGESIGNATURE        "MINT64OSPACKAGE "

// 파일 이름의 최대 길이, 커널의 FILESYSTEM_MAXFILENAMELENGTH와 같음
#define MAXFILENAMELENGTH       24

// DWORD 타입을 정의
#define DWORD                   unsigned int

// 자료구조 정의
// 1바이트로 정렬
#pragma pack( push, 1 )

// 패키지 헤더 내부의 각 파일 정보를 구성하는 자료구조
typedef struct PackageItemStruct
{
    // 파일 이름
    char vcFileName[ MAXFILENAMELENGTH ];

    // 파일의 크기
    DWORD dwFileLength;
} PACKAGEITEM;

// 패키지 헤더 자료구조
typedef struct PackageHeaderStruct
{
    // MINT64 OS의 패키지 파일을 나타내는 시그니처
    char vcSignature[ 16 ];

    // 패키지 헤더의 전체 크기
    DWORD dwHeaderSize;

    // 패키지 아이템의 시작 위치
    PACKAGEITEM vstItem[ 0 ];
} PACKAGEHEADER;

#pragma pack( pop )


// 함수 선언
int AdjustInSectorSize( int iFd, int iSourceSize );
int CopyFile( int iSourceFd, int iTargetFd );

//  Main 함수
int main(int argc, char* argv[])
{
    int iSourceFd;
    int iTargetFd;
    int iSourceSize;
    int i;
    struct stat stFileData;
    PACKAGEHEADER stHeader;
    PACKAGEITEM stItem;

    // 커맨드 라인 옵션 검사
    if( argc < 2 )
    {
        fprintf( stderr, "[ERROR] PackageMaker.exe app1.elf app2.elf data.txt ...\n" );
        exit( -1 );
    }

    // Package.img 파일을 생성
    if( ( iTargetFd = open( "Package.img", O_RDWR | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE ) ) == -1 )
    {
        fprintf( stderr, "[ERROR] Package.img open fail\n" );
        exit( -1 );
    }

    //------------------------------------------------------------------------------
    // 인자로 전달된 파일 이름으로 패키지 헤더를 먼저 생성
    //------------------------------------------------------------------------------
    printf( "[INFO] Create package header...\n" );

    // 시그니처를 복사하고 헤더의 크기를 계산
    memcpy( stHeader.vcSignature, PACKAGESIGNATURE, sizeof( stHeader.vcSignature ) );
    stHeader.dwHeaderSize = sizeof( PACKAGEHEADER ) + ( argc - 1 ) * sizeof( PACKAGEITEM );
    // 파일에 저장
    if( write( iTargetFd, &stHeader, sizeof( stHeader ) ) != sizeof( stHeader ) )
    {
        fprintf( stderr, "[ERROR] Data write fail\n" );
        exit( -1 );
    }

    // 인자를 돌면서 패키지 헤더의 정보를 채워 넣음
    for( i = 1 ; i < argc ; i++ )
    {
        // 파일 정보를 확인
        if( stat( argv[ i ], &stFileData ) != 0 )
        {
            fprintf( stderr, "[ERROR] %s file open fail\n" );
            exit( -1 );
        }

        // 파일 이름과 길이를 저장
        memset( stItem.vcFileName, 0, sizeof( stItem.vcFileName ) );
        strncpy( stItem.vcFileName, argv[ i ], sizeof( stItem.vcFileName ) );
        stItem.vcFileName[ sizeof( stItem.vcFileName ) - 1 ] = '\0';
        stItem.dwFileLength = stFileData.st_size;

        // 파일에 씀
        if( write( iTargetFd, &stItem, sizeof( stItem ) ) != sizeof( stItem ) )
        {
            fprintf( stderr, "[ERROR] Data write fail\n" );
            exit( -1 );
        }

        printf( "[%d] file: %s, size: %d Byte\n", i, argv[ i ], stFileData.st_size );
    }
    printf( "[INFO] Create complete\n" );

    //------------------------------------------------------------------------------
    //  생성된 패키지 헤더 뒤에 파일의 내용을 복사
    //------------------------------------------------------------------------------
    printf( "[INFO] Copy data file to package...\n" );
    // 인자를 돌면서 파일을 채워 넣음
    iSourceSize = 0;
    for( i = 1 ; i < argc ; i++ )
    {
        // 데이터 파일을 엶
        if( ( iSourceFd = open( argv[ i ], O_RDONLY | O_BINARY ) ) == -1 )
        {
            fprintf( stderr, "[ERROR] %s open fail\n", argv[ 1 ] );
            exit( -1 );
        }

        // 파일의 내용을 패키지 파일에 쓴 뒤에 파일을 닫음
        iSourceSize += CopyFile( iSourceFd, iTargetFd );
        close( iSourceFd );
    }

    // 파일의 크기를 섹터 크기인 512바이트로 맞추기 위해 나머지 부분을 0x00으로 채움
    AdjustInSectorSize( iTargetFd, iSourceSize + stHeader.dwHeaderSize );

    // 성공 메시지 출력
    printf( "[INFO] Total %d Byte copy complete\n", iSourceSize );
    printf( "[INFO] Package file create complete\n" );

    close( iTargetFd );
    return 0;
}

//  현재 위치부터 512 바이트 배수 위치까지 맞춰 0x00으로 채움
int AdjustInSectorSize( int iFd, int iSourceSize )
{
    int i;
    int iAdjustSizeToSector;
    char cCh;
    int iSectorCount;

    iAdjustSizeToSector = iSourceSize % BYTESOFSECTOR;
    cCh = 0x00;

    if( iAdjustSizeToSector != 0 )
    {
        iAdjustSizeToSector = 512 - iAdjustSizeToSector;
        for( i = 0 ; i < iAdjustSizeToSector ; i++ )
        {
            write( iFd, &cCh, 1 );
        }
    }
    else
    {
        printf( "[INFO] File size is aligned 512 byte\n" );
    }

    // 섹터 수를 되돌려줌
    iSectorCount = ( iSourceSize + iAdjustSizeToSector ) / BYTESOFSECTOR;
    return iSectorCount;
}

//  소스 파일(Source FD)의 내용을 목표 파일(Target FD)에 복사하고 그 크기를 되돌려줌
int CopyFile( int iSourceFd, int iTargetFd )
{
    int iSourceFileSize;
    int iRead;
    int iWrite;
    char vcBuffer[ BYTESOFSECTOR ];

    iSourceFileSize = 0;
    while( 1 )
    {
        iRead = read( iSourceFd, vcBuffer, sizeof( vcBuffer ) );
        iWrite = write( iTargetFd, vcBuffer, iRead );

        if( iRead != iWrite )
        {
            fprintf( stderr, "[ERROR] iRead != iWrite.. \n" );
            exit( -1 );
        }

        iSourceFileSize += iRead;
        
        if( iRead != sizeof( vcBuffer ) )
        {
            break;
        }
    }

    return iSourceFileSize;
}