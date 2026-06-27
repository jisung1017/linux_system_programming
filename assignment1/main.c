/*
 * #include <stdio.h>
 * 표준 입출력 라이브러리. ssu_runtime()에서 printf()를 쓰기 위해 포함한다.
 * printf()는 write() syscall을 감싼 버퍼드 I/O 함수이며, 터미널 출력 시
 * 개행 문자를 만날 때 실제로 커널에 write()를 호출하는 라인 버퍼 모드로 동작한다.
 */
#include <stdio.h>

/*
 * #include <stdlib.h>
 * exit() 함수 선언을 위해 포함한다. exit(0)은 정상 종료를 의미하며,
 * 내부적으로 atexit() 핸들러 실행 및 stdio 버퍼 flush를 거쳐 _exit() syscall을 호출한다.
 * return 0과 달리 중첩 호출 환경에서도 즉시 프로세스를 종료할 수 있어 명시적 종료에 활용된다.
 */
#include <stdlib.h>

/*
 * #include <unistd.h>
 * POSIX 표준 syscall 래퍼 헤더. 이 파일에서 직접 호출하는 함수는 없지만,
 * ssu_score.h의 함수 프로토타입들이 pid_t, ssize_t 등의 POSIX 타입 정의에 의존한다.
 * ssu_score.c에서 실제 사용되는 read, write, fork, execl, close, getcwd, chdir,
 * access, lseek, dup2, unlink, rmdir, nanosleep 등이 모두 이 헤더에 선언되어 있다.
 */
#include <unistd.h>

/*
 * #include <sys/time.h>
 * struct timeval과 gettimeofday() 시스템 콜 인터페이스를 제공한다.
 * struct timeval은 { time_t tv_sec(초); suseconds_t tv_usec(마이크로초); } 구조이며,
 * 단순 time()으로는 1초 단위 정밀도밖에 얻을 수 없으므로 마이크로초 해상도를 위해 이 헤더가 필요하다.
 */
#include <sys/time.h>

/*
 * #include "ssu_score.h"
 * 직접 작성한 프로젝트 헤더. <>가 아닌 ""를 사용하여 시스템 디렉토리가 아닌
 * 현재 디렉토리에서 먼저 탐색한다.
 * ssu_score() 프로토타입, BUFLEN/SNUM/QNUM 등 전역 상수, ssu_scoreTable 구조체가 선언되어 있다.
 * 헤더 가드(#ifndef MAIN_H_ / #define MAIN_H_)로 중복 포함 시 재선언 오류를 방지한다.
 */
#include "ssu_score.h"

/*
 * SECOND_TO_MICRO 1000000
 * 1초 = 1,000,000 마이크로초. ssu_runtime()에서 timeval 뺄셈 시
 * tv_usec 올림(borrow) 처리를 위해 tv_usec에 더하는 값이다.
 * 리터럴 숫자 대신 이름 있는 상수로 정의하여 가독성과 유지보수성을 높였다.
 */
#define SECOND_TO_MICRO 1000000

/*
 * ssu_runtime() 전방 선언.
 * 이 함수가 main() 아래에 정의되어 있어 컴파일러가 main 내의 호출을 처리하기 위해
 * 시그니처를 미리 알려준다. 포인터로 받는 이유는 함수 내에서 end_t 필드를 직접
 * 수정하여 경과 시간을 계산하는 구조이기 때문이다.
 */
void ssu_runtime(struct timeval *begin_t, struct timeval *end_t);

