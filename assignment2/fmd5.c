/*
 * fmd5.c
 *
 * Project #2의 중복 파일 탐색/삭제 명령어 구현이다. 명세의 fmd5 내장명령어
 * 역할에 맞춰 지정 디렉토리를 BFS로 순회하고, 조건에 맞는 정규 파일을
 * 링크드 리스트에 저장한 뒤 파일 크기와 MD5 해시값이 같은 파일들을 중복
 * 세트로 묶어 출력한다.
 *
 * 필수 옵션인 -s, -a, -m, -c, -h와 삭제 단계의 d/i/f/t를 처리하며,
 * 추가 구현 옵션인 -d, -n, -e, -p와 추가 점수 옵션 -u, -r, -o, -x도
 * 기존 구조 안에서 처리한다. MD5 계산은 명세 권장대로 OpenSSL EVP 계열
 * 인터페이스를 사용한다.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/evp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define HASH_HEX_LEN 32
#define TIME_STR_LEN 32
#define MAX_EXCLUDES 3
#define TRASH_DIR ".ssu_clean_trash"

typedef enum {
	TIME_NONE,
	TIME_ATIME,
	TIME_MTIME,
	TIME_CTIME
} TimeKind;

/* 결과 세트 정렬 기준이다. 기본 정렬은 명세의 크기 오름차순/경로 길이/해시 순서이고,
 * 나머지는 추가 점수 옵션 -r 또는 -n에서 사용한다. */
typedef enum {
	SORT_DEFAULT,
	SORT_SIZE_ASC,
	SORT_SIZE_DESC,
	SORT_MTIME_ASC,
	SORT_MTIME_DESC,
	SORT_PATH_ASC,
	SORT_PATH_DESC
} SortMode;

/* 삭제 단계에서 -u 옵션을 줄 때 어떤 파일을 남길지 표현한다. */
typedef enum {
	KEEP_NEWEST,
	KEEP_OLDEST,
	KEEP_PATHSHORT,
	KEEP_PATHLONG
} KeepMode;

/*
 * 탐색 조건을 통과한 실제 파일 하나를 저장하는 노드이다.
 * 명세에서 탐색 결과를 링크드 리스트로 저장해야 하므로 동적 배열 대신 next 포인터를
 * 사용한다. 경로, 크기, MD5, 시간 정보는 출력과 중복 판정에 쓰이고, dev/ino/nlink
 * 관련 정보는 하드링크 제외 판단 및 보고서 설명에 필요한 stat 정보를 보존하기 위해
 * 저장한다.
 */
typedef struct FileNode {
	char path[PATH_MAX];
	off_t size;
	char hash[HASH_HEX_LEN + 1];
	time_t atime;
	time_t mtime;
	time_t ctime;
	dev_t dev;
	ino_t ino;
	mode_t mode;
	struct FileNode *next;
} FileNode;

/*
 * 같은 크기와 같은 MD5 해시를 가진 파일들의 묶음이다.
 * 하나의 DupSet이 PDF 예시의 "Identical files #N" 한 세트에 대응한다.
 * files 역시 링크드 리스트이며, count가 2 이상일 때만 실제 중복 세트로 출력한다.
 */
typedef struct DupSet {
	off_t size;
	char hash[HASH_HEX_LEN + 1];
	FileNode *files;
	int count;
	struct DupSet *next;
} DupSet;

/*
 * BFS 디렉토리 탐색용 큐 노드이다.
 * depth는 추가 옵션 -d [DEPTH]가 들어왔을 때 더 깊은 하위 디렉토리로 내려갈지
 * 판단하기 위해 저장한다.
 */
typedef struct QueueNode {
	char path[PATH_MAX];
	int depth;
	struct QueueNode *next;
} QueueNode;

/*
 * fmd5 실행 한 번에 적용되는 모든 검색 조건과 추가 옵션을 모아 둔 구조체이다.
 * parse_args()에서 명령행 인자를 검증하며 채우고, scan_bfs()/file_match() 등은
 * 이 구조체만 보고 파일을 포함할지 결정한다.
 */
typedef struct Config {
	char ext[NAME_MAX + 4];
	off_t min_size;
	off_t max_size;
	int use_size;
	char target[PATH_MAX];
	int exclude_hardlinks;
	TimeKind time_kind;
	time_t time_from;
	time_t time_to;
	int depth_limit;
	int use_depth_limit;
	int set_limit;
	int use_set_limit;
	char excludes[MAX_EXCLUDES][PATH_MAX];
	int exclude_count;
	int use_perm;
	mode_t perm;
	int use_sort;
	SortMode sort_mode;
	int use_output;
	char output_path[PATH_MAX];
	int use_min_dup_count;
	int min_dup_count;
} Config;

static void free_files(FileNode *node);
static void free_sets(DupSet *set);

/* >> 입력 단계에서도 최상위 프롬프트와 같은 help 설명서를 보여준다. */
static void run_help(void)
{
	char *args[] = {"./help", NULL};
	pid_t pid = fork();
	int status;

	if (pid < 0) {
		perror("fork");
		return;
	}
	if (pid == 0) {
		execv(args[0], args);
		perror("exec");
		_exit(1);
	}
	while (waitpid(pid, &status, 0) < 0)
		;
}

/* 양의 정수 문자열인지 검사한다. 옵션 인자 중 COUNT, DEPTH, LIST_IDX처럼
 * 정수만 허용되는 값의 에러 처리를 한곳에서 하기 위해 사용한다. */
static int is_number_string(const char *s)
{
	if (*s == '\0')
		return 0;
	while (*s) {
		if (!isdigit((unsigned char)*s))
			return 0;
		s++;
	}
	return 1;
}

/* 추가 점수 옵션 -r의 인자를 내부 정렬 모드로 바꾼다.
 * size/mtime/path 기준별 오름차순과 내림차순을 모두 지원한다. */
static int parse_sort_mode(const char *s, SortMode *mode)
{
	if (strcmp(s, "size_asc") == 0)
		*mode = SORT_SIZE_ASC;
	else if (strcmp(s, "size_desc") == 0)
		*mode = SORT_SIZE_DESC;
	else if (strcmp(s, "mtime_asc") == 0)
		*mode = SORT_MTIME_ASC;
	else if (strcmp(s, "mtime_desc") == 0)
		*mode = SORT_MTIME_DESC;
	else if (strcmp(s, "path_asc") == 0)
		*mode = SORT_PATH_ASC;
	else if (strcmp(s, "path_desc") == 0)
		*mode = SORT_PATH_DESC;
	else
		return -1;
	return 0;
}

/* 삭제 단계에서 f/t와 함께 사용할 수 있는 -u 인자를 해석한다.
 * 잘못된 보존 기준이 들어오면 삭제를 진행하지 않도록 -1을 반환한다. */
static int parse_keep_mode(const char *s, KeepMode *mode)
{
	if (strcmp(s, "newest") == 0)
		*mode = KEEP_NEWEST;
	else if (strcmp(s, "oldest") == 0)
		*mode = KEEP_OLDEST;
	else if (strcmp(s, "pathshort") == 0)
		*mode = KEEP_PATHSHORT;
	else if (strcmp(s, "pathlong") == 0)
		*mode = KEEP_PATHLONG;
	else
		return -1;
	return 0;
}

