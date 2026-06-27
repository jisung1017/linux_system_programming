/* 헤더 가드: 동일 헤더를 여러 번 포함해도 내용이 한 번만 처리되도록 보장한다 */
#ifndef MAIN_H_
#define MAIN_H_

/*
 * true/false 정의.
 * C89/90 환경에서는 bool 타입이 없으므로 직접 1/0으로 정의한다.
 * #ifndef로 감싸서 이미 다른 헤더에서 정의된 경우 충돌을 피한다.
 */
#ifndef true
    #define true 1
#endif
#ifndef false
    #define false 0
#endif

/*
 * STDOUT=1, STDERR=2: 표준 파일 디스크립터 번호.
 * redirection() 함수에서 dup2(new, old)의 old 인자로 전달한다.
 * 리눅스에서 프로세스 시작 시 0=stdin, 1=stdout, 2=stderr로 자동 할당된다.
 */
#ifndef STDOUT
    #define STDOUT 1
#endif
#ifndef STDERR
    #define STDERR 2
#endif

/*
 * TEXTFILE=3, CFILE=4: get_file_type()의 반환값으로 쓰인다.
 * 0, 1, -1과 구별되는 값을 써서 파일 타입 구분을 명확하게 한다.
 * TEXTFILE은 빈칸 문제(.txt), CFILE은 프로그래밍 문제(.c)에 해당한다.
 */
#ifndef TEXTFILE
    #define TEXTFILE 3
#endif
#ifndef CFILE
    #define CFILE 4
#endif

/*
 * OVER=5: execute_program()에서 학생 프로그램의 실행 제한 시간(초).
 * 타임아웃이 이 값을 초과하면 SIGKILL로 학생 프로세스를 강제 종료한다.
 */
#ifndef OVER
    #define OVER 5
#endif

/*
 * WARNING=-0.1: 컴파일 경고 1개당 감점 점수.
 * 음수로 정의하여 점수 계산 시 단순히 더하면 감점이 되도록 설계했다.
 * check_error_warning()에서 warning: 키워드 개수만큼 누적 합산한다.
 */
#ifndef WARNING
    #define WARNING -0.1
#endif

/*
 * ERROR=0: 컴파일 에러 발생 시 compile_program()의 반환값.
 * 0은 false와 같은 값이므로 score_program()에서 if(compile == false || compile == ERROR)
 * 형태로 에러와 미제출을 함께 처리할 수 있다.
 */
#ifndef ERROR
    #define ERROR 0
#endif

/*
 * 크기 관련 상수들.
 * FILELEN=64: 파일명 버퍼 크기. 대부분의 파일명은 이 범위 안에 들어온다.
 * BUFLEN=1024: 일반 문자열/명령어 버퍼 크기. 경로, gcc 명령어 등에 사용된다.
 * SNUM=100: 최대 학생 수. id_table 배열의 크기를 결정한다.
 * QNUM=100: 최대 문제 수. score_table 배열의 크기를 결정한다.
 * ARGNUM=5: -t, -c 옵션에서 받을 수 있는 최대 인자 수.
 */
#define FILELEN   64
#define BUFLEN    1024
#define SNUM      100
#define QNUM      100
#define ARGNUM    5

/*
 * struct ssu_scoreTable
 * 채점 기준표의 한 항목을 나타내는 구조체다.
 * qname: 문제 파일명 (예: "1-1.c", "2-3.txt")
 * score: 해당 문제의 배점
 * score_table[QNUM] 배열로 관리되며, set_scoreTable()에서 채워지고
 * score_student()에서 채점 기준으로 참조된다.
 */
struct ssu_scoreTable {
    char   qname[FILELEN];
    double score;
};

