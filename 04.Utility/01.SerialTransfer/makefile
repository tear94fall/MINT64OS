# 기본적으로 빌드를 수행할 목록
all: NetworkTransfer.exe

# ImageMaker 빌드
NetworkTransfer.exe: NetworkTransfer.c
	gcc -o $@ $<
	
# 소스 파일을 제외한 나머지 파일 정리	
clean:
	rm -f NetworkTransfer.exe