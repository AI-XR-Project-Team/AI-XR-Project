# 앱 아이콘 원본

`generate_android_icons.ps1` 이 여기 있는 마스터에서 해상도별 전경을 만든다.

| 파일 | 설명 |
|---|---|
| `icon_foreground_master.png` | 적응형 아이콘 전경 마스터. 정사각 PNG. |

`TimeMachineAR/Asset/` 은 `.gitignore` 대상이라 거기 두면 팀원이 재생성할 수
없다. 그래서 스크립트 옆에 둔다.

## 현재 상태

임시로 도슨트 아바타(`T_Lexi_Face`)를 쓰고 있다. 256px 라 xxxhdpi(432px)에서
확대되므로, 전용 아트가 준비되면 1024px 정사각으로 교체한다.
교체 절차는 [핸드오프 보드](../../docs/ai-handoff/BOARD.md) REQ-001 참고.

```
pwsh tools/generate_android_icons.ps1 -Source tools/icon_source/icon_foreground_master.png -Inset 0.8
```
