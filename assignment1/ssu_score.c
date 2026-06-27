/* printf, fprintf, fopen, fscanf, fclose, scanf 등 표준 입출력 함수 사용 */
#include <stdio.h>
/* exit(), qsort(), atof(), atoi() 등 표준 유틸리티 함수 사용 */
#include <stdlib.h>
/* time(), difftime() 사용. execute_program()의 타임아웃 계산에 필요하다 */
#include <time.h>
/* kill(), SIGKILL 상수 사용. 타임아웃 초과 시 학생 프로세스를 강제 종료할 때 쓴다 */
#include <signal.h>
/* memset(), memcpy(), strcmp(), strcpy(), strncpy(), strlen(), strcat(),
   strtok(), strrchr(), strstr() 등 문자열 및 메모리 조작 함수 사용 */
#include <string.h>
/* pid_t, off_t, ssize_t 등 POSIX 기본 타입 정의. fork() 반환 타입인 pid_t가 여기에 있다 */
#include <sys/types.h>
/* waitpid(), WNOHANG 등 자식 프로세스 상태 관리 함수와 매크로 사용 */
#include <sys/wait.h>
/* opendir(), readdir(), closedir(), DIR, struct dirent 사용.
   학생/정답 디렉토리를 순회할 때 사용된다 */
#include <dirent.h>
/* read(), write(), fork(), execvp(), close(), getcwd(), chdir(),
   access(), lseek(), dup2(), unlink(), rmdir(), nanosleep() 등 핵심 POSIX syscall 래퍼 */
#include <unistd.h>
/* open(), creat(), O_RDONLY, O_WRONLY, O_APPEND 등 파일 제어 상수와 함수 사용 */
#include <fcntl.h>
/* stat(), lstat(), mkdir(), struct stat, S_ISDIR() 등
   파일/디렉토리 메타데이터 조회 및 생성 함수 사용 */
#include <sys/stat.h>
/* errno 변수 및 오류 상수 선언. syscall 실패 원인 추적에 활용된다 */
#include <errno.h>
/* 프로젝트 전역 상수(BUFLEN, SNUM, QNUM 등), 구조체, 함수 프로토타입 선언 */
#include "ssu_score.h"
/* blank.c의 AST 파싱 함수(make_tokens, make_tree, compare_tree 등) 및
   node 구조체 타입 선언. score_blank()와 preprocess_blank_answers()에서 사용한다 */
#include "blank.h"

/*
 * score_table[QNUM]: 채점 기준표. 문제 파일명(qname)과 배점(score)을 저장한다.
 * set_scoreTable()에서 채워지고, score_student()에서 각 문제를 채점할 때 참조한다.
 * QNUM=100 크기로 고정 배열로 선언하여 동적 할당 없이 사용한다.
 */
struct ssu_scoreTable score_table[QNUM];

/*
 * id_table[SNUM][10]: 학생 학번 목록. set_idTable()이 채우고 sort_idTable()로 정렬된다.
 * 학번이 최대 9자리라고 가정하여 10바이트(9자리+null)로 선언했다.
 * 이 배열의 인덱스 순서가 score.csv의 학생 기록 순서를 결정한다.
 */
char id_table[SNUM][10];

/*
 * stuDir: 학생 제출 디렉토리의 절대 경로.
 * ssu_score()에서 argv[1]을 받아 chdir+getcwd로 절대 경로로 변환하여 저장한다.
 * 절대 경로로 변환하는 이유: 이후 chdir()로 작업 디렉토리가 바뀌어도
 * 경로 접근이 항상 올바르게 동작하도록 하기 위함이다.
 */
char stuDir[BUFLEN];

/*
 * ansDir: 정답 디렉토리의 절대 경로. stuDir과 동일한 방식으로 변환된다.
 */
char ansDir[BUFLEN];

/*
 * errorDir: -e 옵션 지정 시 컴파일 에러 파일을 저장할 디렉토리 경로.
 * -e 옵션이 없으면 에러 파일은 임시로 생성했다가 check_error_warning() 호출 후 삭제된다.
 */
char errorDir[BUFLEN];

/*
 * threadFiles[ARGNUM][FILELEN]: -t 옵션으로 지정된 문제명 목록.
 * 이 목록에 있는 문제의 C 파일은 gcc 컴파일 시 -lpthread 옵션이 추가된다.
 */
char threadFiles[ARGNUM][FILELEN];

/*
 * cIDs[ARGNUM][FILELEN]: -c 옵션으로 점수를 조회할 학생 학번 목록.
 * do_cOption()에서 score.csv를 읽어 이 목록에 있는 학번의 점수만 출력한다.
 */
char cIDs[ARGNUM][FILELEN];

/*
 * 옵션 플래그들. check_option()에서 getopt()로 설정된다.
 * 전역으로 선언한 이유: 채점 흐름의 여러 함수에서 이 상태를 참조해야 하기 때문이다.
 * eOption: -e 활성화 시 컴파일 에러 파일을 errorDir에 보존한다.
 * tOption: -t 활성화 시 threadFiles에 등록된 문제를 -lpthread로 컴파일한다.
 * pOption: -p 활성화 시 각 학생 채점 완료 후 점수와 전체 평균을 출력한다.
 * cOption: -c 활성화 시 채점 후 특정 학생의 점수를 score.csv에서 조회해 출력한다.
 */
int eOption = false;
int tOption = false;
int pOption = false;
int cOption = false;

static char ans_compiled_cache[QNUM][FILELEN]; /* 컴파일 완료된 qname을 저장하는 배열 */
static int  ans_compiled_cnt = 0;              /* 현재 등록된 컴파일 완료 항목 수 */
static char ans_executed_cache[QNUM][FILELEN]; /* 실행 결과 생성 완료된 qname 배열 */
static int  ans_executed_cnt  = 0;             /* 현재 등록된 실행 완료 항목 수 */

/* =====================================================================
 * 정답 AST 캐시
 * 빈칸 문제 정답 파일을 학생마다 반복 파싱하는 비용을 없애기 위해 도입했다.
 * preprocess_blank_answers()에서 fork 이전에 구성하므로
 * 모든 자식 프로세스가 COW로 이 캐시를 읽기 전용으로 공유한다.
 * 자식들이 캐시를 수정하지 않으므로 race condition 없이 안전하게 접근 가능하다.
 *
 * ANS_TREE_MAX=32: 하나의 빈칸 문제 정답 파일에서 허용하는 최대 정답 후보 수.
 *   정답 파일 한 줄마다 하나의 AST가 생성되며 최대 32개까지 저장한다.
 * BLANK_Q_MAX=64: 처리할 수 있는 최대 빈칸 문제 수.
 * ===================================================================== */
#define ANS_TREE_MAX  32
#define BLANK_Q_MAX   64

/*
 * ans_tree_entry: 하나의 빈칸 문제 정답 파일에 대한 캐시 항목이다.
 * filename: 정답 파일명 (예: "1-1.txt"). 캐시 조회 시 strcmp로 비교한다.
 * roots[]: 각 정답 줄을 파싱한 AST의 루트 포인터 배열. 최대 ANS_TREE_MAX개.
 * has_semicolon[]: 해당 정답 줄에 세미콜론(;)이 있는지 여부. 비교 시 학생 답안과 맞춰야 한다.
 * cnt: 실제로 저장된 정답 후보 AST 수.
 */
typedef struct {
    char   filename[FILELEN];
    node  *roots[ANS_TREE_MAX];
    int    has_semicolon[ANS_TREE_MAX];
    int    cnt;
} ans_tree_entry;

/*
 * ans_tree_cache[BLANK_Q_MAX]: 빈칸 문제별 AST 캐시 배열.
 * ans_tree_cnt: 현재 캐시에 저장된 빈칸 문제 수.
 * static으로 선언하여 이 파일 내에서만 접근 가능하도록 한다.
 */
static ans_tree_entry ans_tree_cache[BLANK_Q_MAX];
static int            ans_tree_cnt = 0;

/*
 * ---- 캐시 조회/등록 함수 ----
 * 선형 탐색(strcmp)으로 구현했다. QNUM=100 이하에서 최대 100번 비교이므로
 * 해시 테이블을 도입할 필요 없이 이 방식으로도 충분히 빠르다.
 */

/*
 * is_ans_compiled(): qname이 컴파일 완료 캐시에 있으면 true를 반환한다.
 * strcmp()로 문자열을 정확히 비교하므로 대소문자나 공백 차이에 안전하다.
 */
int is_ans_compiled(char *qname)
{
    int i;
    /* ans_compiled_cnt까지만 순회. 초기화되지 않은 슬롯은 검사하지 않는다 */
    for (i = 0; i < ans_compiled_cnt; i++)
        if (!strcmp(ans_compiled_cache[i], qname)) return true;
    return false;
}

/*
 * mark_ans_compiled(): qname을 컴파일 완료 캐시에 등록한다.
 * strncpy로 FILELEN-1 바이트까지 복사하고 마지막 바이트를 '\0'으로 강제 설정하여
 * 소스 문자열이 FILELEN 이상이어도 널 종단을 보장한다.
 */
void mark_ans_compiled(char *qname)
{
    if (ans_compiled_cnt < QNUM) {
        strncpy(ans_compiled_cache[ans_compiled_cnt], qname, FILELEN - 1);
        ans_compiled_cache[ans_compiled_cnt++][FILELEN - 1] = '\0';
    }
}

/*
 * is_ans_executed(): qname의 실행 결과(.stdout)가 이미 생성되었으면 true를 반환한다.
 * 학생마다 정답 프로그램을 재실행하지 않기 위한 캐시 조회 함수다.
 */
int is_ans_executed(char *qname)
{
    int i;
    for (i = 0; i < ans_executed_cnt; i++)
        if (!strcmp(ans_executed_cache[i], qname)) return true;
    return false;
}

/* mark_ans_executed(): qname을 실행 완료 캐시에 등록한다 */
void mark_ans_executed(char *qname)
{
    if (ans_executed_cnt < QNUM) {
        strncpy(ans_executed_cache[ans_executed_cnt], qname, FILELEN - 1);
        ans_executed_cache[ans_executed_cnt++][FILELEN - 1] = '\0';
    }
}

