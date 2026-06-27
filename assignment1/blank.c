/*
 * ssu_socore.c 와 중복되어 주석을 따로 달지 않겠음.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
/*
 * #include <ctype.h>
 * isspace() 함수를 사용하기 위해 포함한다.
 * rtrim()과 ltrim()에서 공백 문자를 판별할 때 사용된다.
 */
#include <ctype.h>
#include "blank.h"

/*
 * datatype[DATATYPE_SIZE][MINLEN]
 * C 언어 기본 타입 키워드 목록. DATATYPE_SIZE=35개가 등록되어 있다.
 * is_typeStatement()에서 표현식이 타입 선언문인지 판별할 때 이 배열을 순회한다.
 * make_tokens()에서 '*'가 포인터 선언인지 곱셈인지 구분할 때도 참조한다.
 * MINLEN=128로 선언하여 "pthread_mutex_t"(15자) 같은 긴 타입명도 안전하게 수용한다.
 */
char datatype[DATATYPE_SIZE][MINLEN] = {
    "int",    "char",    "double",   "float",    "long",
    "short",  "ushort",  "FILE",     "DIR",      "pid",
    "key_t",  "ssize_t", "mode_t",   "ino_t",    "dev_t",
    "nlink_t","uid_t",   "gid_t",    "time_t",   "blksize_t",
    "blkcnt_t","pid_t",  "pthread_mutex_t","pthread_cond_t","pthread_t",
    "void",   "size_t",  "unsigned", "sigset_t", "sigjmp_buf",
    "rlim_t", "jmp_buf", "sig_atomic_t","clock_t","struct"
};

/*
 * operators[OPERATOR_CNT]
 * 연산자 우선순위 테이블. OPERATOR_CNT=24개의 연산자가 등록되어 있다.
 * 숫자가 작을수록 우선순위가 높다. 괄호(0)가 최고, 대입(14)이 최저.
 * make_tree()에서 새 노드를 삽입할 위치를 결정할 때 이 테이블을 참조한다.
 * get_precedence()와 is_operator()가 이 배열을 선형 탐색하여 연산자를 식별한다.
 *
 * 참고: 인덱스 0, 1의 괄호는 우선순위=0이지만 make_tree()에서 특수 처리되므로
 * get_precedence()에서 인덱스 2부터 탐색하는 이유가 여기에 있다.
 */
operator_precedence operators[OPERATOR_CNT] = {
    {"(",  0}, {")",  0},
    {"->", 1},
    {"*",  4}, {"/",  3}, {"%",  2},
    {"+",  6}, {"-",  5},
    {"<",  7}, {"<=", 7}, {">",  7}, {">=", 7},
    {"==", 8}, {"!=", 8},
    {"&",  9},
    {"^",  10},
    {"|",  11},
    {"&&", 12},
    {"||", 13},
    {"=",  14}, {"+=", 14}, {"-=", 14}, {"&=", 14}, {"|=", 14}
};

/*
 * compare_tree():
 * 두 AST를 루트부터 재귀 순회하며 구조와 값이 동치인지 비교한다.
 * 교환 법칙이 성립하는 연산자(+, *, |, &, ==, != 등)는 피연산자 순서가 달라도 동치로 처리한다.
 * 비교 결과는 int *result에 저장된다. (true=1, false=0)
 */
void compare_tree(node *root1, node *root2, int *result)
{
    node *tmp;

    /* NULL 체크: 한쪽만 NULL이면 구조가 다른 것 */
    if (root1 == NULL || root2 == NULL) {
        *result = false;
        return;
    }

    /* 부등호 방향이 다를 경우 피연산자를 교환하여 동치 처리.
       예) a < b 와 b > a는 논리적으로 같다 */
    if (!strcmp(root1->name, "<")  || !strcmp(root1->name, ">") ||
        !strcmp(root1->name, "<=") || !strcmp(root1->name, ">=")) {
        if (strcmp(root1->name, root2->name) != 0) {
            /* strncpy()로 연산자 문자열을 반전 교체 */
            if      (!strncmp(root2->name, "<=", 2)) strncpy(root2->name, ">=", 2);
            else if (!strncmp(root2->name, ">=", 2)) strncpy(root2->name, "<=", 2);
            else if (!strncmp(root2->name, "<",  1)) strncpy(root2->name, ">",  1);
            else if (!strncmp(root2->name, ">",  1)) strncpy(root2->name, "<",  1);
            root2 = change_sibling(root2);
        }
    }

    if (strcmp(root1->name, root2->name) != 0) {
        *result = false;
        return;
    }

    /* 자식 존재 여부가 다르면 구조 불일치 */
    if ((root1->child_head != NULL && root2->child_head == NULL) ||
        (root1->child_head == NULL && root2->child_head != NULL)) {
        *result = false;
        return;
    }
    else if (root1->child_head != NULL) {
        /* get_sibling_cnt(): 형제 노드 수를 비교하여 피연산자 개수 일치 확인 */
        if (get_sibling_cnt(root1->child_head) != get_sibling_cnt(root2->child_head)) {
            *result = false;
            return;
        }

        if (!strcmp(root1->name, "==") || !strcmp(root1->name, "!=")) {
            /* == 와 != 는 교환 법칙 적용: a==b 와 b==a는 동치 */
            compare_tree(root1->child_head, root2->child_head, result);
            if (*result == false) {
                *result = true;
                root2 = change_sibling(root2);
                compare_tree(root1->child_head, root2->child_head, result);
            }
        }
        else if (!strcmp(root1->name, "+")  || !strcmp(root1->name, "*") ||
                 !strcmp(root1->name, "|")  || !strcmp(root1->name, "&") ||
                 !strcmp(root1->name, "||") || !strcmp(root1->name, "&&")) {
            /* 교환 법칙 연산자: 자식 노드의 모든 순열 조합으로 비교 */
            if (get_sibling_cnt(root1->child_head) != get_sibling_cnt(root2->child_head)) {
                *result = false;
                return;
            }
            tmp = root2->child_head;
            while (tmp->prev != NULL) tmp = tmp->prev; /* 첫 번째 형제로 이동 */
            while (tmp != NULL) {
                compare_tree(root1->child_head, tmp, result);
                if (*result == true) break;
                else {
                    if (tmp->next != NULL) *result = true;
                    tmp = tmp->next;
                }
            }
        }
        else {
            compare_tree(root1->child_head, root2->child_head, result);
        }
    }

    if (root1->next != NULL) {
        if (get_sibling_cnt(root1) != get_sibling_cnt(root2)) {
            *result = false;
            return;
        }
        if (*result == true) {
            tmp = get_operator(root1);
            if (!strcmp(tmp->name, "+")  || !strcmp(tmp->name, "*") ||
                !strcmp(tmp->name, "|")  || !strcmp(tmp->name, "&") ||
                !strcmp(tmp->name, "||") || !strcmp(tmp->name, "&&")) {
                tmp = root2;
                while (tmp->prev != NULL) tmp = tmp->prev;
                while (tmp != NULL) {
                    compare_tree(root1->next, tmp, result);
                    if (*result == true) break;
                    else {
                        if (tmp->next != NULL) *result = true;
                        tmp = tmp->next;
                    }
                }
            }
            else {
                compare_tree(root1->next, root2->next, result);
            }
        }
    }
}

