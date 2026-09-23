# 작업 지침

작업을 할 때 아래 구조도를 확인해 수정이 필요한 부분만 확인한다. 작업물 전체를 보는 작업은 필요할 때만 진행하며 매 작업마다 하지 않는다.

구조도 위치: [`PROJECT_STRUCTURE.md`](PROJECT_STRUCTURE.md)

폴더, 파일의 역할, 주요 에셋 위치가 추가·이동·삭제되는 구조 변경 작업을 마치면 **즉시** 다음 명령을 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\update_structure_summary.ps1
```

명령은 구조도 안의 경로 목록만 갱신한다. 담당 영역이나 진입점이 바뀌었다면 `PROJECT_STRUCTURE.md`의 작업별 탐색 표도 직접 고친다. 액터 좌표, 에셋 내부 속성, 코드의 일반적인 세부 변경은 구조도에 기록하지 않는다.