/*
 * get_ans_tree(): filename에 해당하는 AST 캐시 항목을 반환한다.
 * score_blank()에서 이 함수로 캐시를 조회하고, 히트 시 정답 파일 파싱을 건너뛴다.
 * 없으면 NULL을 반환하고 score_blank()는 기존 방식으로 폴백한다.
 */
ans_tree_entry *get_ans_tree(char *filename)
{
    int i;
    for (i = 0; i < ans_tree_cnt; i++)
        if (!strcmp(ans_tree_cache[i].filename, filename))
            return &ans_tree_cache[i];
    return NULL;
}

/*
 * preprocess_blank_answers():
 * 빈칸 문제 정답 파일 전체를 사전 파싱하여 ans_tree_cache에 AST를 저장한다.
 * 기존 score_blank()는 학생마다 정답 파일 open + make_tokens() + make_tree()를
 * 반복했다. 학생 100명, 빈칸 10개 기준 파싱 1,000번 반복이 이 함수로 10번으로 줄어든다.
 * score_students()로 fork하기 직전에 호출하므로 자식들이 캐시를 COW로 공유한다.
 */
void preprocess_blank_answers(void)
{
    int   i;
    int   size = sizeof(score_table) / sizeof(score_table[0]);
    char  qname[FILELEN];
    char  ans_path[BUFLEN];
    char  a_answer[BUFLEN];
    char  tokens[TOKEN_CNT][MINLEN];
    int   idx;
    int   fd_ans;
    int   has_sc;
    ans_tree_entry *entry;

    for (i = 0; i < size; i++) {
        if (score_table[i].score == 0) break;
        if (get_file_type(score_table[i].qname) != TEXTFILE) continue;
        if (ans_tree_cnt >= BLANK_Q_MAX) break;

        /* strrchr()로 마지막 '.'를 찾아 확장자 길이만큼 뺀 길이로 memcpy -> 확장자 제거 */
        memset(qname, 0, sizeof(qname));
        memcpy(qname, score_table[i].qname,
               strlen(score_table[i].qname) -
               strlen(strrchr(score_table[i].qname, '.')));

        sprintf(ans_path, "%s/%s/%s", ansDir, qname, score_table[i].qname);

        /* open(): 읽기 전용(O_RDONLY)으로 열어 fd를 반환. 실패 시 -1 */
        fd_ans = open(ans_path, O_RDONLY);
        if (fd_ans < 0) continue;

        entry = &ans_tree_cache[ans_tree_cnt];
        /* memset으로 구조체 전체 초기화. 이전에 쓰인 쓰레기 값 방지 */
        memset(entry, 0, sizeof(ans_tree_entry));
        strncpy(entry->filename, score_table[i].qname, FILELEN - 1);
        entry->cnt = 0;

        while (entry->cnt < ANS_TREE_MAX) {
            for (idx = 0; idx < TOKEN_CNT; idx++)
                memset(tokens[idx], 0, sizeof(tokens[idx]));

            get_answer(fd_ans, a_answer);
            if (!strcmp(a_answer, "")) break;

            /* ltrim은 포인터 이동(in-place 아님), rtrim은 '\0' 삽입(in-place) */
            strcpy(a_answer, ltrim(rtrim(a_answer)));

            has_sc = 0;
            if (a_answer[strlen(a_answer) - 1] == ';') {
                has_sc = 1;
                a_answer[strlen(a_answer) - 1] = '\0';
            }

            /* make_tokens(): 문자열을 파싱하여 토큰 배열로 분리. 실패 시 0 반환 */
            if (!make_tokens(a_answer, tokens)) continue;

            idx = 0;
            /* make_tree(): 토큰 배열로부터 AST를 구성하고 루트 노드를 반환한다 */
            entry->roots[entry->cnt]         = make_tree(NULL, tokens, &idx, 0);
            entry->has_semicolon[entry->cnt] = has_sc;
            entry->cnt++;
        }

        /* close(): fd를 반납. close하지 않으면 프로세스 fd 한계에 도달할 수 있다 */
        close(fd_ans);
        ans_tree_cnt++;
    }
}

/*
 * ssu_score(): 전체 채점 흐름의 메인 제어 함수.
 * -h 확인 -> 경로 검증 -> 옵션 파싱 -> 정답 전처리(병렬) -> 빈칸 AST 캐시 -> 학생 채점(병렬)
 */
void ssu_score(int argc, char *argv[])
{
    char saved_path[BUFLEN];
    int  i;

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-h")) { print_usage(); return; }
    }

    memset(saved_path, 0, BUFLEN);
    if (argc >= 3 && strcmp(argv[1], "-c") != 0) {
        strcpy(stuDir, argv[1]);
        strcpy(ansDir, argv[2]);
    }

    if (!check_option(argc, argv)) exit(1);

    if (!eOption && !tOption && !pOption && cOption) {
        do_cOption(cIDs);
        return;
    }

    /* getcwd(): 현재 작업 디렉토리의 절대 경로를 얻는다.
       chdir() 후 getcwd()하는 이유: 상대 경로 인자를 절대 경로로 변환하기 위함이다 */
    getcwd(saved_path, BUFLEN);
    if (chdir(stuDir) < 0) { fprintf(stderr, "%s doesn't exist\n", stuDir); return; }
    getcwd(stuDir, BUFLEN);
    chdir(saved_path);
    if (chdir(ansDir) < 0) { fprintf(stderr, "%s doesn't exist\n", ansDir); return; }
    getcwd(ansDir, BUFLEN);
    chdir(saved_path);

    set_scoreTable(ansDir);
    set_idTable(stuDir);

    /* 정답 전처리는 반드시 fork() 이전에 수행해야 한다.
       fork 이후 자식들이 동시에 동일 파일을 컴파일/파싱하면 파일 충돌이 발생하고
       중복 작업이 자식 수만큼 반복된다. fork 이전 완료 시 캐시가 COW로 공유된다 */
    preprocess_answers_parallel();
    preprocess_blank_answers();

    printf("grading student's test papers..\n");
    score_students();

    if (cOption) do_cOption(cIDs);
}

/*
 * build_argv():
 * 명령 문자열(command)을 공백 기준으로 분리하여 execvp()에 전달할 argv 배열을 구성한다.
 * strtok_r()을 사용하여 재진입 안전성을 확보한다.
 * buf에 command를 복사한 뒤 제자리에서 분리하므로 원본 문자열은 변경되지 않는다.
 *
 * 반환값: 분리된 인자 수. argv의 마지막 슬롯은 반드시 NULL이 설정된다.
 * 주의: argv[0]에는 실행 파일 경로가 그대로 저장되며 execvp()의 첫 번째 인자로 쓰인다.
 */
static int build_argv(const char *command, char *buf, char **argv, int max_argc)
{
    char *saveptr;
    char *token;
    int   argc = 0;

    /* strncpy로 command를 buf에 복사. strtok_r이 buf를 직접 수정하기 때문이다 */
    strncpy(buf, command, BUFLEN - 1);
    buf[BUFLEN - 1] = '\0';

    token = strtok_r(buf, " \t", &saveptr);
    while (token != NULL && argc < max_argc - 1) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }
    /* execvp()는 인자 배열의 끝이 NULL임을 요구한다 */
    argv[argc] = NULL;
    return argc;
}

/*
 * preprocess_answers_parallel():
 * 정답 C 파일의 컴파일과 실행 결과 생성을 문제 단위로 병렬화한다.
 * 기존 순차 처리는 gcc 0.5~2초 * N문제 = 최대 수십 초 대기였다.
 * fork로 병렬화하면 전체 시간이 가장 오래 걸리는 1문제로 수렴한다.
 * 자식에서 변경한 캐시는 COW로 부모와 분리되므로,
 * 자식 종료 후 부모에서 access()로 파일 존재를 직접 확인하여 캐시를 재구성한다.
 */
