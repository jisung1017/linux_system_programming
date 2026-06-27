#ifndef BLANK_H_
#define BLANK_H_

/*
 * true/false 정의.
 * ssu_score.h와 동일하게 정의하되 #ifndef로 중복 정의를 방지한다.
 */
#ifndef true
    #define true 1
#endif
#ifndef false
    #define false 0
#endif

/*
 * BUFLEN=1024: 문자열 버퍼 크기. ssu_score.h에도 동일하게 정의되어 있다.
 * 이 헤더에서 별도로 정의하는 이유는 blank.c가 ssu_score.h 없이도 단독으로
 * 컴파일될 수 있도록 하기 위함이다. #ifndef로 중복 정의를 피한다.
 */
#ifndef BUFLEN
    #define BUFLEN 1024
#endif

/*
 * OPERATOR_CNT=24: operators[] 테이블에 등록된 연산자의 수.
 * get_precedence(), is_operator()에서 이 배열을 순회할 때 범위로 사용된다.
 *
 * DATATYPE_SIZE=35: datatype[] 배열에 등록된 C 언어 기본 타입 키워드의 수.
 * make_tokens()와 is_typeStatement()에서 타입 선언문 판별 시 이 배열을 순회한다.
 *
 * MINLEN=128: 토큰 배열에서 각 토큰 하나의 최대 길이.
 * 기존 64에서 128로 올린 이유: "pthread_mutex_t" 같은 긴 타입명 토큰이
 * 64바이트 경계를 넘어 인접 슬롯을 오염시킬 수 있는 문제가 있었다.
 *
 * TOKEN_CNT=50: 하나의 답안 문자열에서 분리할 수 있는 최대 토큰 수.
 */
#define OPERATOR_CNT  24
#define DATATYPE_SIZE 35
#define MINLEN        128
#define TOKEN_CNT     50

/*
 * struct node (typedef node)
 * AST의 한 노드를 나타내는 구조체다.
 *
 * parentheses: 이 노드가 위치한 괄호 깊이. 우선순위 판별에 활용된다.
 * name: 노드의 값(연산자 문자열 또는 피연산자 문자열). create_node()에서 malloc으로 할당된다.
 * parent: 부모 노드 포인터. 트리를 루트 방향으로 탐색할 때 사용된다.
 * child_head: 첫 번째 자식 노드 포인터. 자식이 없으면 NULL이다.
 * prev: 이전 형제 노드 포인터. 형제 리스트를 역방향 탐색할 때 사용된다.
 * next: 다음 형제 노드 포인터. 형제 리스트를 순방향 탐색할 때 사용된다.
 */
typedef struct node {
    int          parentheses;
    char        *name;
    struct node *parent;
    struct node *child_head;
    struct node *prev;
    struct node *next;
} node;

/*
 * struct operator_precedence (typedef operator_precedence)
 * 연산자 하나의 문자열과 우선순위 숫자를 묶은 구조체.
 * operators[OPERATOR_CNT] 배열로 정의되어 연산자 우선순위 테이블을 구성한다.
 * 숫자가 작을수록 우선순위가 높다. (괄호=0이 최고, 대입=14가 최저)
 * get_precedence()와 is_operator()에서 이 배열을 순회하여 연산자를 식별한다.
 */
typedef struct operator_precedence {
    char *operator;
    int   precedence;
} operator_precedence;

/*
 * ---- AST 구성/비교 함수 ----
 *
 * compare_tree(): 두 AST의 루트부터 재귀 순회하며 논리적 동치를 비교한다.
 *   교환 법칙 성립 연산자(+, *, |, &, ==, != 등)는 피연산자 순서가 달라도 같다고 판단한다.
 *
 * make_tree(): 토큰 배열과 연산자 우선순위 테이블로 AST를 재귀 구성한다.
 *   괄호 레벨(parentheses)을 인자로 전달하여 괄호 안의 식이 더 높은 우선순위를 갖도록 한다.
 *
 * change_sibling(): 부모의 두 자식 노드 순서를 교환한다.
 *   교환 법칙 적용 시 피연산자 순서를 뒤집기 위해 compare_tree() 내부에서 사용된다.
 *
 * create_node(): malloc으로 노드를 동적 할당하고 name을 별도로 복사해 저장한다.
 *   이 함수로 생성된 노드는 반드시 free_node()로 해제해야 한다.
 *
 * get_precedence(): operators 테이블에서 연산자의 우선순위 숫자를 반환한다.
 * is_operator(): 주어진 문자열이 등록된 연산자인지 선형 탐색으로 확인한다.
 *
 * print(): 디버깅용. AST를 재귀적으로 출력한다.
 *
 * get_operator(): 현재 노드에서 가장 가까운 부모 연산자 노드를 반환한다.
 * get_root(): 현재 노드에서 트리 전체의 루트를 찾아 반환한다.
 * get_high_precedence_node(): 새 노드를 삽입할 위치를 우선순위 기준으로 탐색한다.
 * get_most_high_precedence_node(): 트리 전체에서 최적 삽입 위치를 찾는다.
 * insert_node(): old 위치에 new 노드를 삽입하고 old를 new의 자식으로 연결한다.
 * get_last_child(): child_head부터 next를 따라 마지막 자식을 반환한다.
 * free_node(): AST를 후위 순회로 재귀 해제한다. name과 노드 자체를 모두 free한다.
 * get_sibling_cnt(): 형제 노드의 수를 센다. 피연산자 개수 일치 확인에 사용한다.
 */