/* -p [mode] 옵션은 644처럼 3자리 8진 권한만 받는다.
 * stat의 st_mode 하위 9비트와 비교할 수 있도록 mode_t 값으로 변환한다. */
static int parse_mode(const char *s, mode_t *mode)
{
	int value = 0;

	if (strlen(s) != 3)
		return -1;
	for (int i = 0; i < 3; i++) {
		if (s[i] < '0' || s[i] > '7')
			return -1;
		value = value * 8 + (s[i] - '0');
	}
	*mode = (mode_t)value;
	return 0;
}

/*
 * MINSIZE/MAXSIZE 인자를 바이트 단위 정수로 변환한다.
 * 명세에 따라 단위가 없으면 byte, KB/MB/GB는 대소문자 구분 없이 처리하고
 * 실수 입력도 허용한다. "~"는 최소값 위치에서는 0, 최대값 위치에서는 제한 없음으로
 * 해석한다.
 */
static int parse_size(const char *arg, off_t *result, int is_min)
{
	char *endptr;
	double value;
	double multiplier = 1.0;
	char unit[8];
	int i;

	if (strcmp(arg, "~") == 0) {
		*result = is_min ? 0 : LLONG_MAX;
		return 0;
	}

	errno = 0;
	value = strtod(arg, &endptr);
	if (errno != 0 || endptr == arg || value < 0)
		return -1;

	for (i = 0; endptr[i] && i < (int)sizeof(unit) - 1; i++)
		unit[i] = (char)tolower((unsigned char)endptr[i]);
	unit[i] = '\0';

	if (strcmp(unit, "") == 0 || strcmp(unit, "b") == 0)
		multiplier = 1.0;
	else if (strcmp(unit, "kb") == 0)
		multiplier = 1024.0;
	else if (strcmp(unit, "mb") == 0)
		multiplier = 1024.0 * 1024.0;
	else if (strcmp(unit, "gb") == 0)
		multiplier = 1024.0 * 1024.0 * 1024.0;
	else
		return -1;

	*result = (off_t)(value * multiplier);
	return 0;
}

static int valid_date(int year, int month, int day, int hour)
{
	struct tm tmv;
	time_t t;

	if (year < 1970 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23)
		return 0;

	memset(&tmv, 0, sizeof(tmv));
	tmv.tm_year = year - 1900;
	tmv.tm_mon = month - 1;
	tmv.tm_mday = day;
	tmv.tm_hour = hour;
	tmv.tm_isdst = -1;
	t = mktime(&tmv);
	if (t == (time_t)-1)
		return 0;

	return tmv.tm_year == year - 1900 && tmv.tm_mon == month - 1 &&
	       tmv.tm_mday == day && tmv.tm_hour == hour;
}

/* 월까지만 주어진 시간 범위에서 끝 날짜를 계산하기 위해 사용한다.
 * 윤년 2월까지 처리해야 잘못된 날짜를 걸러낼 수 있다. */
static int last_day_of_month(int year, int month)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	if (month == 2) {
		if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
			return 29;
		return 28;
	}
	return days[month - 1];
}

/*
 * -a/-m/-c 시간 범위의 FROM 또는 TO 값을 time_t로 변환한다.
 * 명세는 "년:월:일:시" 중 일부만 줄 수 있고 0은 범위의 시작/끝으로 해석할 수
 * 있다고 설명한다. 따라서 FROM이면 빠진 값을 해당 기간의 시작으로, TO이면 끝으로
 * 채운다. "~"는 시작 쪽에서는 1970년 이후 전체, 끝 쪽에서는 현재 시각까지로 본다.
 * 잘못된 연도(4자리 아님), 월, 일, 시는 모두 에러 처리한다.
 */
static int parse_time_bound(const char *arg, int is_from, time_t *out)
{
	char buf[128];
	char *parts[4] = {NULL, NULL, NULL, NULL};
	int count = 0;
	int year = 0, month = 0, day = 0, hour = 0;
	struct tm tmv;

	if (strcmp(arg, "~") == 0) {
		*out = is_from ? 0 : time(NULL);
		return 0;
	}

	if (strlen(arg) >= sizeof(buf))
		return -1;
	strcpy(buf, arg);

	parts[count++] = strtok(buf, ":");
	while (count < 4 && (parts[count] = strtok(NULL, ":")) != NULL)
		count++;
	if (strtok(NULL, ":") != NULL)
		return -1;

	for (int i = 0; i < count; i++) {
		if (parts[i] == NULL || !is_number_string(parts[i]))
			return -1;
	}
	if (strlen(parts[0]) != 4)
		return -1;

	year = atoi(parts[0]);
	if (count >= 2)
		month = atoi(parts[1]);
	if (count >= 3)
		day = atoi(parts[2]);
	if (count >= 4)
		hour = atoi(parts[3]);

	if (month == 0)
		month = is_from ? 1 : 12;
	if (day == 0)
		day = is_from ? 1 : last_day_of_month(year, month);
	if (count < 4 || hour == 0)
		hour = is_from ? 0 : 23;

	if (!valid_date(year, month, day, hour))
		return -1;

	memset(&tmv, 0, sizeof(tmv));
	tmv.tm_year = year - 1900;
	tmv.tm_mon = month - 1;
	tmv.tm_mday = day;
	tmv.tm_hour = hour;
	tmv.tm_min = is_from ? 0 : 59;
	tmv.tm_sec = is_from ? 0 : 59;
	tmv.tm_isdst = -1;
	*out = mktime(&tmv);
	return 0;
}

/*
 * TARGET_DIRECTORY와 -e [DIR]에 들어온 경로를 절대 경로로 바꾼다.
 * "~"로 시작하면 홈 디렉토리를 붙이고, 그 외 상대경로는 realpath()로 현재 작업
 * 디렉토리 기준 절대 경로로 만든다. 존재하지 않는 경로는 명세상 에러이므로 -1을 반환한다.
 */
static int expand_path(const char *input, char *output)
{
	if (input[0] == '~') {
		const char *home = getenv("HOME");
		char expanded[PATH_MAX];
		if (home == NULL) {
			struct passwd *pw = getpwuid(getuid());
			home = pw ? pw->pw_dir : NULL;
		}
		if (home == NULL)
			return -1;
		if (snprintf(expanded, sizeof(expanded), "%s%s", home, input + 1) >= (int)sizeof(expanded))
			return -1;
		if (realpath(expanded, output) == NULL)
			return -1;
	}
	else {
		if (realpath(input, output) == NULL)
			return -1;
	}

	return 0;
}

/*
 * -o OUTPUT_PATH는 새 파일을 만들 수 있어야 하므로 expand_path()처럼 파일 자체에
 * realpath()를 적용하면 안 된다. 대신 "~"와 상대경로를 절대경로로 바꾸되,
 * 부모 디렉토리만 존재 여부를 확인한다.
 */