void preprocess_answers_parallel(void)
{
    int    i;
    int    size = sizeof(score_table) / sizeof(score_table[0]);
    char   qname[FILELEN];
    char   exe_path[BUFLEN];
    char   src_path[BUFLEN];
    char   stdout_path[BUFLEN];
    char   gcc_cmd[BUFLEN];
    char   err_path[BUFLEN];
    int    type;
    pid_t  pids[QNUM]; /* pid_t: POSIX 프로세스 ID 타입. sys/types.h에 정의됨 */
    int    pcnt = 0;
    int    pid_i;

    memset(pids, 0, sizeof(pids));

    for (i = 0; i < size; i++) {
        if (score_table[i].score == 0) break;
        type = get_file_type(score_table[i].qname);
        if (type != CFILE) continue;

        memset(qname, 0, sizeof(qname));
        memcpy(qname, score_table[i].qname,
               strlen(score_table[i].qname) -
               strlen(strrchr(score_table[i].qname, '.')));

        sprintf(exe_path,    "%s/%s/%s.exe",    ansDir, qname, qname);
        sprintf(src_path,    "%s/%s/%s",         ansDir, qname, score_table[i].qname);
        sprintf(stdout_path, "%s/%s/%s.stdout",  ansDir, qname, qname);

        /* stat(): 파일 메타데이터를 가져온다. st_mtime으로 최종 수정 시각을 비교하여
           소스보다 exe/stdout이 최신이면 재컴파일/재실행을 생략한다 */
        {
            struct stat src_st, exe_st, out_st;
            int compiled_ok = 0, executed_ok = 0;

            if (stat(src_path, &src_st) == 0 &&
                stat(exe_path, &exe_st) == 0 &&
                exe_st.st_mtime >= src_st.st_mtime)
                compiled_ok = 1;

            if (compiled_ok &&
                stat(exe_path,    &exe_st) == 0 &&
                stat(stdout_path, &out_st) == 0 &&
                out_st.st_mtime >= exe_st.st_mtime)
                executed_ok = 1;

            if (compiled_ok) mark_ans_compiled(qname);
            if (executed_ok) mark_ans_executed(qname);
            if (compiled_ok && executed_ok) continue;
        }

        /* fork(): 현재 프로세스를 복제. 부모에게는 자식 pid, 자식에게는 0을 반환 */
        pids[pcnt] = fork();
        if (pids[pcnt] < 0) { pcnt++; continue; }

        if (pids[pcnt] == 0) {
            if (!is_ans_compiled(qname)) {
                int   err_fd;
                off_t err_size; /* off_t: 파일 오프셋 타입. lseek 반환 타입과 일치해야 한다 */

                /* creat(): 파일 생성 또는 기존 파일을 0바이트로 초기화. 0666은 rw-rw-rw- */
                sprintf(err_path, "%s/%s/%s_pre_err.txt", ansDir, qname, qname);
                err_fd = creat(err_path, 0666);

                /* snprintf(): 버퍼 크기를 지정하여 오버플로우를 방지하는 sprintf */
                if (tOption && is_thread(qname))
                    snprintf(gcc_cmd, sizeof(gcc_cmd), "gcc -o %s %s -lpthread", exe_path, src_path);
                else
                    snprintf(gcc_cmd, sizeof(gcc_cmd), "gcc -o %s %s", exe_path, src_path);

                /*
                 * redirection(): gcc 명령을 쉘 없이 직접 fork/dup2/execvp로 실행한다.
                 * STDERR를 err_fd로 교체하여 컴파일 에러 메시지를 파일에 기록한다.
                 */
                redirection(gcc_cmd, err_fd, STDERR);

                /* lseek(fd, 0, SEEK_END): 파일 끝으로 이동하여 파일 크기를 얻는다.
                   에러 파일에 내용이 있으면(>0) 컴파일 실패로 판단 */
                err_size = lseek(err_fd, 0, SEEK_END);
                close(err_fd);
                /* unlink(): 파일의 디렉토리 항목(링크)을 제거하는 syscall */
                unlink(err_path);

                if (err_size > 0) exit(1);
            }

            if (!is_ans_executed(qname)) {
                int out_fd = creat(stdout_path, 0666);
                /*
                 * redirection(): 정답 실행 파일을 직접 execvp로 실행한다.
                 * STDOUT을 out_fd로 교체하여 실행 결과를 stdout 파일에 저장한다.
                 */
                redirection(exe_path, out_fd, STDOUT);
                close(out_fd);
            }

            exit(0);
        }
        pcnt++;
    }

    /* waitpid(): 지정 pid 자식이 종료될 때까지 블로킹 대기. 수거 안 하면 좀비 프로세스 발생 */
    for (pid_i = 0; pid_i < pcnt; pid_i++) {
        if (pids[pid_i] > 0) waitpid(pids[pid_i], NULL, 0);
    }

    /* COW로 분리된 자식의 캐시 변경이 부모에 반영되지 않으므로,
       access()로 파일 존재를 직접 확인하여 부모 캐시를 재구성한다 */
    ans_compiled_cnt = 0;
    ans_executed_cnt = 0;
    for (i = 0; i < size; i++) {
        if (score_table[i].score == 0) break;
        if (get_file_type(score_table[i].qname) != CFILE) continue;

        memset(qname, 0, sizeof(qname));
        memcpy(qname, score_table[i].qname,
               strlen(score_table[i].qname) -
               strlen(strrchr(score_table[i].qname, '.')));

        sprintf(exe_path,    "%s/%s/%s.exe",    ansDir, qname, qname);
        sprintf(stdout_path, "%s/%s/%s.stdout",  ansDir, qname, qname);

        /* access(path, F_OK): 파일 존재 여부만 확인. 실패 시 -1 반환 */
        if (access(exe_path, F_OK) == 0)     mark_ans_compiled(qname);
        if (access(stdout_path, F_OK) == 0)  mark_ans_executed(qname);
    }
}

/*
 * check_option():
 * getopt()로 명령줄 옵션을 파싱하고 전역 플래그를 설정한다.
 * "e:thpc"에서 ':'가 붙은 -e는 다음 인자를 optarg로 받는다.
 * optind: getopt 내부 전역 변수, 다음 처리할 argv 인덱스를 가리킨다.
 */
int check_option(int argc, char *argv[])
{
    int i, j, c;

    while ((c = getopt(argc, argv, "e:thpc")) != -1) {
        switch (c) {
        case 'e':
            eOption = true;
            strcpy(errorDir, optarg); /* optarg: 현재 옵션의 인자 문자열 포인터 */
            /* access()로 존재 확인 후 mkdir() 생성. rmdirs()로 기존 내용 비우고 재생성 */
            if (access(errorDir, F_OK) < 0) mkdir(errorDir, 0755);
            else { rmdirs(errorDir); mkdir(errorDir, 0755); }
            break;
        case 't':
            tOption = true;
            i = optind; j = 0;
            while (i < argc && argv[i][0] != '-') {
                if (j >= ARGNUM) printf("Maximum Number of Argument Exceeded.  :: %s\n", argv[i]);
                else strcpy(threadFiles[j], argv[i]);
                i++; j++;
            }
            break;
        case 'p': pOption = true; break;
        case 'c':
            cOption = true;
            i = optind; j = 0;
            while (i < argc && argv[i][0] != '-') {
                if (j >= ARGNUM) printf("Maximum Number of Argument Exceeded.  :: %s\n", argv[i]);
                else strcpy(cIDs[j], argv[i]);
                i++; j++;
            }
            break;
        case '?':
            /* optopt: 인식하지 못한 옵션 문자가 저장되는 getopt 내부 전역 변수 */
            printf("Unkown option %c\n", optopt);
            return false;
        }
    }
    return true;
}

/*
 * do_cOption():
 * score.csv를 fopen으로 읽고 strtok으로 CSV를 파싱하여 지정 학생의 총점을 출력한다.
 * strtok은 내부 정적 포인터를 사용하므로 중첩 호출이 불가능하다.
 */
void do_cOption(char (*ids)[FILELEN])
{
    FILE *fp;
    char  tmp[BUFLEN];
    char *p, *saved;

    /* fopen(): FILE 스트림을 열어 버퍼드 I/O를 제공. "r"은 읽기 전용 모드 */
    if ((fp = fopen("score.csv", "r")) == NULL) {
        fprintf(stderr, "file open error for score.csv\n");
        return;
    }
    fscanf(fp, "%s\n", tmp); /* 첫 줄(헤더) 읽어 건너뜀 */
    while (fscanf(fp, "%s\n", tmp) != EOF) {
        p = strtok(tmp, ",");
        if (!is_exist(ids, tmp)) continue;
        printf("%s's score : ", tmp);
        while ((p = strtok(NULL, ",")) != NULL) saved = p; /* NULL 전달 시 이전 위치에서 계속 분리 */
        printf("%s\n", saved);
    }
    /* fclose(): FILE 스트림을 닫고 버퍼를 flush하며 fd를 반납한다 */
    fclose(fp);
}

int is_exist(char (*src)[FILELEN], char *target)
{
    int i = 0;
    while (1) {
        if (i >= ARGNUM)              return false;
        if (!strcmp(src[i], ""))      return false;
        if (!strcmp(src[i++], target)) return true;
    }
    return false;
}

/*
 * set_scoreTable():
 * access()로 score_table.csv 존재를 확인하고 읽거나 생성한다.
 */
void set_scoreTable(char *ansDir)
{
    char filename[FILELEN];
    sprintf(filename, "%s/%s", ansDir, "score_table.csv");
    if (access(filename, F_OK) == 0) read_scoreTable(filename);
    else { make_scoreTable(ansDir); write_scoreTable(filename); }
}

/*
 * read_scoreTable():
 * fscanf의 "%[^,],%s" 형식으로 CSV 한 줄씩 읽어 score_table[]을 채운다.
 * %[^,]: 쉼표가 아닌 문자의 연속 -> 문제 파일명 추출
 */
void read_scoreTable(char *path)
{
    FILE *fp;
    char  qname[FILELEN], score[BUFLEN];
    int   idx = 0;

    if ((fp = fopen(path, "r")) == NULL) {
        fprintf(stderr, "file open error for %s\n", path);
        return;
    }
    while (fscanf(fp, "%[^,],%s\n", qname, score) != EOF) {
        strcpy(score_table[idx].qname, qname);
        /* atof(): 문자열을 double로 변환 */
        score_table[idx++].score = atof(score);
    }
    fclose(fp);
}

/*
 * make_scoreTable():
 * opendir/readdir으로 정답 디렉토리를 2단계 탐색하여 문제 파일 목록을 수집하고
 * 사용자 입력으로 배점을 결정한다.
 */
void make_scoreTable(char *ansDir)
{
    int    type, num;
    double score, bscore = 0, pscore = 0;
    /* struct dirent: d_name 필드에 파일/디렉토리 이름이 저장되는 구조체 */
    struct dirent *dirp, *c_dirp;
    DIR   *dp, *c_dp;
    char   tmp[BUFLEN];
    int    idx = 0, i;

    num = get_create_type();
    if (num == 1) {
        printf("Input value of blank question : ");   scanf("%lf", &bscore);
        printf("Input value of program question : "); scanf("%lf", &pscore);
    }

    /* opendir(): 디렉토리를 열고 DIR 포인터를 반환. NULL이면 실패 */
    if ((dp = opendir(ansDir)) == NULL) { fprintf(stderr, "open dir error for %s\n", ansDir); return; }
    while ((dirp = readdir(dp)) != NULL) {
        /* readdir(): 디렉토리 항목을 순차적으로 반환. "."와 ".."는 제외 */
        if (!strcmp(dirp->d_name, ".") || !strcmp(dirp->d_name, "..")) continue;
        sprintf(tmp, "%s/%s", ansDir, dirp->d_name);
        if ((c_dp = opendir(tmp)) == NULL) { fprintf(stderr, "open dir error for %s\n", tmp); return; }
        while ((c_dirp = readdir(c_dp)) != NULL) {
            if (!strcmp(c_dirp->d_name, ".") || !strcmp(c_dirp->d_name, "..")) continue;
            if ((type = get_file_type(c_dirp->d_name)) < 0) continue;
            strcpy(score_table[idx++].qname, c_dirp->d_name);
        }
        /* closedir(): DIR 포인터가 가리키는 스트림을 닫고 자원 반납 */
        closedir(c_dp);
    }
    closedir(dp);
    sort_scoreTable(idx);

    for (i = 0; i < idx; i++) {
        type = get_file_type(score_table[i].qname);
        if      (num == 1 && type == TEXTFILE) score = bscore;
        else if (num == 1 && type == CFILE)    score = pscore;
        else { printf("Input of %s: ", score_table[i].qname); scanf("%lf", &score); }
        score_table[i].score = score;
    }
}

