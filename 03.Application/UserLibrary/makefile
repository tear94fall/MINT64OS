######################################################################
# 빌드 환경 및 규칙 설정
######################################################################
# 컴파일러와 AR 프로그램의 옵션 정의
NASM64 = nasm -f elf64
GCC64 = x86_64-pc-linux-gcc.exe -c -m64 -ffreestanding -mcmodel=large -fno-common
AR64 = x86_64-pc-linux-ar.exe rcs

# 디렉토리 정의
OBJECTDIRECTORY = Temp
SOURCEDIRECTORY = Source

######################################################################
# 빌드 항목과 빌드 방법 설정
######################################################################
# 기본적으로 빌드를 수행할 목록
all: prepare libMINT64OS

# 오브젝트 파일이 위치할 디렉터리를 생성
prepare:
	mkdir -p $(OBJECTDIRECTORY)

# 커널의 C 소스 파일에 대한 의존성 정보 생성
dep:
	@echo === Make Dependency File ===
	make -C $(OBJECTDIRECTORY) -f ../makefile InternalDependency
	#echo === Dependency Search Complete ===

# 디렉터리를 오브젝트 파일 디렉토리로 이동해서 의존성 파일과 실행 파일을 생성
libMINT64OS: dep
	make -C $(OBJECTDIRECTORY) -f ../makefile libMINT64OS.a
	cp $(OBJECTDIRECTORY)/libMINT64OS.a ./

# 소스 파일을 제외한 나머지 파일 정리
clean:
	rm -f *.a
	rm -f $(OBJECTDIRECTORY)/*.*

######################################################################
# Make에 의해 다시 호출되는 부분, Temp 디렉터리를 기준으로 수행됨
######################################################################
# 빌드할 C 소스 파일 정의, Temp 디렉토리를 기준으로 설정
CSOURCEFILES = $(wildcard ../$(SOURCEDIRECTORY)/*.c)
ASSEMBLYSOURCEFILES = $(wildcard ../$(SOURCEDIRECTORY)/*.asm)
COBJECTFILES = $(notdir $(patsubst %.c,%.o,$(CSOURCEFILES)))
ASSEMBLYOBJECTFILES = $(notdir $(patsubst %.asm,%.o,$(ASSEMBLYSOURCEFILES)))

# .c 파일을 .o 파일로 바꾸는 규칙 정의
%.o: ../$(SOURCEDIRECTORY)/%.c
	$(GCC64) -c $<

# .asm 파일을 .o 파일로 바꾸는 규칙 정의
%.o: ../$(SOURCEDIRECTORY)/%.asm
	$(NASM64) -o $@ $<

# 의존성에 관련된 파일을 생성
InternalDependency:
	$(GCC64) -MM $(CSOURCEFILES) > Dependency.dep

# 실제 응용프로그램에서 사용하는 라이브러리 파일들을 정적 라이브러리로 묶음
libMINT64OS.a: $(ASSEMBLYOBJECTFILES) $(COBJECTFILES)
	$(AR64) $@ $^

# 현재 디렉토리의 파일 중, dependency 파일이 있으면 make에 포함
ifeq (Dependency.dep, $(wildcard Dependency.dep))
include Dependency.dep
endif