static int expand_output_path(const char *input, char *output)
{
	char expanded[PATH_MAX];
	char parent[PATH_MAX];
	char real_parent[PATH_MAX];
	char *slash;
	const char *filename;

	if (input[0] == '~') {
		const char *home = getenv("HOME");
		if (home == NULL) {
			struct passwd *pw = getpwuid(getuid());
			home = pw ? pw->pw_dir : NULL;
		}
		if (home == NULL)
			return -1;
		if (snprintf(expanded, sizeof(expanded), "%s%s", home, input + 1) >= (int)sizeof(expanded))
			return -1;
	}
	else {
		if (strlen(input) >= sizeof(expanded))
			return -1;
		strcpy(expanded, input);
	}

	slash = strrchr(expanded, '/');
	if (slash == NULL) {
		if (realpath(".", real_parent) == NULL)
			return -1;
		filename = expanded;
	}
	else {
		filename = slash + 1;
		if (*filename == '\0')
			return -1;
		if (slash == expanded) {
			strcpy(parent, "/");
		}
		else {
			size_t len = (size_t)(slash - expanded);
			if (len >= sizeof(parent))
				return -1;
			memcpy(parent, expanded, len);
			parent[len] = '\0';
		}
		if (realpath(parent, real_parent) == NULL)
			return -1;
	}

	if (snprintf(output, PATH_MAX, "%s/%s", real_parent, filename) >= PATH_MAX)
		return -1;
	return 0;
}

/* FILE_EXTENSION 인자는 "*" 또는 "*.<확장자>"만 허용한다.
 * 이 규칙을 벗어난 입력은 fmd5 인자 오류로 처리된다. */
static int parse_extension(const char *arg, char *ext)
{
	if (strcmp(arg, "*") == 0) {
		strcpy(ext, "*");
		return 0;
	}
	if (strncmp(arg, "*.", 2) == 0 && strlen(arg) > 2) {
		strncpy(ext, arg + 2, NAME_MAX);
		ext[NAME_MAX] = '\0';
		return 0;
	}
	return -1;
}

/* 실제 파일 이름이 사용자가 지정한 확장자 조건에 맞는지 검사한다.
 * "*"는 모든 정규 파일을 대상으로 하며, 그 외에는 마지막 '.' 뒤 문자열을 비교한다. */
static int has_extension(const char *name, const char *ext)
{
	const char *dot;

	if (strcmp(ext, "*") == 0)
		return 1;
	dot = strrchr(name, '.');
	if (dot == NULL || dot[1] == '\0')
		return 0;
	return strcmp(dot + 1, ext) == 0;
}

/* BFS 구현을 위한 큐 삽입 함수이다. DFS 재귀 호출을 쓰지 않고 명세에서 요구한
 * BFS 순회가 되도록 디렉토리 경로와 깊이를 큐 뒤에 추가한다. */
static void enqueue(QueueNode **head, QueueNode **tail, const char *path, int depth)
{
	QueueNode *node = malloc(sizeof(QueueNode));
	if (node == NULL)
		return;
	strncpy(node->path, path, PATH_MAX - 1);
	node->path[PATH_MAX - 1] = '\0';
	node->depth = depth;
	node->next = NULL;
	if (*tail)
		(*tail)->next = node;
	else
		*head = node;
	*tail = node;
}

/* BFS 큐에서 다음 디렉토리를 꺼낸다. 큐가 비어 있으면 0을 반환한다. */
static int dequeue(QueueNode **head, QueueNode **tail, char *path, int *depth)
{
	QueueNode *node = *head;

	if (node == NULL)
		return 0;
	strcpy(path, node->path);
	*depth = node->depth;
	*head = node->next;
	if (*head == NULL)
		*tail = NULL;
	free(node);
	return 1;
}

/* 루트 디렉토리부터 탐색할 때 /proc, /run, /sys는 반드시 제외하라는 명세를
 * 만족하기 위한 검사이다. */
static int is_root_excluded_dir(const char *parent, const char *name)
{
	return strcmp(parent, "/") == 0 &&
	       (strcmp(name, "proc") == 0 || strcmp(name, "run") == 0 || strcmp(name, "sys") == 0);
}

/* 추가 옵션 -e로 받은 제외 디렉토리인지 검사한다.
 * 제외 경로 그 자체뿐 아니라 그 하위 경로도 탐색하지 않도록 prefix와 경로 구분자를
 * 함께 확인한다. */
static int is_user_excluded(const Config *cfg, const char *path)
{
	for (int i = 0; i < cfg->exclude_count; i++) {
		size_t len = strlen(cfg->excludes[i]);
		if (strncmp(path, cfg->excludes[i], len) == 0 &&
		    (path[len] == '\0' || path[len] == '/'))
			return 1;
	}
	return 0;
}

/*
 * 파일 내용을 읽어 MD5 해시를 32자리 16진수 문자열로 계산한다.
 * 명세에서 직접 MD5_* 함수보다 EVP 계열 사용을 권장하므로 EVP_DigestInit_ex,
 * EVP_DigestUpdate, EVP_DigestFinal_ex 흐름으로 구현했다.
 */
static int get_md5(const char *path, char hash[HASH_HEX_LEN + 1])
{
	EVP_MD_CTX *ctx;
	unsigned char buf[8192];
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0;
	int fd;
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	/*
	 * 파일 열기, EVP 컨텍스트 생성, 초기화, 읽기, 최종 해시 추출 중 하나라도 실패하면
	 * 해당 파일은 중복 후보에서 제외한다. 탐색 전체를 중단하지 않는 이유는 권한 문제나
	 * 일시적인 I/O 오류가 있는 파일 하나 때문에 나머지 정상 파일 검사를 포기하지 않기 위해서이다.
	 */
	ctx = EVP_MD_CTX_new();
	if (ctx == NULL) {
		close(fd);
		return -1;
	}
	if (EVP_DigestInit_ex(ctx, EVP_md5(), NULL) != 1) {
		EVP_MD_CTX_free(ctx);
		close(fd);
		return -1;
	}

	while ((n = read(fd, buf, sizeof(buf))) > 0)
		EVP_DigestUpdate(ctx, buf, (size_t)n);
	close(fd);
	if (n < 0) {
		EVP_MD_CTX_free(ctx);
		return -1;
	}

	if (EVP_DigestFinal_ex(ctx, md, &md_len) != 1) {
		EVP_MD_CTX_free(ctx);
		return -1;
	}
	EVP_MD_CTX_free(ctx);

	for (unsigned int i = 0; i < md_len; i++)
		sprintf(hash + i * 2, "%02x", md[i]);
	hash[HASH_HEX_LEN] = '\0';
	return 0;
}

/* -a/-m/-c 중 어떤 시간 조건이 설정되었는지에 따라 stat의 atime, mtime, ctime을
 * 선택해 범위 안에 있는지 검사한다. 시간 조건이 없으면 모든 파일을 통과시킨다. */
static int file_time_match(const Config *cfg, const struct stat *st)
{
	time_t value;

	if (cfg->time_kind == TIME_NONE)
		return 1;
	if (cfg->time_kind == TIME_ATIME)
		value = st->st_atime;
	else if (cfg->time_kind == TIME_MTIME)
		value = st->st_mtime;
	else
		value = st->st_ctime;
	return value >= cfg->time_from && value <= cfg->time_to;
}