int main(int argc, char *argv[])
{
    /*
     * begin_t, end_t: 채점 시작과 종료 시각을 저장할 timeval 구조체.
     * 스택에 자동 할당되므로 별도의 해제가 필요 없다.
     * gettimeofday()로 각각 채점 직전과 직후의 시각을 채운다.
     */
    struct timeval begin_t, end_t;

    /*
     * gettimeofday(&begin_t, NULL)
     * ssu_score() 호출 직전 시각을 begin_t에 기록한다.
     * 두 번째 인자 struct timezone *은 현재 deprecated 상태라 NULL을 전달하는 것이 표준이다.
     * 이 위치가 중요한데, ssu_score() 직전에 있어야 채점 처리 시간만 정확히 포함된다.
     */
    gettimeofday(&begin_t, NULL);

    /*
     * ssu_score(argc, argv)
     * 채점 시스템의 실질적인 메인 함수. 옵션 파싱부터 정답 전처리, 학생 채점, 결과 기록까지
     * 전 과정이 여기서 수행된다.
     * argc: 명령행 인자 개수(프로그램명 포함), argv[1]=학생디렉토리, argv[2]=정답디렉토리
     */
    ssu_score(argc, argv);

    /*
     * gettimeofday(&end_t, NULL)
     * ssu_score() 반환 직후 종료 시각을 기록한다.
     * ssu_score() 내부에서 exit()가 호출되는 경우에는 여기까지 도달하지 못한다.
     */
    gettimeofday(&end_t, NULL);

    /*
     * ssu_runtime(&begin_t, &end_t)
     * 두 시각의 차이를 계산하여 총 실행 시간을 출력한다.
     * 포인터를 전달하는 이유: 함수 내에서 end_t의 tv_sec/tv_usec를 직접 덮어써서
     * 경과 시간을 계산하기 때문에 참조 전달이 필요하다.
     */
    ssu_runtime(&begin_t, &end_t);

    /*
     * exit(0): 정상 종료. stdio 버퍼를 flush하고 프로세스를 종료한다.
     */
    exit(0);
}

/*
 * ssu_runtime():
 * begin_t와 end_t의 차이를 계산하여 채점 총 실행 시간을 출력한다.
 *
 * timeval 뺄셈 주의사항:
 * tv_usec는 0~999999 범위만 유효하다.
 * end.tv_usec가 begin.tv_usec보다 작으면 바로 빼면 음수가 나오므로,
 * tv_sec에서 1을 빌려(borrow) tv_usec에 1,000,000을 더한 뒤 빼야 한다.
 *
 * 예) begin={10, 800000}, end={12, 200000}
 *   → 올림 처리 전: 2초, 200000-800000 = -600000 (잘못됨)
 *   → 올림 처리 후: 1초, 1200000-800000 = 400000
 *   → 출력: "Runtime: 1:400000(sec:usec)"
 *
 * 인자를 포인터로 받는 이유: end_t 필드를 직접 수정하는 구조이므로 참조가 필요하다.
 */
void ssu_runtime(struct timeval *begin_t, struct timeval *end_t)
{
    /*
     * 초 단위 차이를 먼저 계산한다.
     * 아직 tv_usec의 올림(borrow) 여부가 반영되지 않은 잠정적인 값이다.
     */
    end_t->tv_sec -= begin_t->tv_sec;

    /*
     * tv_usec 올림(borrow) 처리.
     * end의 tv_usec가 begin의 tv_usec보다 작은 경우,
     * tv_sec을 1 감소시키고 tv_usec에 1,000,000(=1초)을 더해 올림을 보정한다.
     */
    if (end_t->tv_usec < begin_t->tv_usec) {
        end_t->tv_sec--;
        end_t->tv_usec += SECOND_TO_MICRO;
    }

    /*
     * 올림 처리 완료 후 마이크로초 차이를 계산한다.
     * 이 시점에서는 항상 양수 값이 보장된다.
     */
    end_t->tv_usec -= begin_t->tv_usec;

    /*
     * %ld: tv_sec, tv_usec는 long 타입이므로 %ld를 사용한다.
     * %06ld: 마이크로초를 6자리 고정으로 출력하고 앞자리를 0으로 채운다.
     * 예) 5초 37000마이크로초 → "Runtime: 5:037000(sec:usec)"
     */
    printf("Runtime: %ld:%06ld(sec:usec)\n", end_t->tv_sec, end_t->tv_usec);
}