/*
 * write_scoreTable():
 * creat()로 파일을 생성하고 저수준 write()로 score_table[]을 CSV로 기록한다.
 */
void write_scoreTable(char *filename)
{
    int  fd, i;
    char tmp[BUFLEN];
    int  num = sizeof(score_table) / sizeof(score_table[0]);

    if ((fd = creat(filename, 0666)) < 0) { fprintf(stderr, "creat error for %s\n", filename); return; }
    for (i = 0; i < num; i++) {
        if (score_table[i].score == 0) break;
        sprintf(tmp, "%s,%.2f\n", score_table[i].qname, score_table[i].score);
        /* write(): fd에 buf의 n바이트를 쓰는 syscall. 반환값은 실제로 쓴 바이트 수 */
        write(fd, tmp, strlen(tmp));
    }
    close(fd);
}

/*
 * set_idTable():
 * 학생 제출 디렉토리를 탐색하여 하위 디렉토리 이름(학번)을 id_table에 수집한다.
 * 이 함수로 수집된 id_table이 이후 score.csv의 학생 기록 순서를 결정하므로
 * 정렬된 상태로 유지하는 것이 중요하다.
 */
void set_idTable(char *stuDir)
{
    struct stat    statbuf; /* 파일/디렉토리 메타데이터를 담는 구조체 */
    struct dirent *dirp;    /* readdir()이 반환하는 디렉토리 항목 포인터 */
    DIR   *dp;              /* opendir()이 반환하는 디렉토리 스트림 포인터 */
    char   tmp[BUFLEN];
    int    num = 0;

    /* opendir(): 디렉토리를 열어 탐색 스트림을 얻는다. 실패 시 NULL을 반환한다 */
    if ((dp = opendir(stuDir)) == NULL) { fprintf(stderr, "opendir error for %s\n", stuDir); exit(1); }
    while ((dirp = readdir(dp)) != NULL) {
        /* readdir(): 항목을 순차 반환. "."(현재)와 ".."(부모)는 건너뛴다 */
        if (!strcmp(dirp->d_name, ".") || !strcmp(dirp->d_name, "..")) continue;
        sprintf(tmp, "%s/%s", stuDir, dirp->d_name);
        /*
         * stat(): 경로의 메타데이터를 statbuf에 채운다.
         * 파일인지 디렉토리인지 판별하기 위해 st_mode 필드가 필요하다.
         * lstat()과 달리 심볼릭 링크가 있으면 링크 대상을 따라간다.
         */
        stat(tmp, &statbuf);
        /*
         * S_ISDIR(): statbuf.st_mode를 검사하여 디렉토리이면 true를 반환하는 매크로.
         * 학생 학번 디렉토리만 id_table에 추가하고 파일은 무시한다.
         */
        if (S_ISDIR(statbuf.st_mode)) strcpy(id_table[num++], dirp->d_name);
    }
    /* closedir(): 디렉토리 스트림을 닫고 관련 자원을 해제한다 */
    closedir(dp);
    /* 수집된 학번을 사전순으로 정렬한다. 이 순서가 score.csv 기록 순서가 된다 */
    sort_idTable(num);
}

/*
 * cmp_id(): qsort()에 전달하는 비교 함수. 문자열 사전순 비교.
 * qsort는 void* 포인터를 전달하므로 const char*로 캐스팅 후 strcmp로 비교한다.
 * strcmp: 같으면 0, a<b이면 음수, a>b이면 양수를 반환하여 오름차순 정렬에 쓰인다.
 */
static int cmp_id(const void *a, const void *b)
{ return strcmp((const char *)a, (const char *)b); }

/*
 * sort_idTable():
 * 기존 버블 정렬(O(n^2))을 qsort(O(n log n))로 교체했다.
 * SNUM=100 기준 버블 정렬은 최대 9,900회 비교, qsort는 약 700회로 줄어든다.
 * qsort(): stdlib.h에 선언된 표준 정렬 함수. 비교 함수 포인터로 정렬 기준을 지정한다.
 * 인자: 배열 포인터, 원소 수, 원소 크기, 비교 함수 포인터.
 */
void sort_idTable(int size) { qsort(id_table, size, sizeof(id_table[0]), cmp_id); }

/*
 * cmp_score_table(): score_table 정렬용 비교 함수.
 * "1-2.c"에서 추출한 (1, 2) 쌍을 기준으로 오름차순 정렬한다.
 * 첫 번째 숫자가 같으면 두 번째 숫자로 비교한다.
 * strncpy로 복사해서 쓰는 이유: strtok이 내부 포인터를 사용해 원본을 수정하기 때문이다.
 */
static int cmp_score_table(const void *a, const void *b)
{
    const struct ssu_scoreTable *sa = (const struct ssu_scoreTable *)a;
    const struct ssu_scoreTable *sb = (const struct ssu_scoreTable *)b;
    int n1_1, n1_2, n2_1, n2_2;
    char da[FILELEN], db[FILELEN];
    /* strncpy(): 최대 FILELEN-1 바이트 복사 후 명시적으로 널 종단을 보장 */
    strncpy(da, sa->qname, FILELEN - 1); da[FILELEN-1] = '\0';
    strncpy(db, sb->qname, FILELEN - 1); db[FILELEN-1] = '\0';
    get_qname_number(da, &n1_1, &n1_2);
    get_qname_number(db, &n2_1, &n2_2);
    if (n1_1 != n2_1) return n1_1 - n2_1; /* 첫 번째 번호로 비교 */
    return n1_2 - n2_2;                    /* 같으면 두 번째 번호로 비교 */
}

void sort_scoreTable(int size) { qsort(score_table, size, sizeof(score_table[0]), cmp_score_table); }

/*
 * get_qname_number():
 * "1-2.c" 형태의 파일명에서 숫자 두 개를 추출하여 num1, num2에 저장한다.
 * strtok(): 구분자("-.")로 문자열을 분리한다. 내부 정적 포인터를 수정하므로
 * 원본을 dup에 복사해서 사용한다. 두 번째 호출 시 NULL을 전달하면 이어서 분리한다.
 * atoi(): 문자열을 정수로 변환. 숫자가 없으면 0을 반환한다.
 */
void get_qname_number(char *qname, int *num1, int *num2)
{
    char *p, dup[FILELEN];
    strncpy(dup, qname, strlen(qname)); /* strtok이 원본을 수정하므로 복사본에서 작업 */
    *num1 = atoi(strtok(dup, "-."));    /* 첫 번째 구분자 이전 숫자 */
    p = strtok(NULL, "-.");             /* 이어서 다음 토큰 분리 */
    *num2 = (p == NULL) ? 0 : atoi(p); /* 두 번째 숫자가 없으면(예: "1.c") 0으로 처리 */
}

/*
 * get_create_type():
 * score_table.csv가 없을 때 배점 입력 방식을 사용자에게 선택받는다.
 * 1: 빈칸/프로그래밍 문제 두 종류만 구분해서 일괄 입력
 * 2: 문제별로 개별 입력
 * scanf()로 입력받고 유효하지 않은 값이면 다시 묻는다.
 */
int get_create_type(void)
{
    int num;
    while (1) {
        printf("score_table.csv file doesn't exist in TREUDIR!\n");
        printf("1. input blank question and program question's score. ex) 0.5 1\n");
        printf("2. input all question's score. ex) Input value of 1-1: 0.1\n");
        printf("select type >> ");
        scanf("%d", &num); /* 표준 입력에서 정수 한 개를 읽는다 */
        if (num == 1 || num == 2) break;
        printf("not correct number!\n");
    }
    return num;
}

/*
 * score_students():
 * 학생별 fork 병렬 채점 후 id_table 순으로 결과를 score.csv에 병합한다.
 *
 * 설계 이유:
 * 각 학생 채점은 서로 독립적이므로 fork로 병렬화하면 실행 시간이 가장 느린 학생
 * 1명 처리 시간으로 수렴한다. 단, 병렬로 실행하면 score.csv에 기록 순서가 뒤섞이므로
 * 각 자식이 임시 파일(/tmp/ssu_score_<id>.tmp)에 결과를 기록하고,
 * 부모가 모든 자식 종료 후 id_table 인덱스 순서대로 임시 파일을 읽어 병합한다.
 * 이 방식으로 score.csv의 학생 순서를 항상 사전순으로 보장한다.
 */