/*
 * 정규 파일 여부와 확장자, 크기, 시간, 권한 조건을 한 번에 검사한다.
 * 명세는 정규 파일만 대상으로 하므로 디렉토리, 심볼릭 링크 등은 여기서 제외된다.
 * 크기 조건은 -s가 사용된 경우에만 적용하고, -a/-m/-c 단독 검색에서는 크기 제한을
 * 두지 않는다.
 */
static int file_match(const Config *cfg, const char *name, const struct stat *st)
{
	if (!S_ISREG(st->st_mode))
		return 0;
	if (!has_extension(name, cfg->ext))
		return 0;
	if (cfg->use_size && (st->st_size < cfg->min_size || st->st_size > cfg->max_size))
		return 0;
	if (!file_time_match(cfg, st))
		return 0;
	if (cfg->use_perm && ((st->st_mode & 0777) != cfg->perm))
		return 0;
	return 1;
}

/* 조건을 통과한 파일을 링크드 리스트 노드로 만든다.
 * 출력에 필요한 절대경로와 시간, 중복 판정에 필요한 크기와 해시를 함께 저장한다. */
static FileNode *new_file_node(const char *path, const struct stat *st, const char *hash)
{
	FileNode *node = malloc(sizeof(FileNode));
	if (node == NULL)
		return NULL;
	strncpy(node->path, path, PATH_MAX - 1);
	node->path[PATH_MAX - 1] = '\0';
	node->size = st->st_size;
	strcpy(node->hash, hash);
	node->atime = st->st_atime;
	node->mtime = st->st_mtime;
	node->ctime = st->st_ctime;
	node->dev = st->st_dev;
	node->ino = st->st_ino;
	node->mode = st->st_mode;
	node->next = NULL;
	return node;
}

/* 명세의 세트 내부 파일 정렬 기준에 맞춰 절대경로 길이가 짧은 파일을 먼저 두고,
 * 길이가 같으면 ASCII 문자열 순서로 비교한다. */
static int path_compare(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b);

	if (la != lb)
		return la < lb ? -1 : 1;
	return strcmp(a, b);
}

/* 파일 링크드 리스트에 정렬된 위치로 삽입한다.
 * 탐색 순서와 무관하게 출력 시 세트 내부 파일 순서가 명세와 맞도록 하기 위한 함수이다. */
static void insert_file_sorted(FileNode **head, FileNode *node)
{
	FileNode **cur = head;

	while (*cur && path_compare((*cur)->path, node->path) <= 0)
		cur = &(*cur)->next;
	node->next = *cur;
	*cur = node;
}

static void add_file_to_all(FileNode **all, FileNode *node)
{
	insert_file_sorted(all, node);
}

/*
 * TARGET_DIRECTORY 아래를 BFS로 탐색한다.
 * 큐에 디렉토리를 넣고 하나씩 꺼내며 하위 디렉토리를 다시 큐에 추가하므로, 재귀 DFS가
 * 아니라 명세에서 요구한 BFS 방식이다. 각 항목은 lstat()으로 확인하고, 디렉토리는
 * 큐에 넣고 정규 파일은 file_match() 필터를 거친 뒤 MD5를 계산해 all_files 링크드
 * 리스트에 저장한다. -h가 지정되면 st_nlink가 2 이상인 하드링크 파일은 탐색 결과에서
 * 제외한다.
 */
static void scan_bfs(const Config *cfg, FileNode **all_files)
{
	QueueNode *head = NULL, *tail = NULL;
	char dirpath[PATH_MAX];
	int depth;

	enqueue(&head, &tail, cfg->target, 0);
	while (dequeue(&head, &tail, dirpath, &depth)) {
		DIR *dir = opendir(dirpath);
		struct dirent *entry;

		if (dir == NULL)
			continue;

		/*
		 * opendir/lstat/realpath/get_md5 실패는 모두 continue로 처리한다. 명세의 목적은
		 * 접근 가능한 파일 중 중복을 찾는 것이므로, 권한이 없거나 사라진 항목은 건너뛰고
		 * BFS 탐색 자체는 계속 진행한다.
		 */
		while ((entry = readdir(dir)) != NULL) {
			char path[PATH_MAX];
			struct stat st;

			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
				continue;
			if (is_root_excluded_dir(dirpath, entry->d_name))
				continue;

			if (snprintf(path, sizeof(path), "%s/%s", strcmp(dirpath, "/") == 0 ? "" : dirpath, entry->d_name) >= (int)sizeof(path))
				continue;
			if (lstat(path, &st) < 0)
				continue;

			if (S_ISDIR(st.st_mode)) {
				char real[PATH_MAX];

				/*
				 * 디렉토리는 실제 절대경로로 변환한 뒤 제외 목록과 깊이 제한을 적용한다.
				 * 이렇게 해야 상대경로나 심볼릭 경로 차이 때문에 -e 제외 조건이 우회되지 않는다.
				 */
				if (realpath(path, real) == NULL)
					continue;
				if (is_user_excluded(cfg, real))
					continue;
				if (cfg->depth_limit < 0 || depth < cfg->depth_limit)
					enqueue(&head, &tail, real, depth + 1);
			}
			else if (file_match(cfg, entry->d_name, &st)) {
				char hash[HASH_HEX_LEN + 1];
				char real[PATH_MAX];
				FileNode *node;

				/*
				 * -h는 하드링크가 2개 이상인 파일을 중복 후보에서 제외하는 옵션이다.
				 * stat의 st_nlink 값으로 판단하므로 같은 inode를 공유하는 파일들이
				 * 해시 비교 단계에 들어가지 않는다.
				 */
				if (cfg->exclude_hardlinks) {
					if (st.st_nlink > 1)
						continue;
				}
				/*
				 * 해시 계산 전에 realpath를 다시 적용해 출력 경로를 절대경로로 통일한다.
				 * 명세 출력 예시는 절대경로 기반이므로, 저장되는 FileNode도 같은 기준을 따른다.
				 */
				if (realpath(path, real) == NULL)
					continue;
				if (get_md5(real, hash) < 0)
					continue;
				node = new_file_node(real, &st, hash);
				if (node)
					add_file_to_all(all_files, node);
			}
		}
		closedir(dir);
	}
}

/* 중복 세트 안의 파일 목록 역시 정렬된 링크드 리스트로 유지한다. */
static void append_file(FileNode **head, FileNode *file)
{
	file->next = NULL;
	insert_file_sorted(head, file);
}

/* 세트 정렬에서 경로 길이 비교에 사용할 대표 경로를 가져온다.
 * 세트 내부가 이미 정렬되어 있으므로 첫 번째 파일이 최단 경로 파일이다. */
static const char *first_set_path(const DupSet *set)
{
	return set->files ? set->files->path : "";
}

/* 추가 정렬 옵션 mtime_desc에서 한 세트의 가장 최근 수정 시간을 계산한다. */
static time_t latest_set_mtime(const DupSet *set)
{
	time_t latest = 0;

	if (set->files)
		latest = set->files->mtime;
	for (FileNode *file = set->files; file; file = file->next) {
		if (file->mtime > latest)
			latest = file->mtime;
	}
	return latest;
}