/*
 * make_tokens():
 * 입력 문자열을 연산자, 피연산자, 괄호 등으로 분리하여 tokens[][] 배열에 저장한다.
 * strpbrk()로 연산자 문자를 탐색하고 앞부분을 피연산자로, 해당 위치를 연산자로 분리한다.
 * 포인터(*)와 곱셈(*), 주소(&)와 비트AND(&), 증감(--, ++)과 빼기(-)/더하기(+) 등
 * 컨텍스트에 따라 구분이 필요한 문자들을 세밀하게 처리한다.
 */
int make_tokens(char *str, char tokens[TOKEN_CNT][MINLEN])
{
    char *start, *end;
    char  tmp[BUFLEN];
    char *op = "(),;><=!|&^/+-*\""; /* 연산자 후보 문자들. strpbrk 탐색 기준 */
    int   row = 0;
    int   i;
    int   isPointer;
    int   lcount, rcount;
    int   p_str;

    /* clear_tokens(): memset으로 토큰 배열 전체를 0으로 초기화 */
    clear_tokens(tokens);
    start = str;

    /* is_typeStatement(): 타입 선언문이면 2, 일반 표현식이면 1, 오류면 0 반환 */
    if (is_typeStatement(str) == 0)
        return false;

    while (1) {
        /* strpbrk(): 문자열에서 op에 포함된 문자 중 첫 번째 위치를 반환 */
        if ((end = strpbrk(start, op)) == NULL)
            break;

        if (start == end) {
            /* 연산자 위치에 도달. 2문자 연산자를 먼저 확인하고 1문자 연산자를 처리한다 */
            if (!strncmp(start, "--", 2) || !strncmp(start, "++", 2)) {
                if (!strncmp(start, "++++", 4) || !strncmp(start, "----", 4))
                    return false;

                /* ++a(전위 증감) vs a++(후위 증감) 구분 */
                if (is_character(*ltrim(start + 2))) {
                    if (row > 0 && is_character(tokens[row - 1][strlen(tokens[row - 1]) - 1]))
                        return false;
                    end = strpbrk(start + 2, op);
                    if (end == NULL) end = &str[strlen(str)];
                    while (start < end) {
                        if (*(start - 1) == ' ' && is_character(tokens[row][strlen(tokens[row]) - 1]))
                            return false;
                        else if (*start != ' ')
                            strncat(tokens[row], start, 1);
                        start++;
                    }
                }
                else if (row > 0 && is_character(tokens[row - 1][strlen(tokens[row - 1]) - 1])) {
                    if (strstr(tokens[row - 1], "++") != NULL || strstr(tokens[row - 1], "--") != NULL)
                        return false;
                    memset(tmp, 0, sizeof(tmp));
                    strncpy(tmp, start, 2);
                    strcat(tokens[row - 1], tmp);
                    start += 2;
                    row--;
                }
                else {
                    memset(tmp, 0, sizeof(tmp));
                    strncpy(tmp, start, 2);
                    strcat(tokens[row], tmp);
                    start += 2;
                }
            }
            /* 2문자 연산자 처리: strncpy로 2바이트를 한 번에 토큰으로 저장 */
            else if (!strncmp(start, "==", 2) || !strncmp(start, "!=", 2) ||
                     !strncmp(start, "<=", 2) || !strncmp(start, ">=", 2) ||
                     !strncmp(start, "||", 2) || !strncmp(start, "&&", 2) ||
                     !strncmp(start, "&=", 2) || !strncmp(start, "^=", 2) ||
                     !strncmp(start, "|=", 2) || !strncmp(start, "+=", 2) ||
                     !strncmp(start, "-=", 2) || !strncmp(start, "*=", 2) ||
                     !strncmp(start, "/=", 2)) {
                strncpy(tokens[row], start, 2);
                start += 2;
            }
            /* -> 연산자: a->b를 단일 토큰으로 합친다 */
            else if (!strncmp(start, "->", 2)) {
                end = strpbrk(start + 2, op);
                if (end == NULL) end = &str[strlen(str)];
                while (start < end) {
                    if (*start != ' ') strncat(tokens[row - 1], start, 1);
                    start++;
                }
                row--;
            }
            /* &: 주소 연산자(&a)와 비트 AND(a & b)를 컨텍스트로 구분 */
            else if (*end == '&') {
                if (row == 0 || (strpbrk(tokens[row - 1], op) != NULL)) {
                    end = strpbrk(start + 1, op);
                    if (end == NULL) end = &str[strlen(str)];
                    strncat(tokens[row], start, 1);
                    start++;
                    while (start < end) {
                        if (*(start - 1) == ' ' && tokens[row][strlen(tokens[row]) - 1] != '&')
                            return false;
                        else if (*start != ' ')
                            strncat(tokens[row], start, 1);
                        start++;
                    }
                }
                else {
                    strncpy(tokens[row], start, 1);
                    start += 1;
                }
            }
            /* *: 포인터 선언(char *p)과 곱셈(a * b)을 datatype 배열로 구분 */
            else if (*end == '*') {
                isPointer = 0;
                if (row > 0) {
                    /* 이전 토큰이 타입명이면 포인터 선언으로 판단 */
                    for (i = 0; i < DATATYPE_SIZE; i++) {
                        if (strstr(tokens[row - 1], datatype[i]) != NULL) {
                            strcat(tokens[row - 1], "*");
                            start += 1;
                            isPointer = 1;
                            break;
                        }
                    }
                    if (isPointer == 1) continue;
                    if (*(start + 1) != 0) end = start + 1;
                    if (row > 1 && !strcmp(tokens[row - 2], "*") && (all_star(tokens[row - 1]) == 1)) {
                        strncat(tokens[row - 1], start, end - start);
                        row--;
                    }
                    else if (is_character(tokens[row - 1][strlen(tokens[row - 1]) - 1]) == 1) {
                        strncat(tokens[row], start, end - start);
                    }
                    else if (strpbrk(tokens[row - 1], op) != NULL) {
                        strncat(tokens[row], start, end - start);
                    }
                    else {
                        strncat(tokens[row], start, end - start);
                    }
                    start += (end - start);
                }
                else if (row == 0) {
                    if ((end = strpbrk(start + 1, op)) == NULL) {
                        strncat(tokens[row], start, 1);
                        start += 1;
                    }
                    else {
                        while (start < end) {
                            if (*(start - 1) == ' ' && is_character(tokens[row][strlen(tokens[row]) - 1]))
                                return false;
                            else if (*start != ' ')
                                strncat(tokens[row], start, 1);
                            start++;
                        }
                        if (all_star(tokens[row])) row--;
                    }
                }
            }
            /* '(': 함수 호출 괄호와 그룹핑 괄호를 구분하여 처리 */
            else if (*end == '(') {
                lcount = 0;
                rcount = 0;
                if (row > 0 && (strcmp(tokens[row - 1], "&") == 0 ||
                                strcmp(tokens[row - 1], "*") == 0)) {
                    while (*(end + lcount + 1) == '(') lcount++;
                    start += lcount;
                    end = strpbrk(start + 1, ")");
                    if (end == NULL) return false;
                    else {
                        while (*(end + rcount + 1) == ')') rcount++;
                        end += rcount;
                        if (lcount != rcount) return false;
                        if ((row > 1 && !is_character(tokens[row - 2][strlen(tokens[row - 2]) - 1]))
                            || row == 1) {
                            strncat(tokens[row - 1], start + 1, end - start - rcount - 1);
                            row--;
                            start = end + 1;
                        }
                        else {
                            strncat(tokens[row], start, 1);
                            start += 1;
                        }
                    }
                }
                else {
                    strncat(tokens[row], start, 1);
                    start += 1;
                }
            }
            /* '"': 문자열 리터럴을 통째로 하나의 토큰으로 처리 */
            else if (*end == '\"') {
                end = strpbrk(start + 1, "\"");
                if (end == NULL) return false;
                strncat(tokens[row], start, end - start + 1);
                start = end + 1;
            }
            else {
                if (row > 0 && !strcmp(tokens[row - 1], "++")) return false;
                if (row > 0 && !strcmp(tokens[row - 1], "--")) return false;
                strncat(tokens[row], start, 1);
                start += 1;
                /* 단항 부호 연산자(-, +) 처리: 이전 토큰이 연산자면 피연산자로 합친다 */
                if (!strcmp(tokens[row], "-") || !strcmp(tokens[row], "+") ||
                    !strcmp(tokens[row], "--") || !strcmp(tokens[row], "++")) {
                    if (row == 0) row--;
                    else if (!is_character(tokens[row - 1][strlen(tokens[row - 1]) - 1])) {
                        if (strstr(tokens[row - 1], "++") == NULL &&
                            strstr(tokens[row - 1], "--") == NULL)
                            row--;
                    }
                }
            }
        }
        else {
            /* start != end: 연산자 앞의 피연산자 문자열 처리 */
            if (all_star(tokens[row - 1]) && row > 1 &&
                !is_character(tokens[row - 2][strlen(tokens[row - 2]) - 1]))
                row--;
            if (all_star(tokens[row - 1]) && row == 1) row--;

            for (i = 0; i < end - start; i++) {
                if (i > 0 && *(start + i) == '.') {
                    strncat(tokens[row], start + i, 1);
                    while (*(start + i + 1) == ' ' && i < end - start) i++;
                }
                else if (start[i] == ' ') {
                    while (start[i] == ' ') i++;
                    break;
                }
                else {
                    strncat(tokens[row], start + i, 1);
                }
            }
            if (start[0] == ' ') { start += i; continue; }
            start += i;
        }

        /* ltrim: 포인터 이동으로 좌측 공백 제거(in-place 아님)
           rtrim: '\0' 삽입으로 우측 공백 제거(in-place) */
        strcpy(tokens[row], ltrim(rtrim(tokens[row])));

        /* 연속된 식별자 토큰 체크: 타입명 없이 두 식별자가 연속되면 파싱 오류 */
        if (row > 0 &&
            is_character(tokens[row][strlen(tokens[row]) - 1]) &&
            (is_typeStatement(tokens[row - 1]) == 2 ||
             is_character(tokens[row - 1][strlen(tokens[row - 1]) - 1]) ||
             tokens[row - 1][strlen(tokens[row - 1]) - 1] == '.')) {
            if (row > 1 && strcmp(tokens[row - 2], "(") == 0) {
                if (strcmp(tokens[row - 1], "struct") != 0 &&
                    strcmp(tokens[row - 1], "unsigned") != 0)
                    return false;
            }
            else if (row == 1 && is_character(tokens[row][strlen(tokens[row]) - 1])) {
                if (strcmp(tokens[0], "extern") != 0 &&
                    strcmp(tokens[0], "unsigned") != 0 &&
                    is_typeStatement(tokens[0]) != 2)
                    return false;
            }
            else if (row > 1 && is_typeStatement(tokens[row - 1]) == 2) {
                if (strcmp(tokens[row - 2], "unsigned") != 0 &&
                    strcmp(tokens[row - 2], "extern") != 0)
                    return false;
            }
        }

        /* gcc 명령어면 전체를 단일 토큰으로 처리 */
        if (row == 0 && !strcmp(tokens[row], "gcc")) {
            clear_tokens(tokens);
            strcpy(tokens[0], str);
            return 1;
        }
        row++;
    }

    /* 루프 종료 후 남은 문자열 처리 */
    if (all_star(tokens[row - 1]) && row > 1 &&
        !is_character(tokens[row - 2][strlen(tokens[row - 2]) - 1]))
        row--;
    if (all_star(tokens[row - 1]) && row == 1) row--;

    for (i = 0; i < (int)strlen(start); i++) {
        if (start[i] == ' ') {
            while (start[i] == ' ') i++;
            if (start[0] == ' ') { start += i; i = 0; }
            else { row++; }
            i--;
        }
        else {
            strncat(tokens[row], start + i, 1);
            if (start[i] == '.' && i < (int)strlen(start)) {
                while (start[i + 1] == ' ' && i < (int)strlen(start)) i++;
            }
        }
        strcpy(tokens[row], ltrim(rtrim(tokens[row])));

        if (!strcmp(tokens[row], "lpthread") && row > 0 && !strcmp(tokens[row - 1], "-")) {
            strcat(tokens[row - 1], tokens[row]);
            memset(tokens[row], 0, sizeof(tokens[row]));
            row--;
        }
        else if (row > 0 &&
                 is_character(tokens[row][strlen(tokens[row]) - 1]) &&
                 (is_typeStatement(tokens[row - 1]) == 2 ||
                  is_character(tokens[row - 1][strlen(tokens[row - 1]) - 1]) ||
                  tokens[row - 1][strlen(tokens[row - 1]) - 1] == '.')) {
            if (row > 1 && strcmp(tokens[row - 2], "(") == 0) {
                if (strcmp(tokens[row - 1], "struct") != 0 &&
                    strcmp(tokens[row - 1], "unsigned") != 0)
                    return false;
            }
            else if (row == 1 && is_character(tokens[row][strlen(tokens[row]) - 1])) {
                if (strcmp(tokens[0], "extern") != 0 &&
                    strcmp(tokens[0], "unsigned") != 0 &&
                    is_typeStatement(tokens[0]) != 2)
                    return false;
            }
            else if (row > 1 && is_typeStatement(tokens[row - 1]) == 2) {
                if (strcmp(tokens[row - 2], "unsigned") != 0 &&
                    strcmp(tokens[row - 2], "extern") != 0)
                    return false;
            }
        }
    }

    if (row > 0) {
        /* #include, struct는 전체를 하나의 토큰으로 합친다 */
        if (strcmp(tokens[0], "#include") == 0 ||
            strcmp(tokens[0], "include")  == 0 ||
            strcmp(tokens[0], "struct")   == 0) {
            char extraspace_buf[BUFLEN];
            strncpy(extraspace_buf, str, BUFLEN - 1);
            extraspace_buf[BUFLEN - 1] = '\0';
            clear_tokens(tokens);
            /* remove_extraspace(): malloc 누수 제거 버전. out 버퍼에 직접 기록한다 */
            remove_extraspace(extraspace_buf, tokens[0]);
        }
    }

    /* 타입 선언문이면 모든 토큰을 첫 번째 토큰으로 합친다 */
    if (is_typeStatement(tokens[0]) == 2 || strstr(tokens[0], "extern") != NULL) {
        for (i = 1; i < TOKEN_CNT; i++) {
            if (strcmp(tokens[i], "") == 0) break;
            if (i != TOKEN_CNT - 1) strcat(tokens[0], " ");
            strcat(tokens[0], tokens[i]);
            memset(tokens[i], 0, sizeof(tokens[i]));
        }
    }

    /* find_typeSpecifier/find_typeSpecifier2: 형변환 패턴을 탐색하여 reset_tokens()로 정규화 */
    while ((p_str = find_typeSpecifier(tokens)) != -1) {
        if (!reset_tokens(p_str, tokens)) return false;
    }
    while ((p_str = find_typeSpecifier2(tokens)) != -1) {
        if (!reset_tokens(p_str, tokens)) return false;
    }

    return true;
}