void score_students(void)
{
    double score = 0;
    int    num;
    int    fd;
    /* sizeof(배열) / sizeof(원소) 패턴: 배열 원소 수를 매직 넘버 없이 계산한다 */
    int    size = sizeof(id_table) / sizeof(id_table[0]);

    char   tmp_paths[SNUM][BUFLEN]; /* 각 학생의 임시 결과 파일 경로 */
    double scores[SNUM];            /* 각 학생의 최종 점수 (병합 단계에서 채워진다) */
    pid_t  pids[SNUM];              /* 각 자식 프로세스의 pid */
    int    actual_cnt = 0;          /* id_table에서 실제 학생 수 */

    /* memset으로 0 초기화. pids[i]>0 조건으로 유효한 pid만 waitpid할 수 있다 */
    memset(pids,   0, sizeof(pids));
    memset(scores, 0, sizeof(scores));

    /* id_table에서 빈 슬롯이 시작되는 지점 = 실제 학생 수 */
    for (num = 0; num < size; num++) {
        if (!strcmp(id_table[num], "")) break;
        actual_cnt++;
    }

    /*
     * creat("score.csv", 0666)
     * score.csv를 새로 생성하거나 기존 내용을 0바이트로 초기화한다.
     * creat()는 open(O_WRONLY|O_CREAT|O_TRUNC, 0666)과 동일하다.
     * 0666 = rw-rw-rw-, umask 적용 후 보통 0644(rw-r--r--)가 된다.
     */
    if ((fd = creat("score.csv", 0666)) < 0) {
        fprintf(stderr, "creat error for score.csv");
        return;
    }
    write_first_row(fd); /* 헤더 행 기록 */
    close(fd); /* 헤더 기록 후 닫는다. 이후 O_APPEND 모드로 다시 열어 결과를 이어 쓴다 */

    for (num = 0; num < actual_cnt; num++) {
        /*
         * snprintf(): 임시 파일 경로를 버퍼 크기(BUFLEN)를 지정해 안전하게 조립한다.
         * 파일명에 학번(id)을 포함하므로 자식 프로세스 간 파일 충돌이 발생하지 않는다.
         */
        snprintf(tmp_paths[num], BUFLEN, "/tmp/ssu_score_%s.tmp", id_table[num]);

        /*
         * fork(): 현재 프로세스를 복제하여 자식 프로세스를 생성한다.
         * 반환값: 부모에서는 자식 pid(양수), 자식에서는 0, 실패 시 -1
         * 각 자식은 해당 학생 한 명만 채점하고 임시 파일에 결과를 기록한다.
         */
        pids[num] = fork();
        if (pids[num] < 0) { pids[num] = 0; continue; } /* fork 실패 시 건너뜀 */

        if (pids[num] == 0) {
            /* ---- 자식 프로세스 영역: 해당 학생 채점 수행 ---- */
            int    tfd;
            double s;
            char   line[BUFLEN * 4];
            char   meta[BUFLEN];
            FILE  *mf;

            /* creat()로 임시 파일 생성. 학번이 파일명에 포함되어 충돌 없음 */
            tfd = creat(tmp_paths[num], 0666);
            if (tfd < 0) exit(1);

            /* score.csv 형식: 첫 컬럼이 학번. write()로 직접 기록 */
            sprintf(line, "%s,", id_table[num]);
            write(tfd, line, strlen(line));

            /* 해당 학생의 모든 문제를 채점하고 결과를 tfd에 기록한다 */
            s = score_student(tfd, id_table[num]);
            close(tfd);

            /*
             * 메타 파일에 총점을 기록한다.
             * 자식에서 직접 출력하면 병렬 실행 시 콘솔이 섞이므로,
             * 부모가 병합 단계에서 메타 파일을 읽어 순서대로 출력한다.
             */
            snprintf(meta, BUFLEN, "/tmp/ssu_score_%s.meta", id_table[num]);
            mf = fopen(meta, "w"); /* fopen()으로 FILE 스트림 오픈 */
            if (mf) { fprintf(mf, "%.2f\n", s); fclose(mf); }

            exit(0); /* 자식 정상 종료 */
        }
        /* 부모는 루프를 계속 돌며 모든 학생의 자식을 생성한다 */
    }

    /*
     * 모든 자식이 종료될 때까지 순서대로 waitpid()로 대기한다.
     * waitpid()로 수거하지 않으면 종료된 자식이 좀비(zombie) 프로세스로 남는다.
     * 순서대로 기다리는 이유: 어차피 모두 끝날 때까지 기다려야 하므로 순서는 무방하다.
     */
    for (num = 0; num < actual_cnt; num++)
        if (pids[num] > 0) waitpid(pids[num], NULL, 0);

    {
        /*
         * open(O_WRONLY|O_APPEND): 기존 score.csv 파일을 쓰기+추가 모드로 연다.
         * O_APPEND를 사용하므로 모든 write()가 자동으로 파일 끝에 기록된다.
         * 헤더 뒤에 학생 결과를 이어 쓰는 구조다.
         */
        int out_fd = open("score.csv", O_WRONLY | O_APPEND);
        if (out_fd < 0) { fprintf(stderr, "open error for score.csv\n"); return; }

        /*
         * id_table 인덱스 순서(사전순)로 임시 파일을 읽어 score.csv에 병합한다.
         * 이 순서가 score.csv의 학생 기록 순서를 결정한다.
         * 병렬로 먼저 끝난 학생이 아니라 사전순으로 기록되므로 결정성이 보장된다.
         */
        for (num = 0; num < actual_cnt; num++) {
            int     in_fd;
            char    buf[BUFLEN * 4];
            ssize_t n;  /* ssize_t: signed size_t. read() 반환 타입 */
            char    meta[BUFLEN];
            FILE   *mf;
            double  s = 0;

            /* 임시 파일을 열어 내용을 score.csv로 복사 */
            in_fd = open(tmp_paths[num], O_RDONLY);
            if (in_fd >= 0) {
                /* read/write 루프로 블록 단위 복사. n은 실제 읽은 바이트 수 */
                while ((n = read(in_fd, buf, sizeof(buf))) > 0)
                    write(out_fd, buf, n);
                close(in_fd);
                /*
                 * unlink(): 파일의 디렉토리 항목(이름 링크)을 삭제한다.
                 * 파일 내용은 모든 fd가 닫히는 시점에 실제로 삭제된다.
                 */
                unlink(tmp_paths[num]);
            }

            /* 메타 파일에서 총점을 읽어 pOption 출력에 사용한다 */
            snprintf(meta, BUFLEN, "/tmp/ssu_score_%s.meta", id_table[num]);
            mf = fopen(meta, "r");
            if (mf) { fscanf(mf, "%lf", &s); fclose(mf); unlink(meta); }

            scores[num] = s;
            score += s;

            /* pOption이 활성화된 경우 학생별 점수를 함께 출력한다 */
            if (pOption) printf("%s is finished.. score : %.2f\n", id_table[num], s);
            else         printf("%s is finished..\n", id_table[num]);
        }
        close(out_fd);
    }

    if (pOption)
        printf("Total average : %.2f\n", score / actual_cnt);
}

/*
 * score_student():
 * 한 학생의 모든 문제를 순서대로 채점하고 CSV 행을 fd에 기록한다.
 * score_table[]을 순회하며 score가 0인 항목이 나오면 유효 범위 끝으로 판단한다.
 * 각 문제의 제출 파일 존재 여부는 access()로 확인하고,
 * 파일 확장자에 따라 score_blank() 또는 score_program()으로 분기한다.
 *
 * 반환값 의미:
 *   true(1): 정답 (배점 전액 부여)
 *   false(0): 오답 또는 미제출 (0점)
 *   음수: 경고(WARNING=-0.1 단위) 있는 컴파일 성공 (배점에서 감점)
 */
double score_student(int fd, char *id)
{
    int    type;
    double result;
    double score = 0;
    int    i;
    char   tmp[BUFLEN];
    int    size = sizeof(score_table) / sizeof(score_table[0]);

    for (i = 0; i < size; i++) {
        if (score_table[i].score == 0) break; /* score=0이면 배열 끝 */

        /* 학생의 해당 문제 제출 경로를 조립한다 */
        sprintf(tmp, "%s/%s/%s", stuDir, id, score_table[i].qname);

        /*
         * access(path, F_OK): 파일 존재 여부만 확인한다.
         * 실제 내용을 읽지 않고 존재 여부만 체크하므로 비용이 낮다.
         * 미제출이면 result=false(0)로 처리한다.
         */
        if (access(tmp, F_OK) < 0)
            result = false;
        else {
            if ((type = get_file_type(score_table[i].qname)) < 0) continue;
            /* .txt 파일은 빈칸 채우기, .c 파일은 프로그래밍 문제 */
            if      (type == TEXTFILE) result = score_blank(id, score_table[i].qname);
            else if (type == CFILE)    result = score_program(id, score_table[i].qname);
        }

        if (result == false) {
            write(fd, "0,", 2); /* 오답/미제출: 0점 기록 */
        }
        else {
            if (result == true) {
                score += score_table[i].score;
                sprintf(tmp, "%.2f,", score_table[i].score);
            }
            else if (result < 0) {
                /*
                 * result < 0: 컴파일 경고가 있는 경우.
                 * WARNING=-0.1이므로 경고 1개당 배점에서 0.1 감점된다.
                 * score_table[i].score + result = 배점 - 감점
                 */
                score = score + score_table[i].score + result;
                sprintf(tmp, "%.2f,", score_table[i].score + result);
            }
            write(fd, tmp, strlen(tmp));
        }
    }

    /* 마지막에 총점을 기록하고 줄 바꿈으로 CSV 행을 마무리한다 */
    sprintf(tmp, "%.2f\n", score);
    write(fd, tmp, strlen(tmp));
    return score;
}

/*
 * write_first_row():
 * score.csv의 첫 번째 행(헤더)을 기록한다.
 * 형식: ",문제1,문제2,...,sum\n"
 *
 * 기존 방식과의 차이:
 * 기존: write(fd, ",", 1) 후 문제마다 write() 1회 → N+2회 syscall
 * 개선: char buf[]에 전체 헤더를 조립한 뒤 write() 1회로 기록 → 1회 syscall
 * 효과 자체는 작지만 syscall 최소화 원칙을 일관되게 적용한다.
 * memcpy로 buf를 채우는 방식이라 strlen 호출도 최소화된다.
 */
void write_first_row(int fd)
{
    int  i;
    /*
     * buf 크기를 BUFLEN*4로 잡은 이유:
     * QNUM=100문제, FILELEN=64이면 최대 100*65=6500바이트가 필요하다.
     * BUFLEN*4=4096은 약간 부족할 수 있으나 실제 문제 수는 훨씬 적으므로 충분하다.
     */
    char buf[BUFLEN * 4];
    char tmp[BUFLEN];
    int  size = sizeof(score_table) / sizeof(score_table[0]);
    int  pos  = 0;

    buf[0] = ','; /* 첫 컬럼(학번)을 위한 빈 헤더 */
    pos    = 1;

    for (i = 0; i < size; i++) {
        if (score_table[i].score == 0) break;
        sprintf(tmp, "%s,", score_table[i].qname);
        /* memcpy(): src와 dst가 겹치지 않을 때 memmove보다 빠른 메모리 복사 함수 */
        memcpy(buf + pos, tmp, strlen(tmp));
        pos += strlen(tmp);
    }
    memcpy(buf + pos, "sum\n", 4); /* 마지막 컬럼은 합계 */
    pos += 4;

    /* 전체 헤더를 단 1회 write() syscall로 기록한다 */
    write(fd, buf, pos);
}