/*
 * PDF 명세의 기본 세트 정렬 기준이다.
 * 1) 파일 크기 오름차순, 2) 세트 내 최단 절대경로 길이 오름차순,
 * 3) 해시값 사전순으로 비교한다.
 */
static int compare_sets_default(const DupSet *a, const DupSet *b)
{
	const char *apath = first_set_path(a);
	const char *bpath = first_set_path(b);

	if (a->size != b->size)
		return a->size < b->size ? -1 : 1;
	if (strlen(apath) != strlen(bpath))
		return strlen(apath) < strlen(bpath) ? -1 : 1;
	return strcmp(a->hash, b->hash);
}

/* 기본 정렬에 더해 추가 옵션 -r의 정렬 기준을 적용한다.
 * 사용자가 지정한 기준으로 우선 비교하고, 동률이면 기본 정렬로 안정적인 순서를 만든다. */
static int compare_sets(const DupSet *a, const DupSet *b, SortMode mode)
{
	int cmp;

	if (mode == SORT_SIZE_ASC && a->size != b->size)
		return a->size < b->size ? -1 : 1;
	if (mode == SORT_SIZE_DESC && a->size != b->size)
		return a->size > b->size ? -1 : 1;
	if (mode == SORT_MTIME_ASC || mode == SORT_MTIME_DESC) {
		time_t atime = latest_set_mtime(a);
		time_t btime = latest_set_mtime(b);
		if (atime != btime) {
			if (mode == SORT_MTIME_ASC)
				return atime < btime ? -1 : 1;
			return atime > btime ? -1 : 1;
		}
	}
	if (mode == SORT_PATH_ASC || mode == SORT_PATH_DESC) {
		cmp = strcmp(first_set_path(a), first_set_path(b));
		if (cmp != 0) {
			if (mode == SORT_PATH_DESC)
				return -cmp;
			return cmp;
		}
	}
	return compare_sets_default(a, b);
}

/* DupSet 링크드 리스트를 정렬 상태로 유지하며 삽입한다. */
static void insert_set_sorted(DupSet **sets, DupSet *set, SortMode mode)
{
	DupSet **cur = sets;

	while (*cur) {
		if (compare_sets(*cur, set, mode) > 0)
			break;
		cur = &(*cur)->next;
	}
	set->next = *cur;
	*cur = set;
}

/*
 * 탐색된 전체 파일 리스트를 크기+MD5 기준으로 그룹화해 중복 세트를 만든다.
 * 명세상 중복 파일은 "크기가 같고 해시값이 같은 정규 파일"이므로 두 조건이 모두
 * 같은 파일만 같은 DupSet에 넣는다. 그룹의 파일 수가 min_dup_count보다 작으면
 * 출력 대상이 아니므로 메모리를 해제한다.
 */
static DupSet *build_sets(FileNode *all_files, int min_dup_count, SortMode sort_mode)
{
	DupSet *groups = NULL;
	FileNode *file = all_files;

	if (min_dup_count < 2)
		min_dup_count = 2;

	while (file) {
		FileNode *next = file->next;
		DupSet *set;

		file->next = NULL;
		for (set = groups; set; set = set->next) {
			if (set->size == file->size && strcmp(set->hash, file->hash) == 0)
				break;
		}
		if (set == NULL) {
			/*
			 * 새로운 크기+해시 조합이면 임시 그룹을 만든다. 메모리 할당 실패 시에는
			 * 해당 파일 노드만 해제하고 다음 파일 처리를 계속해 전체 프로그램 종료를 피한다.
			 */
			set = calloc(1, sizeof(DupSet));
			if (set == NULL) {
				free(file);
				file = next;
				continue;
			}
			set->size = file->size;
			strcpy(set->hash, file->hash);
			set->next = groups;
			groups = set;
		}
		append_file(&set->files, file);
		set->count++;
		file = next;
	}

	DupSet *duplicates = NULL;
	while (groups) {
		DupSet *next = groups->next;
		/*
		 * 실제 출력 대상은 count가 기준 이상인 그룹뿐이다. 기준 미만 그룹은 중복이
		 * 아니므로 파일 노드까지 해제해 삭제 단계에서 잘못 선택될 가능성을 없앤다.
		 */
		if (groups->count >= min_dup_count) {
			groups->next = NULL;
			insert_set_sorted(&duplicates, groups, sort_mode);
		}
		else {
			free_files(groups->files);
			free(groups);
		}
		groups = next;
	}
	return duplicates;
}

/* 출력 예시처럼 파일 크기에 1,000 단위 콤마를 넣는다. */
static void comma_size(off_t size, char *buf, size_t buflen)
{
	char tmp[64];
	int len, commas, out = 0, first;

	snprintf(tmp, sizeof(tmp), "%lld", (long long)size);
	len = (int)strlen(tmp);
	commas = (len - 1) / 3;
	first = len % 3;
	if (first == 0)
		first = 3;

	for (int i = 0; i < len && out < (int)buflen - 1; i++) {
		if (i != 0 && (i - first) % 3 == 0 && commas > 0)
			buf[out++] = ',';
		buf[out++] = tmp[i];
	}
	buf[out] = '\0';
}

/* mtime/atime 출력 형식을 PDF 예시의 YYYY-MM-DD HH:MM:SS 형태로 맞춘다. */
static void format_time_value(time_t t, char *buf, size_t buflen)
{
	struct tm tmv;

	localtime_r(&t, &tmv);
	strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tmv);
}

/* 현재 남아 있는 중복 세트 수를 센다. 삭제 후 루프 종료 여부 판단에 사용한다. */
static int set_count(DupSet *sets)
{
	int count = 0;
	while (sets) {
		count++;
		sets = sets->next;
	}
	return count;
}

/*
 * 중복 세트 전체를 PDF 예시 형식으로 출력한다.
 * 첫 줄은 세트 번호, 파일 크기, MD5 해시를 출력하고, 이어서 세트 내부 파일의
 * 인덱스, 절대경로, mtime, atime을 출력한다. limit은 -n 옵션에서 출력할 세트 수를
 * 제한하기 위해 사용한다.
 */
static void print_sets_to(FILE *out, DupSet *sets, int limit)
{
	int set_idx = 1;

	for (DupSet *set = sets; set; set = set->next) {
		char sizebuf[64];
		int list_idx = 1;

		if (limit > 0 && set_idx > limit)
			break;

		comma_size(set->size, sizebuf, sizeof(sizebuf));
		fprintf(out, "---- Identical files #%d (%s bytes - %s) ----\n", set_idx, sizebuf, set->hash);
		for (FileNode *file = set->files; file; file = file->next) {
			char mt[TIME_STR_LEN], at[TIME_STR_LEN];
			format_time_value(file->mtime, mt, sizeof(mt));
			format_time_value(file->atime, at, sizeof(at));
			fprintf(out, "[%d] %s (mtime : %s) (atime : %s)\n", list_idx++, file->path, mt, at);
		}
		set_idx++;
		fprintf(out, "\n");
	}
}

