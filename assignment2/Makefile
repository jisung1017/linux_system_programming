# 명세상 사용자는 make로 세 실행 파일(ssu_clean, fmd5, help)을 한 번에 만들 수
# 있어야 한다. OpenSSL EVP 기반 MD5 계산을 위해 fmd5 링크 단계에만 -lcrypto를
# 붙이고, 프롬프트와 help 프로그램은 표준 C 라이브러리만 사용한다.
CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11
LDFLAGS = -lcrypto
TARGETS = ssu_clean fmd5 help

# 기본 목표는 과제에서 실행해야 하는 모든 바이너리를 생성한다.
all : $(TARGETS)

# 최상위 프롬프트 프로그램. 내장 명령어 실행은 런타임에 fork/exec로 처리되므로
# 별도 라이브러리 링크가 필요 없다.
ssu_clean : ssu_clean.o
	$(CC) -o $@ $^

# fmd5는 MD5 해시 계산을 OpenSSL EVP API로 수행하므로 libcrypto를 링크한다.
fmd5 : fmd5.o
	$(CC) -o $@ $^ $(LDFLAGS)

# help 실행 파일은 ssu_clean과 fmd5의 오류 처리 경로에서도 호출된다.
help : ssu_help.o
	$(CC) -o $@ $^

# 각 오브젝트 규칙은 소스 파일과 1:1로 대응한다. 컴파일 경고를 최대한 확인하기
# 위해 공통 CFLAGS를 모두 적용한다.
ssu_clean.o : ssu_clean.c
	$(CC) $(CFLAGS) -c $<

fmd5.o : fmd5.c
	$(CC) $(CFLAGS) -c $<

ssu_help.o : ssu_help.c
	$(CC) $(CFLAGS) -c $<

# 채점 또는 재빌드 전에 생성물을 지우기 위한 정리 목표이다.
clean :
	rm -f *.o $(TARGETS)
