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

```
20222824> help
20222824> fmd5 <filename>
20222824> exit
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
