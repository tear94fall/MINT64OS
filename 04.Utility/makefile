# 기본적으로 빌드를 수행할 목록
all: ImageMaker SerialTransfer PackageMaker

# 이미지 메이커 빌드를 위해 ImageMaker 디렉터리에서 make 실행
ImageMaker:
	@echo
	@echo =================== Build ImageMaker ===================
	@echo

	make -C 00.ImageMaker

	@echo
	@echo =================== Build Complete ===================
	@echo

# Serial Transfer 빌드를 위해 SerialTransfer 디렉터리에서 make 실행
SerialTransfer:
	@echo
	@echo =================== Build SerialTransfer ===================
	@echo

	make -C 01.SerialTransfer

	@echo
	@echo =================== Build Complete ===================
	@echo

# 패키지 메이커 빌드를 위해 Package Maker 디렉토리에서 make 실행
PackageMaker:
	@echo
	@echo =================== Build PackageMaker ===================
	@echo

	make -C 03.PackageMaker

	@echo
	@echo =================== Build Complete ===================
	@echo
	
# 소스 파일을 제외한 나머지 파일 정리	
clean:
	make -C 00.ImageMaker clean
	make -C 01.SerialTransfer clean
	make -C 03.PackageMaker clean