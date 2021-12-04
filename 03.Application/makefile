# 기본적으로 빌드를 수행할 목록
all: MINT64OSLibrary HelloWorld

# 라이브러리 빌드를 위해 UserLibrary 디렉토리에서 make 실행
MINT64OSLibrary:
	@echo
	@echo =================== Build UserLibrary ===================
	@echo

	make -C UserLibrary

	@echo
	@echo =================== Build Complete ===================
	@echo

# Hello World 빌드를 위해 HelloWorld 디렉토리에서 make 실행
HelloWorld:
	@echo
	@echo =================== Build HelloWorld ===================
	@echo

	make -C 00.HelloWorld

	@echo
	@echo =================== Build Complete ===================
	@echo

# 소스 파일을 제외한 나머지 파일 정리
clean:
	make -C UserLibrary clean
	make -C 00.HelloWorld clean