/*
 * struct student_score_result
 * 병렬 채점 결과를 수집하기 위한 구조체.
 * 학생별 fork 이후 자식이 임시 파일에 결과를 기록하는 방식이어서
 * 이 구조체는 실제로 메모리 상에서 공유되진 않지만, 임시 파일의 내용 형식과
 * 동일한 데이터를 표현하기 위한 참조용 구조체로 정의되어 있다.
 * id[10]: 학번 (최대 9자리 + null)
 * total_score: 총점
 * csv_line: score.csv에 기록될 한 줄 전체 문자열
 */
struct student_score_result {
    char   id[10];
    double total_score;
    char   csv_line[BUFLEN * 4];
};

/* ================================================================
 * 함수 프로토타입
 * 각 함수는 ssu_score.c에 정의되어 있으며, 아래 그룹으로 역할이 분류된다.
 * ================================================================ */

/*
 * ---- 제어 함수 ----
 * ssu_score(): 채점 전체 흐름을 제어하는 메인 함수. main()에서 직접 호출된다.
 * check_option(): getopt()로 명령줄 옵션을 파싱하고 전역 플래그를 설정한다.
 * print_usage(): -h 옵션 시 사용법을 출력하고 반환한다.
 */
void ssu_score(int argc, char *argv[]);
int  check_option(int argc, char *argv[]);
void print_usage(void);

/*
 * ---- 채점 흐름 ----
 * score_students(): 전체 학생을 fork로 병렬 채점하고 결과를 score.csv에 병합한다.
 * score_student(): 한 학생의 모든 문제를 채점하고 CSV 행을 fd에 기록한다.
 * write_first_row(): score.csv의 첫 번째 행(헤더)을 단일 버퍼로 조립해 기록한다.
 */
void   score_students(void);
double score_student(int fd, char *id);
void   write_first_row(int fd);

/*
 * ---- 빈칸 채점 ----
 * get_answer(): ':' 구분자 이전의 답안 문자열을 64바이트 블록 단위로 읽어온다.
 * score_blank(): 학생 답안을 AST로 파싱하고 캐시된 정답 AST와 비교한다.
 */
char  *get_answer(int fd, char *result);
int    score_blank(char *id, char *filename);

/*
 * ---- 프로그램 채점 ----
 * score_program(): compile_program()과 execute_program()을 순서대로 호출해 결과를 종합한다.
 * compile_program(): 정답/학생 C 파일을 gcc로 컴파일하고 에러/경고 여부를 반환한다.
 * execute_program(): 학생 실행 파일을 fork/execvp로 실행하고 정답 출력과 비교한다.
 * check_error_warning(): 컴파일 에러 파일에서 "error:"/"warning:" 키워드를 탐색한다.
 * compare_resultfile(): 두 출력 파일을 블록 I/O로 읽어 공백/대소문자 무시 비교한다.
 */
double score_program(char *id, char *filename);
double compile_program(char *id, char *filename);
int    execute_program(char *id, char *filename);
double check_error_warning(char *filename);
int    compare_resultfile(char *file1, char *file2);

/*
 * ---- 정답 전처리 ----
 * preprocess_answers(): 선언만 남아있는 이전 버전 호환용 프로토타입.
 * is_ans_compiled/mark_ans_compiled: 정답 컴파일 완료 여부 캐시 조회/등록.
 * is_ans_executed/mark_ans_executed: 정답 실행 결과 생성 완료 여부 캐시 조회/등록.
 * 이 캐시들은 학생마다 정답을 반복 컴파일/실행하지 않기 위해 도입했다.
 */
void preprocess_answers(void);
int  is_ans_compiled(char *qname);
void mark_ans_compiled(char *qname);
int  is_ans_executed(char *qname);
void mark_ans_executed(char *qname);

/*
 * ---- 정답 전처리 병렬화 ----
 * preprocess_answers_parallel(): 정답 C 파일의 컴파일과 실행 결과 생성을 문제 단위 fork로 병렬화한다.
 *   자식에서 변경한 캐시는 COW로 부모와 분리되므로, 자식 종료 후 access()로 파일 존재를
 *   직접 확인하여 부모 캐시를 재구성한다.
 */
