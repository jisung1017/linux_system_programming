/*
 * ssu_clean.c
 *
 * Project #2의 최상위 프롬프트 프로그램이다. 명세에서 요구한
 * "20222824> " 프롬프트를 출력하고, 사용자가 입력한 내장 명령어 중
 * fmd5와 help는 별도 프로세스로 실행한다. exit는 새 프로세스를 만들
 * 필요가 없으므로 현재 ssu_clean 프로세스에서 바로 처리한다.
 *
 * system 함수 사용이 금지되어 있으므로, 자식 프로세스 생성은 fork(),
 * 실제 프로그램 교체는 execv()로 수행한다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define STUDENT_ID "20222824"
#define MAX_INPUT 4096
#define MAX_ARGC 128

/*
 * 프롬프트에서 입력받은 한 줄을 공백, 탭, 개행 기준으로 나눈다.
 * 이 프로그램은 일반 셸이 아니라 과제 전용 프롬프트이므로 따옴표나 파이프
 * 같은 셸 문법은 해석하지 않고, fmd5/help/exit에 필요한 인자만 분리한다.
 * 반환값은 분리된 인자 개수이고, argv 끝에는 execv()에 넘길 수 있도록 NULL을 둔다.
 */
static int split_args(char *line, char *argv[])
{
	int argc = 0;
	char *token = strtok(line, " \t\n");

	/*
	 * MAX_ARGC - 1까지만 채우는 이유는 마지막 칸을 반드시 NULL로 남겨
	 * execv() 인자 배열 형식을 보장하기 위해서이다. 입력이 너무 길어도 버퍼를
	 * 넘지 않고, 넘친 인자는 자연스럽게 무시되어 잘못된 메모리 접근을 피한다.
	 */
	while (token != NULL && argc < MAX_ARGC - 1) {
		argv[argc++] = token;
		token = strtok(NULL, " \t\n");
	}
	argv[argc] = NULL;
	return argc;
}

/*
 * fmd5와 help 실행에 공통으로 쓰는 함수이다.
 * 명세에서 내장명령어 실행 시 fork() 후 exec() 계열 함수를 사용하라고 했기
 * 때문에, 부모는 waitpid()로 자식 종료를 기다리고 자식은 execv()로 실행 파일을
 * 교체한다. system 함수는 사용하지 않는다.
 */
static void run_child(char *const argv[])
{
	pid_t pid = fork();
	int status;

	/*
	 * fork 실패는 새 프로세스를 만들 수 없는 시스템 오류이므로 perror로 원인을
	 * 출력하고 프롬프트는 계속 살아 있게 한다. 내장 프롬프트 자체가 종료되면
	 * 이후 명령을 받을 수 없기 때문이다.
	 */
	if (pid < 0) {
		perror("fork");
		return;
	}
	if (pid == 0) {
		/*
		 * execv가 성공하면 아래 코드는 실행되지 않는다. 실패한 경우에만 오류를
		 * 출력하고 _exit로 자식 프로세스만 종료해 부모의 stdio 상태를 건드리지 않는다.
		 */
		execv(argv[0], argv);
		perror("exec");
		_exit(1);
	}
	/* 부모는 명세의 순차 프롬프트 동작을 맞추기 위해 자식 명령이 끝날 때까지 대기한다. */
	while (waitpid(pid, &status, 0) < 0)
		;
}

/*
 * 허용되지 않은 명령어가 들어오면 help를 실행한 것과 같은 결과를 보여야 하므로
 * help 실행을 별도 함수로 묶었다.
 */
static void run_help(void)
{
	char *args[] = {"./help", NULL};
	run_child(args);
}

int main(void)
{
	char input[MAX_INPUT];

	while (1) {
		char *argv[MAX_ARGC];
		int argc;

		/* 명세에서 요구한 프롬프트 형식: "학번> " */
		printf(STUDENT_ID "> ");
		fflush(stdout);

		if (fgets(input, sizeof(input), stdin) == NULL)
			break;
		if (input[0] == '\n')
			continue;

		argc = split_args(input, argv);
		if (argc == 0)
			continue;

		/*
		 * exit는 별도 실행 파일이 없고 ssu_clean 자체를 종료하는 명령어이다.
		 * 예시 출력과 동일하게 "Prompt End"를 출력하고 루프를 종료한다.
		 */
		if (strcmp(argv[0], "exit") == 0) {
			printf("Prompt End\n");
			break;
		}
		else if (strcmp(argv[0], "help") == 0) {
			run_help();
		}
		else if (strcmp(argv[0], "fmd5") == 0) {
			char *child_argv[MAX_ARGC];
			int i;

			/*
			 * 프롬프트에서는 fmd5라고 입력하지만 실제 실행 파일은 현재
			 * 디렉토리의 ./fmd5이다. 나머지 인자는 그대로 넘겨 fmd5.c에서
			 * 명세의 옵션 규칙에 맞게 검사하도록 한다.
			 */
			child_argv[0] = "./fmd5";
			for (i = 1; i < argc; i++)
				child_argv[i] = argv[i];
			child_argv[argc] = NULL;
			run_child(child_argv);
			}
			else {
				/*
				 * 명세에 없는 명령어는 별도의 에러 문자열을 만들지 않고 help를
				 * 보여 주는 방식으로 처리한다. 이 경로가 잘못된 입력에 대한
				 * 공통 예외 처리 역할을 한다.
				 */
				run_help();
			}
	}

	return 0;
}