/* 표준 출력으로 중복 세트를 출력하는 래퍼 함수이다. */
static void print_sets(DupSet *sets, int limit)
{
	print_sets_to(stdout, sets, limit);
}

/* 사용자가 >> 단계에서 입력한 SET_INDEX에 해당하는 세트를 찾는다.
 * 출력 번호는 항상 현재 링크드 리스트 순서 기준으로 다시 매겨진다. */
static DupSet *get_set(DupSet *sets, int idx)
{
	int n = 1;
	while (sets) {
		if (n == idx)
			return sets;
		n++;
		sets = sets->next;
	}
	return NULL;
}

/* d 옵션 또는 i 옵션에서 특정 LIST_IDX 파일을 세트에서 떼어낸다.
 * 링크드 리스트 연결을 직접 갱신하고 count를 줄여 삭제 후 상태가 일관되게 유지되도록 한다. */
static FileNode *detach_file(DupSet *set, int idx)
{
	FileNode **cur = &set->files;
	int n = 1;

	while (*cur) {
		if (n == idx) {
			FileNode *target = *cur;
			*cur = target->next;
			target->next = NULL;
			set->count--;
			return target;
		}
		cur = &(*cur)->next;
		n++;
	}
	return NULL;
}

/* 삭제/이동 후 파일이 1개 이하로 남은 세트는 더 이상 중복 세트가 아니므로 제거한다.
 * 이렇게 해야 다음 출력에서 세트 번호가 1부터 다시 자연스럽게 부여된다. */
static void remove_small_sets(DupSet **sets)
{
	DupSet **cur = sets;

	while (*cur) {
		if ((*cur)->count < 2) {
			DupSet *target = *cur;
			*cur = target->next;
			free_files(target->files);
			free(target);
		}
		else {
			cur = &(*cur)->next;
		}
	}
}

/* f/t 옵션에서 남길 파일을 고른다.
 * 기본은 명세처럼 가장 최근 수정 파일이며, 추가 점수 옵션 -u가 들어온 경우 oldest,
 * pathshort, pathlong 기준으로 바꿀 수 있다. 동률이면 출력 순서가 안정되도록 경로
 * 기준으로 한 번 더 비교한다.
 */
static FileNode *choose_keep_file(DupSet *set, KeepMode mode)
{
	FileNode *best = set->files;

	for (FileNode *cur = set->files; cur; cur = cur->next) {
		size_t cur_len = strlen(cur->path);
		size_t best_len = strlen(best->path);

		if (mode == KEEP_NEWEST &&
		    (cur->mtime > best->mtime ||
		     (cur->mtime == best->mtime && path_compare(cur->path, best->path) < 0)))
			best = cur;
		else if (mode == KEEP_OLDEST &&
			 (cur->mtime < best->mtime ||
			  (cur->mtime == best->mtime && path_compare(cur->path, best->path) < 0)))
			best = cur;
		else if (mode == KEEP_PATHSHORT &&
			 (cur_len < best_len ||
			  (cur_len == best_len && strcmp(cur->path, best->path) < 0)))
			best = cur;
		else if (mode == KEEP_PATHLONG &&
			 (cur_len > best_len ||
			  (cur_len == best_len && strcmp(cur->path, best->path) < 0)))
			best = cur;
	}
	return best;
}

/* t 옵션에서 사용할 휴지통 디렉토리를 준비한다.
 * 명세가 "특정 디렉토리로 이동"을 허용하므로 홈 디렉토리 아래 .ssu_clean_trash를
 * 간단한 휴지통으로 사용한다. */