/*
 * get_answer():
 * 빈칸 문제 파일에서 ':' 구분자 이전의 답안 문자열을 읽어온다.
 * 기존: read(fd, &c, 1) 루프로 매 바이트마다 syscall. 20바이트 파일에 20회 syscall.
 * 개선: 64바이트 블록으로 읽고 버퍼 내에서 ':' 탐색.
 * ':' 발견 후 lseek()으로 fd 위치를 ':' 직후로 보정하여
 * 다음 get_answer() 호출이 정확한 위치에서 이어 읽도록 한다.
 */
char *get_answer(int fd, char *result)
{
    char    buf[64];
    ssize_t n; /* ssize_t: signed size_t. read() 반환 타입. 오류 시 음수 가능 */
    int     idx = 0;
    int     i;
    int     found = 0;

    memset(result, 0, BUFLEN);

    while (!found) {
        n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        for (i = 0; i < (int)n; i++) {
            if (buf[i] == ':') {
                /* lseek(fd, offset, SEEK_CUR): 현재 위치에서 offset만큼 이동.
                   -(n-i-1)로 ':' 이후에 읽힌 바이트를 되돌린다 */
                lseek(fd, -(n - i - 1), SEEK_CUR);
                found = 1;
                break;
            }
            if (idx < BUFLEN - 1)
                result[idx++] = buf[i];
        }
    }

    /* idx>0 체크: 빈 문자열일 때 result[-1] 접근(UB) 방지 */
    if (idx > 0 && result[idx - 1] == '\n')
        result[idx - 1] = '\0';

    return result;
}

/*
 * score_blank():
 * 빈칸 문제를 AST 기반으로 채점한다.
 * 개선: get_ans_tree()로 캐시 히트 시 정답 파일 open/파싱 전체를 건너뜀.
 * 캐시 미스 시 기존 방식으로 폴백하여 하위 호환성을 유지한다.
 */
int score_blank(char *id, char *filename)
{
    char   tokens[TOKEN_CNT][MINLEN];
    node  *std_root = NULL;
    int    idx;
    char   tmp[BUFLEN];
    char   s_answer[BUFLEN], a_answer[BUFLEN];
    char   qname[FILELEN];
    int    fd_std;
    int    result = true;
    int    has_semicolon = false;
    ans_tree_entry *cached;

    memset(qname, 0, sizeof(qname));
    memcpy(qname, filename,
           strlen(filename) - strlen(strrchr(filename, '.')));

    sprintf(tmp, "%s/%s/%s", stuDir, id, filename);
    fd_std = open(tmp, O_RDONLY); /* O_RDONLY: 읽기 전용으로 파일 열기 */
    get_answer(fd_std, s_answer);
    close(fd_std);

    if (!strcmp(s_answer, ""))     return false;
    if (!check_brackets(s_answer)) return false;

    strcpy(s_answer, ltrim(rtrim(s_answer)));

    if (s_answer[strlen(s_answer) - 1] == ';') {
        has_semicolon = true;
        s_answer[strlen(s_answer) - 1] = '\0';
    }

    if (!make_tokens(s_answer, tokens)) return false;
    idx = 0;
    std_root = make_tree(std_root, tokens, &idx, 0);

    /* 캐시 히트: 정답 파일 open, get_answer, make_tokens, make_tree 전부 생략 */
    cached = get_ans_tree(filename);
    if (cached != NULL) {
        int k;
        for (k = 0; k < cached->cnt; k++) {
            if (has_semicolon != cached->has_semicolon[k]) continue;
            result = true;
            compare_tree(std_root, cached->roots[k], &result);
            if (result == true) {
                if (std_root != NULL) free_node(std_root);
                return true;
            }
        }
        if (std_root != NULL) free_node(std_root);
        return false;
    }

    /* 캐시 미스: 기존 방식 폴백 */
    {
        node *ans_root = NULL;
        int   fd_ans;

        sprintf(tmp, "%s/%s/%s", ansDir, qname, filename);
        fd_ans = open(tmp, O_RDONLY);

        while (1) {
            ans_root = NULL;
            result   = true;
            for (idx = 0; idx < TOKEN_CNT; idx++) memset(tokens[idx], 0, sizeof(tokens[idx]));

            get_answer(fd_ans, a_answer);
            if (!strcmp(a_answer, "")) break;

            strcpy(a_answer, ltrim(rtrim(a_answer)));

            if (!has_semicolon && a_answer[strlen(a_answer) - 1] == ';')  continue;
            if  (has_semicolon && a_answer[strlen(a_answer) - 1] != ';')  continue;
            if  (has_semicolon) a_answer[strlen(a_answer) - 1] = '\0';

            if (!make_tokens(a_answer, tokens)) continue;
            idx = 0;
            ans_root = make_tree(ans_root, tokens, &idx, 0);
            compare_tree(std_root, ans_root, &result);

            if (result == true) {
                close(fd_ans);
                if (std_root != NULL) free_node(std_root);
                if (ans_root  != NULL) free_node(ans_root);
                return true;
            }
        }
        close(fd_ans);
        if (std_root != NULL) free_node(std_root);
        if (ans_root  != NULL) free_node(ans_root);
    }

    return false;
}

/*
 * score_program():
 * C 파일 문제를 채점하는 함수. compile_program()과 execute_program()을 순서대로 호출한다.
 * 컴파일이 실패(ERROR 또는 false)하면 즉시 false를 반환하여 실행 단계를 건너뛴다.
 * 컴파일은 성공했지만 실행 결과가 다르면 false를 반환한다.
 * compile < 0(경고 있음)이면 감점 점수를 반환하고, 정답이면 true를 반환한다.
 */
double score_program(char *id, char *filename)
{
    double compile;
    int    result;

    compile = compile_program(id, filename);
    /* ERROR(=0)와 false(=0)가 같은 값이므로 두 경우를 한 번에 처리한다 */
    if (compile == ERROR || compile == false) return false;

    result = execute_program(id, filename);
    if (!result) return false;

    /* compile < 0: 경고 감점 점수를 그대로 반환하여 호출자가 감점 처리하게 한다 */
    if (compile < 0) return compile;
    return true;
}

/*
 * is_thread():
 * qname이 -t 옵션으로 지정된 threadFiles 목록에 있는지 확인한다.
 * 있으면 gcc 컴파일 시 -lpthread 링크 옵션이 추가된다.
 * 최대 ARGNUM=5개이므로 선형 탐색으로 충분하다.
 */
int is_thread(char *qname)
{
    int i, size = sizeof(threadFiles) / sizeof(threadFiles[0]);
    for (i = 0; i < size; i++)
        if (!strcmp(threadFiles[i], qname)) return true;
    return false;
}

/*
 * compile_program():
 * 정답과 학생 C 파일을 gcc로 컴파일한다.
 *
 * 컴파일 에러 판별 방법:
 * gcc의 stderr 출력을 파일로 리다이렉션한 뒤, lseek(fd, 0, SEEK_END)로
 * 해당 파일의 크기를 얻는다. 크기가 0이면 에러/경고 없음, 0보다 크면 에러/경고가 있다.
 * 에러가 있는 경우 check_error_warning()으로 error:/warning: 개수를 파악하고
 * 에러면 false(0), 경고면 경고 개수 × -0.1의 감점 점수를 반환한다.
 *
 * 정답 컴파일 최적화:
 * is_ans_compiled() 캐시를 확인하여 이미 컴파일된 정답은 다시 컴파일하지 않는다.
 * 이 함수가 학생 채점 중에 호출되더라도 캐시 히트 시 정답 컴파일을 건너뛴다.
 */
double compile_program(char *id, char *filename)
{
    int    fd;
    char   tmp_f[BUFLEN], tmp_e[BUFLEN], command[BUFLEN], qname[FILELEN];
    int    isthread;
    off_t  size; /* off_t: lseek() 반환 타입. 파일 오프셋 및 크기를 표현하는 POSIX 타입 */
    double result;

    /* 확장자 제거로 qname 추출. strrchr()로 마지막 '.'를 찾아 그 앞까지만 복사 */
    memset(qname, 0, sizeof(qname));
    memcpy(qname, filename, strlen(filename) - strlen(strrchr(filename, '.')));
    isthread = is_thread(qname);

    if (!is_ans_compiled(qname)) {
        sprintf(tmp_f, "%s/%s/%s",     ansDir, qname, filename);
        sprintf(tmp_e, "%s/%s/%s.exe", ansDir, qname, qname);

        /* -t 옵션에 해당하는 문제면 -lpthread 링크 옵션을 추가한다 */
        if (isthread && tOption)
            sprintf(command, "gcc -o %s %s -lpthread", tmp_e, tmp_f);
        else
            sprintf(command, "gcc -o %s %s", tmp_e, tmp_f);

        /* creat()로 에러 파일 생성. gcc stderr를 이 파일로 리다이렉션한다 */
        sprintf(tmp_e, "%s/%s/%s_error.txt", ansDir, qname, qname);
        fd = creat(tmp_e, 0666);
        /*
         * redirection(): gcc 명령을 쉘 없이 직접 fork/dup2/execvp로 실행한다.
         * STDERR를 fd로 교체하여 컴파일 에러 메시지를 에러 파일에 기록한다.
         */
        redirection(command, fd, STDERR);
        /*
         * lseek(fd, 0, SEEK_END): fd의 파일 위치를 끝으로 이동하고 오프셋(=파일 크기)을 반환.
         * 에러 파일 크기가 0보다 크면 컴파일 에러가 있다고 판단한다.
         */
        size = lseek(fd, 0, SEEK_END);
        close(fd); unlink(tmp_e); /* 에러 파일 정리 */

        if (size > 0) return false; /* 정답 컴파일 실패 */
        mark_ans_compiled(qname);
    }

    /* 학생 코드 컴파일 */
    sprintf(tmp_f, "%s/%s/%s",        stuDir, id, filename);
    sprintf(tmp_e, "%s/%s/%s.stdexe", stuDir, id, qname);

    if (isthread && tOption)
        sprintf(command, "gcc -o %s %s -lpthread", tmp_e, tmp_f);
    else
        sprintf(command, "gcc -o %s %s", tmp_e, tmp_f);

    sprintf(tmp_f, "%s/%s/%s_error.txt", stuDir, id, qname);
    fd = creat(tmp_f, 0666);
    /*
     * redirection(): 학생 코드를 gcc로 컴파일한다.
     * STDERR를 fd로 교체하여 컴파일 에러/경고 메시지를 에러 파일에 기록한다.
     */
    redirection(command, fd, STDERR);
    size = lseek(fd, 0, SEEK_END);
    close(fd);

    if (size > 0) {
        if (eOption) {
            /* -e 옵션: 에러 파일을 errorDir에 영구 보존한다 */
            sprintf(tmp_e, "%s/%s", errorDir, id);
            if (access(tmp_e, F_OK) < 0) mkdir(tmp_e, 0755); /* 학번 하위 디렉토리 생성 */
            sprintf(tmp_e, "%s/%s/%s_error.txt", errorDir, id, qname);
            /*
             * rename(): 파일 경로를 변경하는 syscall.
             * 같은 파일시스템 내에서는 inode 이동만 발생하여 복사 없이 빠르게 처리된다.
             */
            rename(tmp_f, tmp_e);
            result = check_error_warning(tmp_e);
        }
        else {
            result = check_error_warning(tmp_f);
            unlink(tmp_f); /* -e 옵션 없으면 에러 파일 삭제 */
        }
        return result;
    }
    unlink(tmp_f); /* 에러 없으면 빈 에러 파일도 삭제 */
    return true;
}