/*
 * make_tree():
 * 토큰 배열을 연산자 우선순위에 따라 AST로 구성한다.
 * 재귀적으로 호출되며 괄호 수준(parentheses)을 인자로 전달한다.
 * 반환값: 현재 서브트리의 루트 노드 포인터
 */
node *make_tree(node *root, char (*tokens)[MINLEN], int *idx, int parentheses)
{
    node *cur = root;
    node *new;
    node *operator;
    int   fstart;
    int   i;

    while (1) {
        if (strcmp(tokens[*idx], "") == 0) break;

        if (!strcmp(tokens[*idx], ")"))
            return get_root(cur); /* 닫는 괄호: 현재 레벨의 루트를 반환 */
        else if (!strcmp(tokens[*idx], ","))
            return get_root(cur); /* 쉼표: 현재 레벨의 루트를 반환 */
        else if (!strcmp(tokens[*idx], "(")) {
            /* 이전 토큰이 식별자면 함수 호출, 아니면 그룹핑 괄호 */
            if (*idx > 0 && !is_operator(tokens[*idx - 1]) &&
                strcmp(tokens[*idx - 1], ",") != 0) {
                fstart = true;
                while (1) {
                    *idx += 1;
                    if (!strcmp(tokens[*idx], ")")) break;
                    new = make_tree(NULL, tokens, idx, parentheses + 1);
                    if (new != NULL) {
                        if (fstart == true) {
                            cur->child_head = new;
                            new->parent = cur;
                            fstart = false;
                        }
                        else {
                            cur->next = new;
                            new->prev = cur;
                        }
                        cur = new;
                    }
                    if (!strcmp(tokens[*idx], ")")) break;
                }
            }
            else {
                *idx += 1;
                new = make_tree(NULL, tokens, idx, parentheses + 1);
                if (cur == NULL) cur = new;
                else if (!strcmp(new->name, cur->name)) {
                    if (!strcmp(new->name, "|")  || !strcmp(new->name, "||") ||
                        !strcmp(new->name, "&")  || !strcmp(new->name, "&&")) {
                        cur = get_last_child(cur);
                        if (new->child_head != NULL) {
                            new = new->child_head;
                            new->parent->child_head = NULL;
                            new->parent = NULL;
                            new->prev = cur;
                            cur->next = new;
                        }
                    }
                    else if (!strcmp(new->name, "+") || !strcmp(new->name, "*")) {
                        i = 0;
                        while (1) {
                            if (!strcmp(tokens[*idx + i], "")) break;
                            if (is_operator(tokens[*idx + i]) && strcmp(tokens[*idx + i], ")") != 0) break;
                            i++;
                        }
                        if (get_precedence(tokens[*idx + i]) < get_precedence(new->name)) {
                            cur = get_last_child(cur);
                            cur->next = new;
                            new->prev = cur;
                            cur = new;
                        }
                        else {
                            cur = get_last_child(cur);
                            if (new->child_head != NULL) {
                                new = new->child_head;
                                new->parent->child_head = NULL;
                                new->parent = NULL;
                                new->prev = cur;
                                cur->next = new;
                            }
                        }
                    }
                    else {
                        cur = get_last_child(cur);
                        cur->next = new;
                        new->prev = cur;
                        cur = new;
                    }
                }
                else {
                    cur = get_last_child(cur);
                    cur->next = new;
                    new->prev = cur;
                    cur = new;
                }
            }
        }
        else if (is_operator(tokens[*idx])) {
            /* 교환 법칙 연산자: 기존 트리에서 적절한 위치를 찾아 삽입 */
            if (!strcmp(tokens[*idx], "||") || !strcmp(tokens[*idx], "&&") ||
                !strcmp(tokens[*idx], "|")  || !strcmp(tokens[*idx], "&") ||
                !strcmp(tokens[*idx], "+")  || !strcmp(tokens[*idx], "*")) {
                if (is_operator(cur->name) && !strcmp(cur->name, tokens[*idx]))
                    operator = cur;
                else {
                    new = create_node(tokens[*idx], parentheses);
                    operator = get_most_high_precedence_node(cur, new);
                    if (operator->parent == NULL && operator->prev == NULL) {
                        if (get_precedence(operator->name) < get_precedence(new->name))
                            cur = insert_node(operator, new);
                        else if (get_precedence(operator->name) > get_precedence(new->name)) {
                            if (operator->child_head != NULL) {
                                operator = get_last_child(operator);
                                cur = insert_node(operator, new);
                            }
                        }
                        else {
                            operator = cur;
                            while (1) {
                                if (is_operator(operator->name) && !strcmp(operator->name, tokens[*idx])) break;
                                if (operator->prev != NULL) operator = operator->prev;
                                else break;
                            }
                            if (strcmp(operator->name, tokens[*idx]) != 0) operator = operator->parent;
                            if (operator != NULL && !strcmp(operator->name, tokens[*idx])) cur = operator;
                        }
                    }
                    else cur = insert_node(operator, new);
                }
            }
            else {
                /* 일반 연산자: get_most_high_precedence_node로 삽입 위치 결정 */
                new = create_node(tokens[*idx], parentheses);
                if (cur == NULL) cur = new;
                else {
                    operator = get_most_high_precedence_node(cur, new);
                    if (operator->parentheses > new->parentheses)
                        cur = insert_node(operator, new);
                    else if (operator->parent == NULL && operator->prev == NULL) {
                        if (get_precedence(operator->name) > get_precedence(new->name)) {
                            if (operator->child_head != NULL) {
                                operator = get_last_child(operator);
                                cur = insert_node(operator, new);
                            }
                        }
                        else cur = insert_node(operator, new);
                    }
                    else cur = insert_node(operator, new);
                }
            }
        }
        else {
            /* 피연산자(리프 노드): create_node()로 생성하여 트리에 연결 */
            new = create_node(tokens[*idx], parentheses);
            if (cur == NULL) cur = new;
            else if (cur->child_head == NULL) {
                cur->child_head = new;
                new->parent = cur;
                cur = new;
            }
            else {
                cur = get_last_child(cur);
                cur->next = new;
                new->prev = cur;
                cur = new;
            }
        }
        *idx += 1;
    }
    return get_root(cur);
}