static int ensure_trash_dir(char *trash)
{
	const char *home = getenv("HOME");

	if (home == NULL)
		home = ".";
	snprintf(trash, PATH_MAX, "%s/%s", home, TRASH_DIR);
	if (mkdir(trash, 0755) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

/* 파일을 휴지통 디렉토리로 이동한다.
 * 같은 이름의 파일이 이미 있으면 .1, .2처럼 suffix를 붙여 덮어쓰지 않도록 한다. */
static int move_to_trash(const char *path)
{
	char trash[PATH_MAX], dest[PATH_MAX], base[NAME_MAX + 1];
	const char *slash = strrchr(path, '/');
	int suffix = 0;
	int written;

	if (ensure_trash_dir(trash) < 0)
		return -1;
	strncpy(base, slash ? slash + 1 : path, NAME_MAX);
	base[NAME_MAX] = '\0';

	do {
		if (suffix == 0)
			written = snprintf(dest, sizeof(dest), "%s/%s", trash, base);
		else
			written = snprintf(dest, sizeof(dest), "%s/%s.%d", trash, base, suffix);
		if (written < 0 || written >= (int)sizeof(dest))
			return -1;
		suffix++;
	} while (access(dest, F_OK) == 0);

	return rename(path, dest);
}

/* FileNode 링크드 리스트 전체를 해제한다. */
static void free_files(FileNode *node)
{
	while (node) {
		FileNode *next = node->next;
		free(node);
		node = next;
	}
}

/* DupSet 목록과 그 안의 FileNode 목록까지 모두 해제한다. */
static void free_sets(DupSet *set)
{
	while (set) {
		DupSet *next = set->next;
		free_files(set->files);
		free(set);
		set = next;
	}
}

/* f/t 옵션의 공통 삭제 로직이다.
 * keep 파일은 세트에 남기고 나머지는 삭제하거나 휴지통으로 이동한 뒤 링크드 리스트에서
 * 제거한다. */
static void delete_set_except(DupSet *set, FileNode *keep, int trash_mode)
{
	FileNode **cur = &set->files;

	while (*cur) {
		FileNode *file = *cur;
		if (file == keep) {
			cur = &file->next;
			continue;
		}
		if (trash_mode)
			move_to_trash(file->path);
		else
			remove(file->path);
		*cur = file->next;
		free(file);
		set->count--;
	}
}

/* f/t 옵션 뒤에 추가 점수 옵션 -u가 붙었는지 검사한다.
 * 옵션이 없으면 명세 기본값인 newest를 사용하고, 형식이 맞지 않으면 삭제를 수행하지 않는다. */
static int parse_delete_keep_option(int argc, char *argv[], KeepMode *mode)
{
	*mode = KEEP_NEWEST;
	if (argc == 2)
		return 0;
	if (argc == 4 && strcmp(argv[2], "-u") == 0)
		return parse_keep_mode(argv[3], mode);
	return -1;
}

/*
 * 중복 세트를 출력한 뒤의 ">> [SET_INDEX] [OPTION ...]" 입력 루프이다.
 * exit는 프롬프트로 돌아가고, d는 지정 파일 하나 삭제, i는 각 파일 삭제 여부 확인,
 * f는 최신 수정 파일만 남기고 삭제, t는 최신 수정 파일만 남기고 휴지통으로 이동한다.
 * 각 동작 후에는 링크드 리스트에서 삭제된 노드를 제거하고, 중복이 아니게 된 세트를
 * 정리한 뒤 남아 있는 세트를 다시 출력한다.
 */
static void deletion_loop(DupSet **sets)
{
	char input[1024];

	while (*sets && set_count(*sets) > 0) {
		char *argv[8];
		int argc = 0;
		char *token;
		int set_idx;
		int too_many_args = 0;
		DupSet *set;

		printf(">> ");
		fflush(stdout);
		if (fgets(input, sizeof(input), stdin) == NULL)
			break;
		if (input[0] == '\n')
			continue;

		token = strtok(input, " \t\n");
		while (token) {
			if (argc >= 8) {
				too_many_args = 1;
				break;
			}
			argv[argc++] = token;
			token = strtok(NULL, " \t\n");
		}
		if (too_many_args) {
			printf("ERROR: invalid command\n");
			continue;
		}
		if (argc == 1 && strcmp(argv[0], "exit") == 0) {
			printf(">> Back to Prompt\n");
			break;
		}
		if (argc == 1 && strcmp(argv[0], "help") == 0) {
			run_help();
			continue;
		}
		if (argc < 2 || !is_number_string(argv[0])) {
			printf("ERROR: invalid command\n");
			continue;
		}
		set_idx = atoi(argv[0]);
		set = get_set(*sets, set_idx);
		if (set == NULL) {
			printf("ERROR: invalid set index\n");
			continue;
		}

		/*
		 * 아래 분기는 삭제 단계 명세의 d/i/f/t 네 가지 동작을 그대로 대응한다.
		 * 각 분기는 인자 개수와 숫자 형식을 먼저 확인하고, 유효하지 않으면 파일 시스템을
		 * 건드리지 않은 채 에러 메시지를 출력한 뒤 다음 명령을 받는다.
		 */
		if (strcmp(argv[1], "d") == 0) {
			FileNode *target;
			if (argc != 3 || !is_number_string(argv[2])) {
				printf("ERROR: invalid list index\n");
				continue;
			}
			target = detach_file(set, atoi(argv[2]));
			if (target == NULL) {
				printf("ERROR: invalid list index\n");
				continue;
			}
			remove(target->path);
			printf("\"%s\" has been deleted in #%d\n", target->path, set_idx);
			free(target);
		}
		else if (strcmp(argv[1], "i") == 0) {
			FileNode *cur = set->files;
			int invalid_answer = 0;
			/*
			 * i 옵션은 파일마다 즉시 삭제 여부를 묻는다. 순회 중 현재 노드가
			 * 삭제될 수 있으므로 next를 먼저 저장해 링크드 리스트 변경 후에도
			 * 다음 파일로 안전하게 이동한다.
			 */
			while (cur) {
				FileNode *next = cur->next;
				char answer[32];
				printf("Delete \"%s\"? [y/n] ", cur->path);
				fflush(stdout);
				if (fgets(answer, sizeof(answer), stdin) == NULL)
					break;
				if (answer[0] == 'y' || answer[0] == 'Y') {
					FileNode *target;
					int idx = 1;
					for (FileNode *tmp = set->files; tmp && tmp != cur; tmp = tmp->next)
						idx++;
					target = detach_file(set, idx);
					if (target) {
						remove(target->path);
						free(target);
					}
				}
				else if (answer[0] != 'n' && answer[0] != 'N') {
					printf("ERROR: invalid answer\n");
					invalid_answer = 1;
					break;
				}
				cur = next;
			}
			if (invalid_answer) {
				remove_small_sets(sets);
				continue;
			}
		}
		else if (strcmp(argv[1], "f") == 0 || strcmp(argv[1], "t") == 0) {
			KeepMode keep_mode;
			FileNode *keep;
			char mt[TIME_STR_LEN];
			int trash_mode = strcmp(argv[1], "t") == 0;

			/*
			 * f와 t는 "하나만 남기고 나머지 처리"라는 구조가 같아서 공통 로직을
			 * 사용한다. -u 옵션 형식이 틀리면 삭제/이동 전에 중단해 의도하지 않은
			 * 대량 삭제를 막는다.
			 */
			if (parse_delete_keep_option(argc, argv, &keep_mode) < 0) {
				printf("ERROR: invalid option\n");
				continue;
			}
			keep = choose_keep_file(set, keep_mode);
			format_time_value(keep->mtime, mt, sizeof(mt));
			delete_set_except(set, keep, trash_mode);
			if (trash_mode)
				printf("All files in #%d have moved to Trash except \"%s\" (%s)\n", set_idx, keep->path, mt);
			else
				printf("Left file in #%d : %s (%s)\n", set_idx, keep->path, mt);
		}
		else {
			printf("ERROR: invalid option\n");
			continue;
		}

		/* 삭제 또는 이동 후 중복 파일이 2개 미만이 된 세트는 즉시 제거해 다음 출력과
		 * SET_INDEX 해석이 현재 상태와 일치하도록 유지한다. */
		remove_small_sets(sets);
		printf("\n");
		if (*sets) {
			print_sets(*sets, 0);
		}
	}
}

/*
 * fmd5 명령행 인자를 명세 규칙에 맞게 검증하고 Config로 변환한다.
 * -s는 확장자/최소크기/최대크기/대상 디렉토리를 필요로 하고, -a/-m/-c는 시간 범위와
 * 대상 디렉토리를 필요로 한다. -h는 단독 사용이 아니라 -s 또는 시간 옵션과 함께 쓰여야
 * 하며, a/m/c는 중복 사용할 수 없도록 막는다. 추가 옵션도 중복 사용이나 잘못된 인자를
 * 최대한 에러 처리한다.
 */
static int parse_args(Config *cfg, int argc, char *argv[])
{
	int has_target = 0;

	memset(cfg, 0, sizeof(*cfg));
	strcpy(cfg->ext, "*");
	cfg->min_size = 0;
	cfg->max_size = LLONG_MAX;
	cfg->depth_limit = -1;

	for (int i = 1; i < argc;) {
		int has_s = 0;
		char time_opt = '\0';

		if (argv[i][0] != '-')
			return -1;

		if (strcmp(argv[i], "-r") == 0) {
			/* -r, -o, -x, -d, -n, -e, -p는 독립 추가 옵션이므로 여기서 먼저 처리한다.
			 * 각 옵션은 중복 지정, 인자 누락, 잘못된 값이면 즉시 인자 오류로 반환한다. */
			if (cfg->use_sort || i + 1 >= argc)
				return -1;
			if (parse_sort_mode(argv[i + 1], &cfg->sort_mode) < 0)
				return -1;
			cfg->use_sort = 1;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "-o") == 0) {
			if (cfg->use_output || i + 1 >= argc || argv[i + 1][0] == '-')
				return -1;
			if (expand_output_path(argv[i + 1], cfg->output_path) < 0)
				return -1;
			cfg->use_output = 1;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "-x") == 0) {
			if (cfg->use_min_dup_count || i + 1 >= argc || !is_number_string(argv[i + 1]))
				return -1;
			cfg->min_dup_count = atoi(argv[i + 1]);
			if (cfg->min_dup_count < 2)
				return -1;
			cfg->use_min_dup_count = 1;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "-d") == 0) {
			if (cfg->use_depth_limit || i + 1 >= argc || !is_number_string(argv[i + 1]))
				return -1;
			cfg->depth_limit = atoi(argv[i + 1]);
			cfg->use_depth_limit = 1;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "-n") == 0) {
			if (cfg->use_set_limit || i + 1 >= argc || !is_number_string(argv[i + 1]))
				return -1;
			cfg->set_limit = atoi(argv[i + 1]);
			if (cfg->set_limit <= 0)
				return -1;
			cfg->use_set_limit = 1;
			i += 2;
			continue;
		}
		if (strcmp(argv[i], "-e") == 0) {
			int parsed = 0;

			/*
			 * -e는 최대 MAX_EXCLUDES개까지 연속된 디렉토리 인자를 받는다. 하나도
			 * 없으면 오류이고, 제한 개수를 넘긴 추가 경로가 남아 있으면 그 역시
			 * 잘못된 사용으로 처리한다.
			 */
			i++;
			while (i < argc && argv[i][0] != '-' && cfg->exclude_count < MAX_EXCLUDES) {
				if (expand_path(argv[i], cfg->excludes[cfg->exclude_count]) < 0)
					return -1;
				cfg->exclude_count++;
				parsed++;
				i++;
			}
			if (parsed == 0)
				return -1;
			if (i < argc && argv[i][0] != '-')
				return -1;
			continue;
		}
		if (strcmp(argv[i], "-p") == 0) {
			if (cfg->use_perm || i + 1 >= argc || parse_mode(argv[i + 1], &cfg->perm) < 0)
				return -1;
			cfg->use_perm = 1;
			i += 2;
			continue;
		}

		/*
		 * -s, -a, -m, -c, -h는 -sh처럼 한 묶음으로 들어올 수 있으므로 문자 단위로
		 * 검사한다. 시간 기준 옵션은 한 번에 하나만 허용하고, 알 수 없는 문자가 있으면
		 * help를 띄우도록 오류를 반환한다.
		 */
		for (int j = 1; argv[i][j]; j++) {
			char opt = argv[i][j];

			if (opt == 's') {
				if (has_s)
					return -1;
				has_s = 1;
			}
			else if (opt == 'a' || opt == 'm' || opt == 'c') {
				if (time_opt != '\0')
					return -1;
				time_opt = opt;
			}
			else if (opt == 'h') {
				if (cfg->exclude_hardlinks)
					return -1;
				cfg->exclude_hardlinks = 1;
			}
			else {
				return -1;
			}
		}

		if (has_s) {
			/*
			 * -s는 반드시 네 개의 뒤따르는 인자를 가진다. 확장자, 크기 범위, 대상
			 * 디렉토리를 모두 검증한 뒤에만 Config에 반영해 잘못된 일부 값으로 탐색이
			 * 시작되지 않게 한다.
			 */
			if (has_target)
				return -1;
			if (i + 4 >= argc)
				return -1;
			if (parse_extension(argv[i + 1], cfg->ext) < 0)
				return -1;
			if (parse_size(argv[i + 2], &cfg->min_size, 1) < 0 ||
			    parse_size(argv[i + 3], &cfg->max_size, 0) < 0 ||
			    cfg->min_size > cfg->max_size)
				return -1;
			if (expand_path(argv[i + 4], cfg->target) < 0)
				return -1;
			cfg->use_size = 1;
			has_target = 1;
			i += 5;
		}
		else {
			i++;
		}

		if (time_opt != '\0') {
			TimeKind kind = time_opt == 'a' ? TIME_ATIME : (time_opt == 'm' ? TIME_MTIME : TIME_CTIME);
			/*
			 * 시간 옵션은 FROM/TO 범위를 먼저 검증하고, -s가 없던 형식이라면 그 다음
			 * 인자를 TARGET_DIRECTORY로 해석한다. FROM이 TO보다 뒤이면 의미 없는 범위이므로
			 * 오류 처리한다.
			 */
			if (cfg->time_kind != TIME_NONE)
				return -1;
			if (i + 1 >= argc)
				return -1;
			if (parse_time_bound(argv[i], 1, &cfg->time_from) < 0 ||
			    parse_time_bound(argv[i + 1], 0, &cfg->time_to) < 0 ||
			    cfg->time_from > cfg->time_to)
				return -1;
			cfg->time_kind = kind;
			i += 2;
			if (!has_target) {
				if (i >= argc)
					return -1;
				if (expand_path(argv[i], cfg->target) < 0)
					return -1;
				cfg->use_size = 0;
				has_target = 1;
				i++;
			}
		}
	}

	if (!has_target)
		return -1;
	return 0;
}

