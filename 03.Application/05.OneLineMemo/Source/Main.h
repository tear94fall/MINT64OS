#ifndef __MAIN_H__
#define __MAIN_H__

// 매크로
// 최대로 표시할 수 있는 문자의 바이트 수
#define MAXOUTPUTLENGTH         60

// 구조체 
// 버퍼의 정보를 저장하는 구조체
typedef struct BufferManagerStruct
{
    //------------------------------------------------------------------------------------------
    // 한글을 조합하는데 필요한 필드
    //------------------------------------------------------------------------------------------
    // 한글 조합을 위해 키 입력을 저장하는 버퍼
    char vcInputBuffer[ 20 ];
    int iInputBufferLength;

    // 조합 중인 한글과 조합이 완료된 한글을 저장하는 버퍼
    char vcOutputBufferForProcessing[ 3 ];
    char vcOutputBufferForComplete[ 3 ];

    //------------------------------------------------------------------------------------------
    // 처리가 완료된 정보를 윈도우 화면에서 출력하는데 필요한 필드
    //------------------------------------------------------------------------------------------
    // 실제로 완료된 정보를 윈도우 화면에 출력하는데 필요한 필드
    // 실제로 화면에 출력하는 정보가 들어있는 버퍼
    char vcOutputBuffer[ MAXOUTPUTLENGTH ];
    int iOutputBufferLength;
} BUFFERMANAGER;

// 함수
void ConvertJaumMoumToLowerCharactor( BYTE* pbInput );

#endif /*__MAIN_H__*/