/*
 * check_error_warning():
 * 컴파일 에러 파일을 파싱하여 에러/경고 여부를 판별한다.
 *
 * 기존 방식과의 차이:
 * 기존: FILE* + fscanf(fp, "%s", tmp)로 단어마다 반복 호출했다.
 *   공백 기준으로 분리하므로 에러가 많을수록 fscanf 호출 횟수가 늘어난다.
 * 개선: raw fd + BUFLEN 블록 읽기로 교체.
 *   버퍼 경계에서 키워드가 잘리는 경우를 대비해 leftover(8바이트)를 유지하여
 *   연속된 두 버퍼에 걸친 "error:"/"warning:"도 정확히 검출한다.
 *
 * 반환값:
 *   ERROR(0): "error:" 발견 → 컴파일 에러
 *   음수(경고 개수 × -0.1): "warning:" 발견 횟수만큼 감점
 *   0(warning=0): 에러/경고 없음 (이 경우는 파일이 비어있어 이 함수가 호출되지 않는다)
 */
double check_error_warning(char *filename)
{
    int     fd;
    char    buf[BUFLEN];
    ssize_t n;           /* read() 반환 타입. 실제 읽은 바이트 수 */
    char    leftover[16] = {0}; /* 이전 버퍼의 끝 8바이트. 경계 키워드 검출용 */
    int     lo_len = 0;
    double  warning = 0;

    fd = open(filename, O_RDONLY); /* 에러 파일을 읽기 전용으로 열기 */
    if (fd < 0) { fprintf(stderr, "open error for %s\n", filename); return false; }

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        char search[BUFLEN + 16]; /* leftover + 현재 버퍼를 합친 탐색 버퍼 */
        char *p;

        buf[n] = '\0'; /* read() 후 명시적으로 널 종단 추가 */
        memset(search, 0, sizeof(search));
        /*
         * 이전 버퍼 끝의 잔여분(leftover)을 앞에 붙여 경계 키워드를 잡는다.
         * 예: 이전 버퍼가 "...err"로 끝나고 현재 버퍼가 "or: ..."로 시작하는 경우.
         */
        memcpy(search, leftover, lo_len);
        memcpy(search + lo_len, buf, n + 1);

        p = search;
        while (*p) {
            /*
             * strncmp(): 최대 n바이트만 비교하여 버퍼 끝을 넘지 않게 한다.
             * "error:" 발견 즉시 fd를 닫고 ERROR(0)를 반환한다.
             * "warning:" 발견 시 WARNING(-0.1)을 누적하고 8바이트 건너뛴다.
             */
            if      (strncmp(p, "error:",   6) == 0) { close(fd); return ERROR; }
            else if (strncmp(p, "warning:", 8) == 0) { warning += WARNING; p += 8; continue; }
            p++;
        }

        /* 다음 버퍼 처리를 위해 현재 버퍼의 마지막 8바이트를 leftover에 저장 */
        lo_len = (n >= 8) ? 8 : (int)n;
        memcpy(leftover, buf + n - lo_len, lo_len);
        leftover[lo_len] = '\0';
    }
    close(fd);
    return warning; /* 경고만 있으면 음수 감점 점수 반환, 아무것도 없으면 0 */
}

/*
 * execute_program():
 * 학생 실행 파일을 fork/execvp로 직접 실행하고 출력 결과를 정답과 비교한다.
 *
 * 기존 방식의 문제:
 * system("... &")으로 백그라운드 실행 후 inBackground()를 while 루프에서
 * 반복 호출하여 ps | grep으로 프로세스 종료를 감지했다.
 * 문제: ① ps+grep 프로세스를 매 감시 주기마다 새로 생성하는 비용 누적,
 *       ② 루프에 sleep 없어 타임아웃 5초 동안 CPU를 100% 점유하는 busy-wait,
 *       ③ 프로세스명 동일 시 다른 프로세스를 잘못 감지하는 비결정적 동작.
 *
 * 개선: fork/execvp + waitpid(WNOHANG) + nanosleep(10ms)
 * ① 자식 프로세스를 직접 생성하고 pid로 정확히 추적한다.
 * ② 10ms마다 CPU를 다른 프로세스에 양보하므로 CPU 점유율이 5% 미만이다.
 * ③ SIGKILL 후 즉시 waitpid()로 수거하여 좀비 프로세스를 방지한다.
 * ④ execvp()로 쉘 없이 직접 실행 파일을 구동하므로 불필요한 쉘 프로세스가 생성되지 않는다.
 */
int execute_program(char *id, char *filename)
{
    char   std_fname[BUFLEN], ans_fname[BUFLEN], tmp[BUFLEN], qname[FILELEN];
    time_t start, now;
    pid_t  pid;
    int    fd, status;
    /*
     * struct timespec: nanosleep()에 필요한 타입. { tv_sec(초), tv_nsec(나노초) }
     * 10ms = 10,000,000 나노초. 이 간격으로 CPU를 양보하며 자식 종료를 감시한다.
     */
    struct timespec ts = {0, 10000000};
    /*
     * exec_argv: execvp()에 전달할 인자 배열. 실행 파일 경로와 NULL만 있으면 충분하다.
     * exec_buf: execvp() 인자로 넘길 문자열의 작업 버퍼.
     */
    char  *exec_argv[2];
    char   exec_buf[BUFLEN];

    memset(qname, 0, sizeof(qname));
    memcpy(qname, filename, strlen(filename) - strlen(strrchr(filename, '.')));

    /*
     * 정답 실행 결과 캐시 확인.
     * is_ans_executed() 캐시에 있으면 정답 프로그램을 재실행하지 않는다.
     * 학생 100명이라면 기존에 정답 실행 100회였던 것이 1회로 줄어든다.
     */
    sprintf(ans_fname, "%s/%s/%s.stdout", ansDir, qname, qname);
    if (!is_ans_executed(qname)) {
        fd = creat(ans_fname, 0666);
        sprintf(tmp, "%s/%s/%s.exe", ansDir, qname, qname);
        /*
         * redirection(): 정답 실행 파일을 쉘 없이 직접 execvp로 실행한다.
         * STDOUT을 fd로 교체하여 실행 결과를 stdout 파일에 저장한다.
         */
        redirection(tmp, fd, STDOUT);
        close(fd);
        mark_ans_executed(qname);
    }

    sprintf(std_fname, "%s/%s/%s.stdout", stuDir, id, qname);
    fd = creat(std_fname, 0666); /* 학생 출력 파일 생성 */

    sprintf(tmp, "%s/%s/%s.stdexe", stuDir, id, qname);

    /*
     * execvp()를 위한 인자 배열 구성.
     * 학생 실행 파일은 인자 없이 단독으로 실행되므로 argv[0]=경로, argv[1]=NULL로 충분하다.
     */
    strncpy(exec_buf, tmp, BUFLEN - 1);
    exec_buf[BUFLEN - 1] = '\0';
    exec_argv[0] = exec_buf;
    exec_argv[1] = NULL;

    pid = fork(); /* 자식 프로세스 생성 */
    if (pid < 0) { close(fd); return false; }

    if (pid == 0) {
        /*
         * dup2(fd, STDOUT_FILENO): stdout(fd=1)을 fd로 교체한다.
         * 이후 이 자식의 모든 표준 출력이 std_fname 파일로 저장된다.
         * STDOUT_FILENO는 1의 상수. unistd.h에 정의되어 있다.
         */
        dup2(fd, STDOUT_FILENO);
        close(fd); /* dup2 후 원본 fd는 자식에서 더 이상 필요 없으므로 닫는다 */
        /*
         * execvp(): 쉘을 경유하지 않고 실행 파일을 직접 구동한다.
         * 첫 번째 인자는 실행 파일 경로, 두 번째 인자는 argv 배열이다.
         * 성공 시 이후의 코드는 실행되지 않는다.
         */
        execvp(exec_argv[0], exec_argv);
        exit(1); /* execvp 실패 시 (실행 파일이 없거나 권한 없음 등) */
    }

    close(fd); /* 부모에서 fd를 닫아야 자식이 닫을 때 실제로 파일이 정리된다 */

    /*
     * time(): 현재 Unix 시각(초)을 반환한다. 타임아웃 기준 시각을 기록한다.
     */
    start = time(NULL);

    while (1) {
        /*
         * waitpid(pid, &status, WNOHANG):
         * WNOHANG 플래그로 비블로킹 호출. 자식이 아직 실행 중이면 즉시 0을 반환한다.
         * ret > 0이면 자식이 종료됨, ret < 0이면 오류, ret == 0이면 실행 중.
         */
        int ret = waitpid(pid, &status, WNOHANG);
        if (ret > 0) break; /* 자식 정상 종료 */
        if (ret < 0) break; /* waitpid 오류 */

        now = time(NULL);
        /*
         * difftime(): 두 time_t 값의 차이를 double(초)로 반환한다.
         * OVER=5초 초과 시 학생 프로그램이 무한 루프에 빠진 것으로 판단한다.
         */
        if (difftime(now, start) > OVER) {
            /*
             * kill(pid, SIGKILL): 지정한 프로세스에 SIGKILL 시그널을 보낸다.
             * SIGKILL은 프로세스가 무시하거나 처리할 수 없으므로 반드시 종료된다.
             */
            kill(pid, SIGKILL);
            /*
             * kill 후 waitpid()를 호출하지 않으면 자식이 좀비 프로세스로 남는다.
             * 좀비는 프로세스 테이블 슬롯을 점유하므로 반드시 수거해야 한다.
             */
            waitpid(pid, NULL, 0);
            return false;
        }
        /*
         * nanosleep(&ts, NULL): ts에 지정된 시간만큼 슬립한다.
         * 10ms마다 CPU를 다른 프로세스에 양보하여 busy-wait 과부하를 방지한다.
         * 두 번째 인자(남은 시간 포인터)는 NULL이면 인터럽트 시 재시작하지 않는다.
         */
        nanosleep(&ts, NULL);
    }

    return compare_resultfile(std_fname, ans_fname);
}