void preprocess_answers_parallel(void);

/*
 * ---- 정답 AST 캐시 ----
 * preprocess_blank_answers(): 빈칸 정답 파일 전체를 사전 파싱하여 AST 캐시를 구성한다.
 * 이 함수를 fork 이전에 호출하면 자식들이 COW로 캐시를 공유하여 파싱 비용이 없어진다.
 */
void preprocess_blank_answers(void);


/*
 * ---- 옵션 처리 ----
 * do_cOption(): score.csv에서 특정 학생의 점수만 찾아 출력한다.
 * is_exist(): 문자열이 배열 안에 존재하는지 선형 탐색으로 확인한다.
 */
void do_cOption(char (*ids)[FILELEN]);
int  is_exist(char (*src)[FILELEN], char *target);

/*
 * ---- 유틸리티 ----
 * is_thread(): qname이 -t 옵션의 pthread 대상 목록에 있는지 확인한다.
 *
 * build_argv(): 명령 문자열을 공백 기준으로 분리하여 execvp()용 argv 배열을 구성한다.
 *   redirection() 내부에서 호출되며, strtok_r()로 원본 문자열을 보존한 채 분리한다.
 *   반환값은 분리된 인자 수이며, argv의 마지막 슬롯은 항상 NULL로 설정된다.
 *   static 한정자를 사용하므로 이 선언은 참고용이며 외부 링크가 없다.
 *
 * redirection(): fork/dup2/execvp 기반으로 명령을 쉘 없이 직접 실행하며 fd를 리다이렉션한다.
 *   기존의 execl("/bin/sh", "sh", "-c", command) 방식은 쉘을 경유하여 system()과 동일한
 *   보안·성능 문제를 가졌다. 개선된 버전은 build_argv()로 명령을 직접 분리한 뒤
 *   execvp()로 실행 파일을 쉘 없이 구동하여 이 문제를 해결한다.
 *
 * get_file_type(): 확장자로 TEXTFILE/CFILE/-1을 반환한다.
 * rmdirs(): 디렉토리를 하위 내용까지 재귀적으로 삭제한다.
 * to_lower_case(): compare_resultfile()에서 대소문자 무시 비교에 사용한다.
 */
int   is_thread(char *qname);
void  redirection(char *command, int newfd, int oldfd);
int   get_file_type(char *filename);
void  rmdirs(const char *path);
void  to_lower_case(char *c);

/*
 * ---- 채점 기준표 관리 ----
 * set_scoreTable(): score_table.csv 존재 여부를 확인하고 읽거나 생성한다.
 * read_scoreTable(): 기존 CSV 파일을 읽어 score_table[] 배열에 채운다.
 * make_scoreTable(): 정답 디렉토리를 탐색하고 사용자 입력으로 배점을 받는다.
 * write_scoreTable(): score_table[]을 CSV로 저장한다.
 * set_idTable(): 학생 디렉토리를 탐색하여 id_table[]을 구성하고 정렬한다.
 * get_create_type(): 배점 입력 방식(일괄/개별)을 사용자에게 선택받는다.
 */
void set_scoreTable(char *ansDir);
void read_scoreTable(char *path);
void make_scoreTable(char *ansDir);
void write_scoreTable(char *filename);
void set_idTable(char *stuDir);
int  get_create_type(void);

/*
 * ---- 정렬 ----
 * sort_idTable(): id_table을 qsort로 사전순 정렬한다.
 * sort_scoreTable(): score_table을 qsort로 문제 번호 오름차순 정렬한다.
 * get_qname_number(): "1-2.c" 형태 파일명에서 숫자 두 개를 추출한다. 정렬 기준 계산에 쓰인다.
 */
void sort_idTable(int size);
void sort_scoreTable(int size);
void get_qname_number(char *qname, int *num1, int *num2);

#endif