/*
 * change_sibling():
 * 부모 노드의 두 자식 노드 순서를 교환한다.
 * compare_tree()에서 교환 법칙 연산자(==, !=, <, > 등)의 피연산자 순서를
 * 뒤집어 동치 여부를 재검사할 때 사용한다.
 * 예: a < b와 b > a의 비교에서 child_head와 그 next 노드의 순서를 바꾼다.
 */
node *change_sibling(node *parent)
{
    node *tmp;
    tmp = parent->child_head;
    parent->child_head = parent->child_head->next;
    parent->child_head->parent = parent;
    parent->child_head->prev = NULL;
    parent->child_head->next = tmp;
    parent->child_head->next->prev = parent->child_head;
    parent->child_head->next->next = NULL;
    parent->child_head->next->parent = NULL;
    return parent;
}

/*
 * create_node():
 * 노드를 동적 할당하고 초기화한다.
 * malloc()으로 node 구조체와 name 문자열을 각각 별도로 할당한다.
 * name을 별도 malloc하는 이유: 연산자("+")는 2바이트, 식별자는 수십 바이트로
 * 크기가 제각각이므로 포인터로 관리하면 필요한 만큼만 할당할 수 있다.
 * 이 함수로 생성된 모든 노드는 반드시 free_node()로 해제해야 한다.
 */
