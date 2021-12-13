# 기본적으로 빌드를 수행할 목록
all: MINT64OSLibrary HelloWorld TextViewer Shooter Hexa

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

# 텍스트 뷰어 빌드를 위해 Text Viewer 디렉토리에서 make 실행
TextViewer:
	@echo
	@echo =================== Build TextViewer ===================
	@echo

	make -C 01.TextViewer

	@echo
	@echo =================== Build Complete ===================
	@echo

# 버블 슈터 빌드를 위해 Shooter 디렉토리에서 make 실행
Shooter:
	@echo
	@echo =================== Build Shooter ===================
	@echo

	make -C 02.Shooter

	@echo
	@echo =================== Build Complete ===================
	@echo

# 헥사 빌드를 위해 Hexa 디렉토리에서 make 실행
Hexa:
	@echo
	@echo =================== Build Hexa ===================
	@echo

	make -C 03.Hexa

	@echo
	@echo =================== Build Complete ===================
	@echo

# 소스 파일을 제외한 나머지 파일 정리
clean:
	make -C UserLibrary clean
	make -C 00.HelloWorld clean
	make -C 01.TextViewer clean
	make -C 02.Shooter clean
	make -C 03.Hexa clean