int main(int argc, char *argv[])
{
	Config cfg;
	struct stat st;
	struct timeval begin, end;
	FileNode *all_files = NULL;
	DupSet *sets;
	SortMode sort_mode;
	long sec, usec;
	FILE *output = NULL;

	if (parse_args(&cfg, argc, argv) < 0 || stat(cfg.target, &st) < 0 || !S_ISDIR(st.st_mode)) {
		/*
		 * fmd5는 인자 오류, 존재하지 않는 대상, 디렉토리가 아닌 대상 모두를 help 출력으로
		 * 처리한다. 잘못된 조건으로 파일 삭제 단계까지 진입하지 않게 하는 가장 바깥 예외 처리이다.
		 */
		run_help();
		return 1;
	}

	/*
	 * 명세는 전체 프로그램 시간이 아니라 "주어진 옵션대로 진행한 탐색 시간"을
	 * 출력하라고 한다. 따라서 인자 파싱과 삭제 단계는 제외하고, BFS 탐색과
	 * 중복 세트 구성/정렬에 걸린 시간만 측정한다.
	 */
	gettimeofday(&begin, NULL);
	scan_bfs(&cfg, &all_files);
	sort_mode = cfg.use_sort ? cfg.sort_mode : (cfg.use_set_limit ? SORT_SIZE_DESC : SORT_DEFAULT);
	sets = build_sets(all_files, cfg.use_min_dup_count ? cfg.min_dup_count : 2, sort_mode);
	gettimeofday(&end, NULL);

	sec = end.tv_sec - begin.tv_sec;
	usec = end.tv_usec - begin.tv_usec;
	if (usec < 0) {
		sec--;
		usec += 1000000;
	}

	/* -o 옵션은 표준 출력과 같은 탐색 결과를 파일에도 저장한다. */
	if (cfg.use_output) {
		output = fopen(cfg.output_path, "w");
		if (output == NULL) {
			run_help();
			free_sets(sets);
			return 1;
		}
	}

	if (sets == NULL) {
		printf("No duplicates in %s\n", cfg.target);
		if (output)
			fprintf(output, "No duplicates in %s\n", cfg.target);
	}
	else {
		print_sets(sets, cfg.set_limit);
		if (output)
			print_sets_to(output, sets, cfg.set_limit);
	}

	printf("Searching time: %ld:%06ld(sec:usec)\n\n", sec, usec);
	if (output) {
		fprintf(output, "Searching time: %ld:%06ld(sec:usec)\n\n", sec, usec);
		fclose(output);
	}

	if (sets != NULL) {
		deletion_loop(&sets);
	}

	free_sets(sets);
	return 0;
}
