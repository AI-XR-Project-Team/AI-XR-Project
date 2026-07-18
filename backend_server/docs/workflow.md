# 명세(Spec) 전략 — Git Flow 연동

Cowork에서 구상·명세를 만들고, Claude Code가 명세를 읽어 구현하는 워크플로우를 Git Flow에 맞춘 규칙.

## 핵심 원칙

- **브랜치 1개 = 명세 1개 = PR 1개**
- **명세 파일명 = 브랜치 topic** → 추적성 확보
  - `feature/listen-server-lobby` ↔ `docs/specs/feature-listen-server-lobby.md`
  - `fix/replication-desync` ↔ `docs/specs/fix-replication-desync.md`
- 명세는 **코드와 같은 브랜치에 커밋**되어 함께 리뷰·머지된다(문서와 코드가 같이 이동).

## 폴더 구조

```
docs/
  workflow.md              # 이 문서
  specs/                   # 실제 기능/수정 명세 (브랜치별)
    feature-<topic>.md
    fix-<topic>.md
  templates/
    feature-spec.md        # 기능 명세 템플릿
    fix-spec.md            # 버그 수정 명세 템플릿
```

## 브랜치 유형별 명세 강도

| 브랜치 | 명세 | 이유 |
|--------|------|------|
| `feature/<topic>` | 전체 명세 필수 (`templates/feature-spec.md`) | 새 기능은 인터페이스·완료조건을 미리 못 박아야 구현이 정확 |
| `fix/<topic>` | 경량 명세 (`templates/fix-spec.md`) | 재현·원인·수정·테스트만. 산문 최소화 |
| `hotfix/<topic>` | 명세 생략, PR 설명으로 대체 | 속도 우선. 사후에 원인/재발방지만 기록 |
| `release/<version>` | 변경 요약(체인지로그) | 머지된 명세들을 모아 릴리스 노트로 |

## 라이프사이클 (Git Flow에 매핑)

1. **[Cowork] 구상·명세 작성** — 템플릿 기반으로 `docs/specs/<type>-<topic>.md` 초안 작성. 상태 `Draft`.
2. **[Git] 브랜치 생성** — `develop`에서 `feature/<topic>` 분기. 명세 파일을 이 브랜치에 커밋.
   - 커밋: `docs(spec): add <type>-<topic> spec`
3. **[검토] 명세 확정** — 팀/본인 확인 후 상태 `Approved`.
4. **[Claude Code] 구현** — 같은 브랜치에서 명세를 읽고 plan → 구현 → 테스트. 상태 `Implemented`.
   - 커밋: `feat(server): ...`, `test(server): ...` 등 Conventional Commits
5. **[PR] 리뷰** — 코드 + 명세가 한 diff에 포함. 리뷰어는 코드가 명세의 **완료 조건(acceptance criteria)** 을 만족하는지 확인. 서버 코드는 1인 이상 리뷰 필수.
6. **[머지] develop 반영** — 머지 시 명세 상태 `Merged`. 명세는 삭제하지 않고 살아있는 문서로 유지.
7. **[release] 취합** — `release/<version>` 만들 때 머지된 명세들로 체인지로그 작성 → `main` 반영 후 `develop` 동기화.

## 명세 상태 값

`Draft` → `Approved` → `Implemented` → `Merged`

각 명세 상단 메타에 현재 상태를 표기한다.

## 피드백 루프

Claude Code 구현 중 명세와 현실이 어긋나면(라이브러리 제약, 설계 변경 등) **명세를 먼저 갱신**하고 커밋(`docs(spec): update ...`)한 뒤 구현을 잇는다. 명세가 항상 최신이어야 다음 기능이 정확하다.