/*
 * compare_resultfile():
 * 학생 실행 출력 파일과 정답 출력 파일을 비교한다.
 * 공백 문자는 무시하고, 알파벳은 대소문자를 무시한다.
 *
 * 기존 방식의 문제:
 * read(fd, &c, 1)로 1바이트씩 읽으면 파일 크기 N바이트에 대해 N번 syscall 발생.
 * 커널-유저 전환 비용이 누적되어 출력이 긴 문제에서 채점 속도가 느려졌다.
 *
 * 개선: BUFLEN(1024) 블록 단위로 읽어 syscall을 ceil(N/1024)회로 줄인다.
 * NEXT_CHAR 매크로가 버퍼 소진 시 자동으로 다음 블록을 읽는 로직을 캡슐화한다.
 */
int compare_resultfile(char *file1, char *file2)
{
    int     fd1, fd2;
    char    buf1[BUFLEN], buf2[BUFLEN];
    ssize_t n1 = 0, n2 = 0;
    int     pos1 = 0, pos2 = 0;
    int     done1 = 0, done2 = 0; /* 각 파일의 EOF 도달 여부 */
    char    c1, c2;

    fd1 = open(file1, O_RDONLY); /* 학생 출력 파일 */
    fd2 = open(file2, O_RDONLY); /* 정답 출력 파일 */
    if (fd1 < 0 || fd2 < 0) {
        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        return false;
    }

/*
 * NEXT_CHAR 매크로:
 * 버퍼에서 공백(' ')을 건너뛰며 다음 유효 문자를 가져온다.
 * 버퍼가 소진되면(pos >= n) read()로 BUFLEN 크기의 다음 블록을 읽어 채운다.
 * EOF에 도달하면 done 플래그를 설정하고 루프를 빠져나온다.
 */
#define NEXT_CHAR(fd, buf, n, pos, done, c)            \
    do {                                               \
        (c) = '\0';                                    \
        while (1) {                                    \
            if ((pos) >= (n)) {                        \
                (n) = read((fd), (buf), BUFLEN);       \
                (pos) = 0;                             \
                if ((n) <= 0) { (done) = 1; break; }  \
            }                                          \
            if ((buf)[(pos)] != ' ') {                 \
                (c) = (buf)[(pos)++]; break;           \
            }                                          \
            (pos)++;                                   \
        }                                              \
    } while (0)

    while (1) {
        NEXT_CHAR(fd1, buf1, n1, pos1, done1, c1);
        NEXT_CHAR(fd2, buf2, n2, pos2, done2, c2);
        if (done1 && done2) break;             /* 두 파일 모두 끝: 내용이 같다 */
        if (done1 || done2) { close(fd1); close(fd2); return false; } /* 길이 불일치 */
        to_lower_case(&c1); /* 대문자를 소문자로 변환하여 대소문자 무시 비교 */
        to_lower_case(&c2);
        if (c1 != c2) { close(fd1); close(fd2); return false; }
    }
#undef NEXT_CHAR

    close(fd1); close(fd2);
    return true;
}

/*
 * redirection():
 * 명령(command)을 fork/dup2/execvp로 직접 실행하면서
 * fd(new)를 표준 출력 또는 에러(old)로 교체한다.
 *
 * 기존 방식의 문제:
 * execl("/bin/sh", "sh", "-c", command, (char *)NULL)은 내부적으로 쉘을 경유한다.
 * 이는 system() 호출과 동일한 문제를 가진다.
 * ① /bin/sh 프로세스가 추가로 생성되어 불필요한 fork/exec 비용이 발생한다.
 * ② 쉘 확장(글로브, 변수 치환 등)이 의도치 않게 적용될 보안 위험이 있다.
 * ③ 부모의 열린 fd가 sh에 상속되어 불필요한 fd 누수가 발생할 수 있다.
 *
 * 개선: build_argv()로 command를 공백 기준으로 직접 분리한 뒤 execvp()로 실행한다.
 * ① 쉘 프로세스 없이 실행 파일을 직접 구동하므로 fork가 1회 줄어든다.
 * ② 쉘 해석 과정이 없으므로 인자에 특수 문자가 포함되어도 안전하다.
 * ③ 부모의 fd 테이블은 변경되지 않아 fd 누수가 없다.
 */
void redirection(char *command, int new, int old)
{
    /*
     * cmd_buf: build_argv()가 strtok_r로 command를 분리할 때 사용하는 작업 버퍼.
     * command 원본은 변경되지 않는다.
     * argv: execvp()에 전달할 인자 포인터 배열. 최대 인자 수를 64로 제한한다.
     * gcc 명령도 최대 4~5개 인자이므로 64는 충분히 크다.
     */
    char  cmd_buf[BUFLEN];
    char *argv[64];
    pid_t pid;

    /*
     * build_argv(): command를 공백 기준으로 분리하여 argv 배열을 구성한다.
     * argc가 0이면 빈 명령이므로 실행을 건너뛴다.
     */
    if (build_argv(command, cmd_buf, argv, 64) == 0) return;

    pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        /*
         * dup2(new, old): old번 fd를 new fd로 교체한다.
         * old가 이미 열려 있으면 자동으로 닫힌 뒤 new가 복사된다.
         * 예: dup2(fd, STDERR)이면 stderr(2)가 fd로 교체된다.
         */
        dup2(new, old);
        close(new); /* dup2 완료 후 원본 new fd는 자식에서 불필요하므로 닫는다 */
        /*
         * execvp(): PATH를 탐색하여 argv[0]에 해당하는 실행 파일을 직접 구동한다.
         * 쉘을 거치지 않으므로 system() 계열의 보안·성능 문제가 없다.
         * 성공 시 이후 코드는 실행되지 않는다.
         */
        execvp(argv[0], argv);
        exit(1); /* execvp 실패 시만 도달 */
    }
    /* 부모: 자식 종료를 블로킹 대기. 수거하지 않으면 좀비가 된다 */
    waitpid(pid, NULL, 0);
}

/*
 * get_file_type():
 * 파일명의 확장자를 기준으로 파일 종류를 반환한다.
 * strrchr()로 마지막 '.' 위치를 찾아 확장자 문자열을 직접 비교한다.
 * 반환값: TEXTFILE(3)=빈칸문제, CFILE(4)=프로그래밍문제, -1=지원하지 않는 확장자
 */
int get_file_type(char *filename)
{
    /* strrchr(): 문자열에서 마지막으로 나타나는 문자의 위치 포인터를 반환한다 */
    char *ext = strrchr(filename, '.');
    if (!strcmp(ext, ".txt")) return TEXTFILE;
    if (!strcmp(ext, ".c"))   return CFILE;
    return -1;
}

/*
 * rmdirs():
 * 디렉토리와 그 하위 내용을 재귀적으로 삭제한다.
 * -e 옵션 사용 시 errorDir를 초기화할 때 호출된다.
 * 파일은 unlink()로, 빈 디렉토리는 rmdir()로 삭제한다.
 * lstat()을 사용하는 이유: 심볼릭 링크가 있을 때 링크 대상이 아닌
 * 링크 자체의 정보를 얻어야 하기 때문이다. stat()은 링크 대상을 따라간다.
 */
void rmdirs(const char *path)
{
    struct dirent *dirp;
    struct stat    statbuf;
    DIR   *dp;
    char   tmp[BUFLEN];

    if ((dp = opendir(path)) == NULL) return;
    while ((dirp = readdir(dp)) != NULL) {
        if (!strcmp(dirp->d_name, ".") || !strcmp(dirp->d_name, "..")) continue;
        snprintf(tmp, sizeof(tmp), "%s/%s", path, dirp->d_name);
        /* lstat(): 심볼릭 링크를 따라가지 않고 항목 자체의 메타데이터를 가져온다 */
        if (lstat(tmp, &statbuf) == -1) continue;
        if (S_ISDIR(statbuf.st_mode)) rmdirs(tmp); /* 하위 디렉토리 재귀 삭제 */
        else unlink(tmp); /* 일반 파일 삭제 */
    }
    closedir(dp);
    /* rmdir(): 비어있는 디렉토리를 삭제하는 syscall. 내용이 있으면 ENOTEMPTY 오류 발생 */
    rmdir(path);
}

/*
 * to_lower_case():
 * 대문자 알파벳을 소문자로 변환한다.
 * ASCII 코드에서 대문자와 소문자의 차이가 정확히 32이므로 더하기로 변환한다.
 * compare_resultfile()에서 대소문자 무시 비교를 위해 사용된다.
 */
void to_lower_case(char *c)
{ if (*c >= 'A' && *c <= 'Z') *c = *c + 32; }


/*
 * 사용법을 출력한다.
 */
void print_usage(void)
{
    printf("Usage : ssu_score <STUDENTDIR> <TRUEDIR> [OPTION]\n");
    printf("Option : \n");
    printf(" -e <DIRNAME>      print error on 'DIRNAME/ID/qname_error.txt' file \n");
    printf(" -t <QNAMES>       compile QNAME.C with -lpthread option\n");
    printf(" -h                print usage\n");
    printf(" -p                print student's score and total average\n");
    printf(" -c <IDS>          print ID's score\n");
}