node *create_node(char *name, int parentheses)
{
    node *new = (node *)malloc(sizeof(node));          /* 노드 구조체 할당 */
    new->name = (char *)malloc(sizeof(char) * (strlen(name) + 1)); /* name 문자열 할당 */
    strcpy(new->name, name);  /* strcpy: name 복사. 할당 크기가 strlen+1이므로 안전하다 */
    new->parentheses = parentheses;
    /* 모든 포인터 필드를 NULL로 초기화. 쓰레기 값이 링크로 해석되는 것을 방지 */
    new->parent = NULL;
    new->child_head = NULL;
    new->prev = NULL;
    new->next = NULL;
    return new;
}

/*
 * get_precedence():
 * operators[] 테이블에서 연산자 문자열의 우선순위 숫자를 반환한다.
 * 인덱스 0, 1의 괄호는 make_tree()에서 특수 처리되므로 인덱스 2부터 탐색한다.
 * 테이블에 없으면 false(0)를 반환한다.
 */
int get_precedence(char *op)
{
    int i;
    /* 인덱스 2부터 시작: 0, 1번이 괄호이며 우선순위=0이라 별도 처리가 필요하다 */
    for (i = 2; i < OPERATOR_CNT; i++)
        if (!strcmp(operators[i].operator, op)) return operators[i].precedence;
    return false;
}

/*
 * is_operator():
 * 주어진 문자열이 operators[] 테이블에 등록된 연산자인지 확인한다.
 * make_tree()에서 토큰이 연산자인지 피연산자인지 구분하는 데 사용된다.
 */
