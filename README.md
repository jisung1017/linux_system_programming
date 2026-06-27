# Linux Systems Programming — Assignments

3-1 **리눅스 시스템 프로그래밍** 수업 과제 모음입니다.

POSIX 시스템 콜(`fork`, `exec`, `pipe`, `dup2`, `read`, `write` 등)을 직접 사용하여 구현했으며, `system()` 함수 및 표준 라이브러리 고수준 함수 사용이 제한된 환경에서 작성했습니다.

---

## Assignment 1 — `ssu_score`: 자동 채점 프로그램

> 학생 제출물을 `fork` 기반 병렬 처리로 자동 채점하고 결과를 CSV로 출력하는 프로그램

### 주요 구현 내용

- `fork()` / `waitpid()` 기반 **병렬 채점** (학생별 자식 프로세스)
- `execvp()` + `dup2()` 를 이용한 **출력 리다이렉션** (쉘 없이 직접 실행)
- 빈칸 문제(`.txt`) / 프로그래밍 문제(`.c`) 자동 구분 및 채점
- `gcc` 컴파일 후 **경고·에러 수 기반 감점** 처리
- 정답 컴파일·실행 결과 **캐시**(COW 공유)로 중복 작업 방지
- `getopt()` 옵션 파싱 (`-t`, `-c`, `-h`)

### 빌드 및 실행

```bash
cd assignment1
make
./ssu_score [options] <answer_dir> <student_dir>
```

| 옵션 | 설명 |
|------|------|
| `-t <file>` | pthread 병렬 처리 대상 문제 지정 |
| `-c <id>` | 특정 학번 학생만 채점 |
| `-h` | 사용법 출력 |

---

## Assignment 2 — `ssu_clean`: 중복 파일 삭제 프로그램

> 시스템 내 존재하는 동일(내용이 동일)한 파일을 찾고 삭제하는 프로그램

### 주요 구현 내용

- `fork()` / `execv()` 기반 명령어 실행 (`system()` 미사용)
- **내장 명령어**: `fmd5`, `help`, `exit`
- `fmd5`: 지정 파일의 **MD5 해시** 계산 및 출력
- `help`: 사용 가능한 명령어 목록 출력
- 공백·탭 기준 인자 분리 파서 직접 구현

### 빌드 및 실행

```bash
cd assignment2
make
./ssu_clean
```

실행하면 다음 프롬프트가 출력된다.

```
20222824>
```

사용 가능한 명령어: `fmd5`, `help`, `exit`

---

### 사용자 매뉴얼

#### fmd5 — 중복 파일 탐색 (크기/확장자 기준)

```
fmd5 -s [FILE_EXTENSION] [MINSIZE] [MAXSIZE] [TARGET_DIRECTORY]
```

| 인자 | 설명 |
|------|------|
| `FILE_EXTENSION` | `*` 또는 `*.확장자` |
| `MINSIZE` / `MAXSIZE` | 최소·최대 파일 크기 (`~` 입력 시 제한 없음) |
| `TARGET_DIRECTORY` | 탐색할 디렉토리 경로 |

```bash
fmd5 -s * ~ ~ ~/test          # 모든 파일, 크기 제한 없음
fmd5 -s *.txt 1KB 10MB ~/test # .txt 파일, 1KB~10MB 범위
```

---

#### 시간 조건 옵션

```
fmd5 -a [ATIME_FROM] [ATIME_TO] [TARGET_DIRECTORY]   # 접근 시간
fmd5 -m [MTIME_FROM] [MTIME_TO] [TARGET_DIRECTORY]   # 수정 시간
fmd5 -c [CTIME_FROM] [CTIME_TO] [TARGET_DIRECTORY]   # 상태 변경 시간
```

지원 시간 형식: `YYYY` / `YYYY:MM` / `YYYY:MM:DD` / `YYYY:MM:DD:HH`  
`~`는 범위 제한 없음을 의미한다.

---

#### 추가 탐색 옵션

| 옵션 | 설명 |
|------|------|
| `-sh` | 하드 링크 파일 제외 (inode 공유 파일 탐색 결과에서 제외) |
| `-d [DEPTH]` | BFS 탐색 깊이 제한 |
| `-n [COUNT]` | 출력할 중복 세트 개수 제한 |
| `-e [DIR]` | 특정 디렉토리 제외 (최대 3개) |
| `-p [MODE]` | 특정 권한의 파일만 탐색 |
| `-x [COUNT]` | 최소 중복 파일 개수 제한 |
| `-o [FILE]` | 결과를 파일로 저장 |
| `-r [MODE]` | 결과 정렬 기준 선택 |

**정렬 모드 (`-r`):**

| 모드 | 설명 |
|------|------|
| `size_desc` / `size_asc` | 파일 크기 내림차순 / 오름차순 |
| `mtime_desc` / `mtime_asc` | 수정 시간 내림차순 / 오름차순 |
| `path_desc` / `path_asc` | 대표 경로 내림차순 / 사전순 |

---

#### 삭제 단계 명령 (`>>` 프롬프트)

중복 세트 출력 후 `>>` 프롬프트에서 아래 명령을 사용한다.

```
[SET_INDEX] d [LIST_IDX]    # 해당 세트에서 특정 파일 하나 삭제
[SET_INDEX] i               # 파일마다 삭제 여부 개별 질문
[SET_INDEX] f [-u POLICY]   # 보존 1개 제외 나머지 삭제
[SET_INDEX] t [-u POLICY]   # 보존 1개 제외 나머지 휴지통 이동
exit                        # 삭제 단계 종료, 상위 프롬프트로 복귀
help                        # 사용법 출력
```

**보존 정책 (`-u`):**

| 옵션 | 설명 |
|------|------|
| `newest` | 가장 최근 수정 파일 보존 |
| `oldest` | 가장 오래된 수정 파일 보존 |
| `pathshort` | 절대 경로가 가장 짧은 파일 보존 |
| `pathlong` | 절대 경로가 가장 긴 파일 보존 |

```bash
1 f -u newest      # 세트 1에서 최신 파일 보존 후 나머지 삭제
2 t -u pathshort   # 세트 2에서 경로 짧은 파일 보존 후 나머지 휴지통 이동
```
---

## 환경

| 항목 | 내용 |
|------|------|
| OS | Linux (Ubuntu 22.04) |
| 컴파일러 | gcc 11+ |
| 표준 | POSIX.1-2017 |
| 빌드 | GNU Make |

## 핵심 기술

`fork` · `exec` · `waitpid` · `pipe` · `dup2` · `read` · `write` · `lseek` · `getopt` · `MD5`