void   compare_tree(node *root1, node *root2, int *result);
node  *make_tree(node *root, char (*tokens)[MINLEN], int *idx, int parentheses);
node  *change_sibling(node *parent);
node  *create_node(char *name, int parentheses);
int    get_precedence(char *op);
int    is_operator(char *op);
void   print(node *cur);
node  *get_operator(node *cur);
node  *get_root(node *cur);
node  *get_high_precedence_node(node *cur, node *new);
node  *get_most_high_precedence_node(node *cur, node *new);
node  *insert_node(node *old, node *new);
node  *get_last_child(node *cur);
void   free_node(node *cur);
int    get_sibling_cnt(node *cur);

/*
 * ---- 토큰화 및 문자열 처리 함수 ----
 *
 * make_tokens(): 입력 문자열을 연산자/피연산자/괄호로 분리하여 tokens[][] 배열에 저장한다.
 *   strpbrk()로 연산자 문자를 탐색하며, *, & 같이 컨텍스트에 따라 의미가 다른 문자를
 *   앞뒤 토큰 내용을 보고 판별한다.
 *
 * is_typeStatement(): 문자열이 타입 선언문인지 확인한다.
 *   datatype[] 배열을 순회하여 앞부분이 타입명이면 2, 일반 표현식이면 1, 오류면 0 반환.
 *
 * find_typeSpecifier(): (타입)변수 형태의 형변환 패턴을 tokens 배열에서 탐색한다.
 * find_typeSpecifier2(): struct 뒤에 식별자가 오는 패턴을 탐색한다.
 * reset_tokens(): 위 두 함수로 찾은 패턴을 정규화한다. memmove를 써서 overlapping 안전.
 *
 * is_character(): 알파벳/숫자인지 확인. 식별자 문자 판별에 사용된다.
 * all_star(): 문자열이 '*'로만 구성되었는지 확인. 포인터 선언 판별에 사용한다.
 * all_character(): 문자열에 알파벳/숫자가 하나라도 있으면 1 반환.
 * clear_tokens(): memset으로 tokens[][] 배열 전체를 0으로 초기화한다.
 * get_token_cnt(): 비어있지 않은 토큰의 수를 반환한다.
 *
 * rtrim(): 오른쪽 공백 제거. in-place 방식으로 원본 문자열에 '\0'을 삽입한다.
 *   기존 구현은 로컬 배열 주소를 반환하는 UB가 있었다. 이를 수정한 버전이다.
 *
 * ltrim(): 왼쪽 공백 제거. 포인터를 앞으로 이동하여 반환한다(in-place가 아님).
 *
 * remove_extraspace(): 연속된 공백을 단일 공백으로 축약한다.
 *   기존의 malloc 반환 방식에서 호출자 제공 out 버퍼 방식으로 변경하여 메모리 누수를 없앴다.
 *
 * remove_space(): 문자열에서 모든 공백을 제거한다. in-place로 문자를 압축한다.
 *   is_typeStatement()에서 타입명 비교 전처리에 사용된다.
 *
 * check_brackets(): 괄호 짝이 맞는지 확인한다. strpbrk()로 '('와 ')'를 순차 탐색한다.
 */
int    make_tokens(char *str, char tokens[TOKEN_CNT][MINLEN]);
int    is_typeStatement(char *str);
int    find_typeSpecifier(char tokens[TOKEN_CNT][MINLEN]);
int    find_typeSpecifier2(char tokens[TOKEN_CNT][MINLEN]);
int    is_character(char c);
int    all_star(char *str);
int    all_character(char *str);
int    reset_tokens(int start, char tokens[TOKEN_CNT][MINLEN]);
void   clear_tokens(char tokens[TOKEN_CNT][MINLEN]);
int    get_token_cnt(char tokens[TOKEN_CNT][MINLEN]);
char  *rtrim(char *str);
char  *ltrim(char *str);
void   remove_extraspace(char *str, char *out);
void   remove_space(char *str);
int    check_brackets(char *str);

#endif 