int is_operator(char *op)
{
    int i;
    for (i = 0; i < OPERATOR_CNT; i++) {
        if (operators[i].operator == NULL) break;
        if (!strcmp(operators[i].operator, op)) return true;
    }
    return false;
}

/* print(): 디버깅용. AST를 재귀적으로 출력한다. 실제 채점에서는 호출되지 않는다 */
void print(node *cur)
{
    if (cur->child_head != NULL) { print(cur->child_head); printf("\n"); }
    if (cur->next       != NULL) { print(cur->next); printf("\t"); }
    printf("%s", cur->name);
}

/*
 * get_operator():
 * 현재 노드(또는 형제 리스트의 첫 번째 노드)의 부모 노드를 반환한다.
 * 같은 레벨의 형제 중 첫 번째(prev가 없는 노드)의 parent가 연산자 노드이다.
 * compare_tree()에서 교환 법칙 검사 시 부모 연산자 종류를 확인하는 데 사용된다.
 */
node *get_operator(node *cur)
{
    if (cur == NULL) return cur;
    if (cur->prev != NULL) while (cur->prev != NULL) cur = cur->prev;
    return cur->parent;
}

/*
 * get_root():
 * 현재 노드에서 트리 전체의 루트 노드를 찾아 반환한다.
 * prev를 따라 첫 번째 형제로 이동한 뒤 parent를 재귀적으로 따라간다.
 * make_tree()의 각 처리 단계가 끝날 때 현재 서브트리의 루트를 반환하는 데 사용된다.
 */
node *get_root(node *cur)
{
    if (cur == NULL) return cur;
    while (cur->prev != NULL) cur = cur->prev; /* 가장 앞 형제로 이동 */
    if (cur->parent != NULL) cur = get_root(cur->parent); /* 부모 레벨로 재귀 */
    return cur;
}

node *get_high_precedence_node(node *cur, node *new)
{
    if (is_operator(cur->name))
        if (get_precedence(cur->name) < get_precedence(new->name))
            return cur;
    if (cur->prev != NULL) {
        while (cur->prev != NULL) {
            cur = cur->prev;
            return get_high_precedence_node(cur, new);
        }
        if (cur->parent != NULL)
            return get_high_precedence_node(cur->parent, new);
    }
    if (cur->parent == NULL) return cur;
    return cur;
}

/*
 * get_most_high_precedence_node():
 * 트리 전체에서 새 노드를 삽입할 최적 위치를 찾는다.
 * get_high_precedence_node()를 루트까지 반복 적용하여
 * 우선순위 기준으로 가장 적합한 삽입 지점을 탐색한다.
 */
node *get_most_high_precedence_node(node *cur, node *new)
{
    node *op  = get_high_precedence_node(cur, new);
    node *sav = op;
    while (1) {
        if (sav->parent == NULL) break;
        if (sav->prev   != NULL) op = get_high_precedence_node(sav->prev,   new);
        else if (sav->parent != NULL) op = get_high_precedence_node(sav->parent, new);
        sav = op;
    }
    return sav;
}

/*
 * insert_node():
 * old 위치에 new 노드를 삽입하고 old를 new의 첫 번째 자식으로 연결한다.
 * old에 이전 형제(prev)가 있으면 해당 연결을 new로 이전한다.
 * make_tree()에서 연산자 노드를 기존 트리에 삽입할 때 호출된다.
 */
node *insert_node(node *old, node *new)
{
    if (old->prev != NULL) {
        new->prev = old->prev;      /* old의 이전 형제를 new의 이전 형제로 연결 */
        old->prev->next = new;      /* 이전 형제의 next도 new로 갱신 */
        old->prev = NULL;           /* old는 이제 new의 자식이 되므로 prev 끊기 */
    }
    new->child_head = old;          /* old를 new의 첫 번째 자식으로 설정 */
    old->parent = new;              /* old의 부모를 new로 설정 */
    return new;
}

/*
 * get_last_child():
 * child_head에서 시작하여 next를 따라 마지막 자식 노드를 반환한다.
 * make_tree()에서 교환 법칙 연산자의 피연산자를 이어 붙일 때 사용된다.
 */
node *get_last_child(node *cur)
{
    if (cur->child_head != NULL) cur = cur->child_head;
    while (cur->next != NULL) cur = cur->next; /* 마지막 형제까지 이동 */
    return cur;
}

/*
 * get_sibling_cnt():
 * 형제 노드의 수(next 방향 연결 수)를 반환한다.
 * compare_tree()에서 두 AST의 피연산자 개수가 같은지 확인할 때 사용된다.
 * prev를 따라 첫 번째 형제로 이동한 뒤 next를 따라 카운트한다.
 */
int get_sibling_cnt(node *cur)
{
    int i = 0;
    while (cur->prev != NULL) cur = cur->prev; /* 첫 번째 형제로 이동 */
    while (cur->next != NULL) { cur = cur->next; i++; } /* next 개수 카운트 */
    return i;
}

/*
 * free_node():
 * AST를 후위 순회(child 먼저 → next → 자신 순서)로 재귀 해제한다.
 * create_node()에서 malloc한 name 문자열과 노드 구조체를 모두 free한다.
 * 포인터 필드를 NULL로 초기화한 뒤 free하는 이유:
 * 재귀 호출 중 해제된 노드의 포인터가 실수로 역참조되는 상황을 방지하기 위함이다.
 */
void free_node(node *cur)
{
    if (cur->child_head != NULL) free_node(cur->child_head); /* 자식 먼저 해제 */
    if (cur->next       != NULL) free_node(cur->next);       /* 형제 해제 */
    cur->prev = cur->next = NULL;           /* dangling pointer 방지 */
    cur->parent = cur->child_head = NULL;
    free(cur->name);  /* malloc으로 할당한 name 문자열 해제 */
    free(cur);        /* 노드 구조체 자체 해제 */
}

/*
 * is_character():
 * 주어진 문자가 알파벳(대소문자) 또는 숫자인지 확인한다.
 * make_tokens()에서 식별자 구성 문자와 연산자를 구분하는 데 사용된다.
 * 예: '*'가 포인터인지 곱셈인지 구분할 때 이전 토큰의 마지막 문자가
 * is_character()이면 곱셈, 아니면 포인터로 판단한다.
 */
