/*
 * ssu_help.c
 *
 * ssu_clean 프롬프트의 help 내장명령어가 실행하는 프로그램이다.
 * ssu_clean.c에서 fork()/execv()로 ./help를 실행하면, 이 파일은 현재 구현된
 * fmd5 사용법과 삭제 단계 옵션, 추가 구현 옵션을 한 번에 출력한다.
 * 명세에서 "학생들이 구현한 내장 명령어는 모두 포함"하라고 했으므로 필수 옵션과
 * 추가 옵션(-d, -n, -e, -p, -u, -r, -o, -x)을 모두 안내한다.
 */
#include <stdio.h>

int main(void)
{
	/*
	 * help는 상태를 저장하지 않는 단순 출력 프로그램이다. ssu_clean의 help 명령,
	 * fmd5 인자 오류, 삭제 단계의 help 요청이 모두 같은 실행 파일을 호출하므로
	 * 사용 가능한 문법을 여기 한 곳에 모아 둔다.
	 */
	printf("Usage:\n");
	/* -s 계열은 확장자, 최소/최대 크기, 대상 디렉토리를 모두 요구하는 기본 탐색 형식이다. */
	printf("  > fmd5 -s [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY]\n");
	printf("  > fmd5 -sh [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY]\n");
	/* -s와 시간 옵션을 함께 쓰는 경우에는 크기 조건과 시간 조건을 모두 만족해야 한다. */
	printf("  > fmd5 -s [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY] -a [ATIME_FROM] [ATIME_TO]\n");
	printf("  > fmd5 -s [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY] -m [MTIME_FROM] [MTIME_TO]\n");
	printf("  > fmd5 -s [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY] -c [CTIME_FROM] [CTIME_TO]\n");
	printf("  > fmd5 -sh [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY] -a [ATIME_FROM] [ATIME_TO]\n");
	printf("  > fmd5 -sh [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY] -m [MTIME_FROM] [MTIME_TO]\n");
	printf("  > fmd5 -sh [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY] -c [CTIME_FROM] [CTIME_TO]\n");
	/* 시간 옵션 단독 형식은 크기 제한 없이 접근/수정/상태변경 시간 범위로 파일을 고른다. */
	printf("  > fmd5 -a [ATIME_FROM] [ATIME_TO] [TARGET_DIRECTORY]\n");
	printf("  > fmd5 -m [MTIME_FROM] [MTIME_TO] [TARGET_DIRECTORY]\n");
	printf("  > fmd5 -c [CTIME_FROM] [CTIME_TO] [TARGET_DIRECTORY]\n");
	/* 아래 옵션들은 필수 기능과 추가 구현 기능을 사용자가 한눈에 확인할 수 있도록 나열한다. */
	printf("     -h              : 하드 링크 파일의 경우 제외할 수 있는 옵션\n");
	printf("     -d [DEPTH]      : BFS 탐색 시 depth 제한\n");
	printf("     -n [COUNT]      : 파일 크기가 큰 중복 파일 세트부터 COUNT개만 출력\n");
	printf("     -e [DIR ...]    : BFS 탐색 시 특정 디렉토리는 제외 (제외 디렉토리는 최대 3개까지 지정 가능)\n");
	printf("     -p [MODE]       : 특정 권한이 있는 파일만 탐색 (ex: -p 644)\n");
	printf("     -r [MODE]       : 중복 세트 정렬 기준 선택 (역정렬 또는 정렬)\n");
	printf("                       e.g. -r size_asc, -r size_desc, -r mtime_asc, -r mtime_desc, -r path_asc, -r path_desc\n");
	printf("     -o [FILE]       : 결과를 파일로 저장\n");
	printf("                       e.g. fmd5 -s *.c 1KB ~ ~/test -o result.txt\n");
	printf("     -x [COUNT]      : 최소 중복 개수 제한 (COUNT >= 2)\n");
	printf("                       e.g. fmd5 -s * ~ ~ ~/test -x 3\n");
	/* 탐색 결과 출력 후 들어가는 삭제 프롬프트에서 허용되는 명령 형식이다. */
	printf("  >> [SET_INDEX] [OPTION ...]\n");
    printf("     [OPTION ...]\n");
    printf("     d [LIST_IDX]    : 선택한 세트에서 [LIST_IDX]에 해당하는 파일 삭제\n");
	printf("     i               : 중복 파일의 절대 경로를 하나씩 보여주고 삭제 여부 확인 후 삭제 또는 유지 (Y,y/N,n)\n");
	printf("     f               : 가장 최근에 수정된 파일을 제외한 나머지 중복 파일 삭제\n");
	printf("     t               : 가장 최근에 수정된 파일을 제외한 나머지 중복 파일을 휴지통으로 이동\n");
	printf("     -u [MODE]       : f/t 옵션과 함께 사용할 세트 내 우선 보존 규칙 지정\n");
	printf("                       e.g. -u newest, -u oldest, -u pathshort, -u pathlong\n");
	printf("  > help\n");
	printf("  > exit\n\n");
	return 0;
}