int is_character(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/*
 * is_typeStatement():
 * 문자열이 타입 선언문인지 판별한다.
 * datatype 배열을 순회하여 문자열 앞부분이 타입명과 일치하면 2, 일반 표현식이면 1, 오류면 0 반환.
 * remove_space()로 공백을 제거한 str2와 원본 start 포인터를 비교하여 오탐을 방지한다.
 */
int is_typeStatement(char *str)
{
    char *start;
    char  str2[BUFLEN] = {0};
    char  tmp[BUFLEN]  = {0};
    char  tmp2[BUFLEN] = {0};
    int   i;

    start = str;
    strncpy(str2, str, strlen(str));
    /* remove_space(): 공백을 모두 제거하여 비교 시 공백 차이를 무시한다 */
    remove_space(str2);
    while (start[0] == ' ') start += 1;

    if (strstr(str2, "gcc") != NULL) {
        strncpy(tmp2, start, strlen("gcc"));
        return (!strcmp(tmp2, "gcc")) ? 2 : 0;
    }
    for (i = 0; i < DATATYPE_SIZE; i++) {
        if (strstr(str2, datatype[i]) != NULL) {
            strncpy(tmp,  str2,  strlen(datatype[i]));
            strncpy(tmp2, start, strlen(datatype[i]));
            if (strcmp(tmp, datatype[i]) == 0)
                return (strcmp(tmp, tmp2) != 0) ? 0 : 2;
        }
    }
    return 1;
}

/* find_typeSpecifier(): (타입)변수 형태의 형변환 패턴을 탐색하여 시작 인덱스를 반환한다 */
int find_typeSpecifier(char tokens[TOKEN_CNT][MINLEN])
{
    int i, j;
    for (i = 0; i < TOKEN_CNT; i++)
        for (j = 0; j < DATATYPE_SIZE; j++)
            if (strstr(tokens[i], datatype[j]) != NULL && i > 0)
                if (!strcmp(tokens[i - 1], "(") && !strcmp(tokens[i + 1], ")") &&
                    (tokens[i + 2][0] == '&' || tokens[i + 2][0] == '*' ||
                     tokens[i + 2][0] == ')' || tokens[i + 2][0] == '(' ||
                     tokens[i + 2][0] == '-' || tokens[i + 2][0] == '+' ||
                     is_character(tokens[i + 2][0])))
                    return i;
    return -1;
}

/* find_typeSpecifier2(): struct 뒤에 식별자가 오는 패턴을 탐색한다 */
int find_typeSpecifier2(char tokens[TOKEN_CNT][MINLEN])
{
    int i, j;
    for (i = 0; i < TOKEN_CNT; i++)
        for (j = 0; j < DATATYPE_SIZE; j++)
            if (!strcmp(tokens[i], "struct") && (i + 1) <= TOKEN_CNT &&
                is_character(tokens[i + 1][strlen(tokens[i + 1]) - 1]))
                return i;
    return -1;
}

/* all_star(): 문자열이 '*'로만 구성되었는지 확인. 포인터 선언 판별에 사용한다 */
int all_star(char *str)
{
    int i, length = strlen(str);
    if (length == 0) return 0;
    for (i = 0; i < length; i++) if (str[i] != '*') return 0;
    return 1;
}

int all_character(char *str)
{
    int i;
    for (i = 0; i < (int)strlen(str); i++) if (is_character(str[i])) return 1;
    return 0;
}

/*
 * reset_tokens():
 * 형변환 패턴(예: (char)a, (unsigned int)b)을 정규화한다.
 * 기존: strcpy(tokens[i], tokens[i+N])에서 N=0이면 src==dst -> overlapping UB.
 * 개선: memmove()로 교체. memmove는 src/dst가 겹쳐도 안전하게 동작한다.
 */
int reset_tokens(int start, char tokens[TOKEN_CNT][MINLEN])
{
    int i;
    int j          = start - 1;
    int lcount     = 0, rcount     = 0;
    int sub_lcount = 0, sub_rcount = 0;

    if (start > -1) {
        if (!strcmp(tokens[start], "struct")) {
            strcat(tokens[start], " ");
            strcat(tokens[start], tokens[start + 1]);
            /* memmove(): 메모리가 겹치는 경우에도 안전하게 복사하는 함수 */
            for (i = start + 1; i < TOKEN_CNT - 1; i++) {
                memmove(tokens[i], tokens[i + 1], MINLEN);
                memset(tokens[i + 1], 0, MINLEN);
            }
        }
        else if (!strcmp(tokens[start], "unsigned") && strcmp(tokens[start + 1], ")") != 0) {
            strcat(tokens[start], " ");
            strcat(tokens[start], tokens[start + 1]);
            strcat(tokens[start], tokens[start + 2]);
            for (i = start + 1; i < TOKEN_CNT - 1; i++) {
                memmove(tokens[i], tokens[i + 1], MINLEN);
                memset(tokens[i + 1], 0, MINLEN);
            }
        }

        j = start + 1;
        while (!strcmp(tokens[j], ")")) { rcount++; if (j == TOKEN_CNT) break; j++; }

        j = start - 1;
        while (!strcmp(tokens[j], "(")) { lcount++; if (j == 0) break; j--; }
        if ((j != 0 && is_character(tokens[j][strlen(tokens[j]) - 1])) || j == 0)
            lcount = rcount;

        if (lcount != rcount) return false;
        if ((start - lcount) > 0 && !strcmp(tokens[start - lcount - 1], "sizeof"))
            return true;
        else if ((!strcmp(tokens[start], "unsigned") || !strcmp(tokens[start], "struct")) &&
                 strcmp(tokens[start + 1], ")")) {
            strcat(tokens[start - lcount], tokens[start]);
            strcat(tokens[start - lcount], tokens[start + 1]);
            strcpy(tokens[start - lcount + 1], tokens[start + rcount]);
            for (i = start - lcount + 1; i < TOKEN_CNT - lcount - rcount; i++) {
                memmove(tokens[i], tokens[i + lcount + rcount], MINLEN);
                memset(tokens[i + lcount + rcount], 0, MINLEN);
            }
        }
        else {
            if (tokens[start + 2][0] == '(') {
                j = start + 2;
                while (!strcmp(tokens[j], "(")) { sub_lcount++; j++; }
                if (!strcmp(tokens[j + 1], ")")) {
                    j = j + 1;
                    while (!strcmp(tokens[j], ")")) { sub_rcount++; j++; }
                }
                else return false;
                if (sub_lcount != sub_rcount) return false;
                memmove(tokens[start + 2], tokens[start + 2 + sub_lcount], MINLEN);
                for (i = start + 3; i < TOKEN_CNT; i++) memset(tokens[i], 0, MINLEN);
            }
            strcat(tokens[start - lcount], tokens[start]);
            strcat(tokens[start - lcount], tokens[start + 1]);
            strcat(tokens[start - lcount], tokens[start + rcount + 1]);
            for (i = start - lcount + 1; i < TOKEN_CNT - lcount - rcount - 1; i++) {
                memmove(tokens[i], tokens[i + lcount + rcount + 1], MINLEN);
                memset(tokens[i + lcount + rcount + 1], 0, MINLEN);
            }
        }
    }
    return true;
}

/* clear_tokens(): memset으로 토큰 배열 전체를 0으로 초기화한다 */
void clear_tokens(char tokens[TOKEN_CNT][MINLEN])
{
    int i;
    for (i = 0; i < TOKEN_CNT; i++) memset(tokens[i], 0, sizeof(tokens[i]));
}

/*
 * rtrim():
 * 기존 구현은 로컬 char tmp[BUFLEN]을 수정한 후 그 주소를 반환했다.
 * 함수 반환 시 스택 프레임이 해제되므로 반환된 포인터는 즉시 dangling이 된다.
 * gdb에서 score_blank->sprintf 경로의 segfault 원인으로 확인됨.
 * in-place 방식으로 변경: 원본 문자열에 '\0'을 삽입하여 직접 수정한다.
 * `end != _str` 비교는 서로 다른 배열의 포인터 비교로 항상 true -> UB.
 * `end >= str` 조건으로 교체하여 배열 하단 이탈을 방지한다.
 * isspace()에 (unsigned char) 캐스팅: 음수 char 입력 시 UB 방지.
 */
char *rtrim(char *str)
{
    char *end;
    if (str == NULL || *str == '\0') return str;
    end = str + strlen(str) - 1;
    /* 오른쪽에서부터 공백이 아닌 문자를 탐색 */
    while (end >= str && isspace((unsigned char)*end)) end--;
    /* 유효 문자 다음에 '\0' 삽입 */
    *(end + 1) = '\0';
    return str;
}

/* ltrim(): 포인터를 앞으로 이동하여 좌측 공백을 제거한다. in-place가 아니므로
   반환된 포인터로만 결과에 접근해야 한다(원본 포인터는 변경되지 않음) */
char *ltrim(char *str)
{
    char *start = str;
    while (*start != '\0' && isspace((unsigned char)*start)) ++start;
    return start;
}

/*
 * remove_extraspace():
 * 기존 구현은 malloc(BUFLEN)으로 버퍼를 할당하고 반환했다.
 * 호출자(make_tokens 내부)에서 free를 호출하지 않아 매 호출마다 1024바이트 누수 발생.
 * 학생 100명 * 문제당 수십 호출 = 수백KB 단위의 누적 누수였다.
 * 호출자가 out 버퍼를 직접 제공하는 방식으로 변경하여 malloc을 완전히 제거했다.
 */
void remove_extraspace(char *str, char *out)
{
    int   i;
    char  temp[BUFLEN] = "";
    char *start, *end;
    int   position;

    /* "include<" 형태를 "include <"로 정규화. strpbrk()로 '<'의 위치를 찾는다 */
    if (strstr(str, "include<") != NULL) {
        start    = str;
        end      = strpbrk(str, "<");
        position = end - start;
        strncat(temp, str, position);
        strncat(temp, " ", 1);
        strncat(temp, str + position, strlen(str) - position + 1);
        str = temp;
    }

    out[0] = '\0';
    for (i = 0; i < (int)strlen(str); i++) {
        if (str[i] == ' ') {
            if (i == 0 && str[0] == ' ') {
                while (str[i + 1] == ' ') i++;
            }
            else {
                if (i > 0 && str[i - 1] != ' ')
                    out[strlen(out)] = str[i];
                while (str[i + 1] == ' ') i++;
            }
        }
        else {
            out[strlen(out)] = str[i];
        }
    }
    out[strlen(out)] = '\0';
}

/* remove_space():
 * 문자열에서 모든 공백을 제거하여 in-place로 압축한다.
 * is_typeStatement()에서 타입명을 비교하기 전에 공백을 무시하고 싶을 때 사용된다.
 * i 포인터는 쓰기 위치, j 포인터는 읽기 위치. 공백이 아닌 문자만 i에 기록하고 전진한다.
 */
void remove_space(char *str)
{
    char *i = str, *j = str;
    while (*j != 0) { *i = *j++; if (*i != ' ') i++; }
    *i = 0; /* 압축 완료 후 널 종단 */
}

/*
 * check_brackets():
 * 여는 괄호와 닫는 괄호 수가 일치하면 1, 다르면 0을 반환한다.
 * score_blank()에서 학생 답안의 괄호 오류를 파싱 전에 걸러내는 데 사용된다.
 * 괄호 짝이 맞지 않으면 make_tokens()와 make_tree()에서 오동작이 발생할 수 있으므로
 * 사전에 차단하는 것이 안전하다.
 * strpbrk()로 "()" 중 하나가 나타나는 위치를 순차 탐색하며 카운트한다.
 */
int check_brackets(char *str)
{
    char *start = str;
    int   lcount = 0, rcount = 0;
    while (1) {
        /* strpbrk(): "()" 중 하나라도 포함된 첫 번째 문자 위치를 반환. 없으면 NULL */
        if ((start = strpbrk(start, "()")) != NULL) {
            if (*start == '(') lcount++; else rcount++;
            start += 1; /* 현재 괄호 다음 위치부터 이어서 탐색 */
        }
        else break;
    }
    return (lcount == rcount) ? 1 : 0;
}

/*
 * get_token_cnt():
 * 토큰 배열에서 비어있지 않은 토큰의 수를 반환한다.
 * strcmp로 빈 문자열("")을 만나는 시점이 유효 토큰의 끝이다.
 * 주로 토큰 배열의 실제 사용 범위를 확인하는 유틸리티 함수로 쓰인다.
 */
int get_token_cnt(char tokens[TOKEN_CNT][MINLEN])
{
    int i;
    for (i = 0; i < TOKEN_CNT; i++) if (!strcmp(tokens[i], "")) break;
    return i;
}
