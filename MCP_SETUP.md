# 프로젝트 MCP 설치

2026-09-28 설치·검증. Codex 설정은 프로젝트 `.codex/config.toml`에 있다.

## Unreal: UE 5.8 공식 내장 MCP

[Epic 공식 문서](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)에 따라 엔진 내장 `ModelContextProtocol`(표시 이름 Unreal MCP)과 `AllToolsets`를 `msc_vr.uproject`에서 Editor 대상으로 활성화했다. `ToolsetRegistry`는 의존성으로 활성화된다. 아직 Experimental 기능이다.

- 서버: `http://127.0.0.1:8000/mcp` (Streamable HTTP).
- 자동 시작: `Config/DefaultEditorPerProjectUserSettings.ini`의 `[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]`.
- `bAutoStartServer=true`, `ServerPortNumber=8000`, `ServerUrlPath=/mcp`, `bEnableToolSearch=true`.
- Python/uv 브리지 없이 Codex가 에디터에 직접 연결한다.
- 이전 kvick-games/UnrealMCP 플러그인, 소스·빌드 캐시, ZIP 및 기존 13377 포트 설정은 삭제했다. 엔진 소스는 수정하지 않았다.

사용 순서:

1. 이 프로젝트를 Unreal Editor에서 연다. 이미 열려 있으면 저장 후 재시작한다.
2. Codex에서 MCP 서버를 재연결하거나 앱을 재시작한다. 이 프로젝트를 신뢰한 상태로 연다.
3. `list_toolsets` → `describe_toolset` → `call_tool` 순서로 도구를 찾고 호출한다. 에디터 도구 호출은 순차 실행한다.

자동 연결이 안 되면 에디터 콘솔에서 `ModelContextProtocol.StartServer`를 실행한다. 설정 위치는 Editor Preferences → General → Model Context Protocol이다. 8000 포트를 다른 앱이나 다른 Unreal 프로젝트가 사용하면 충돌할 수 있다. 이 경우 에디터 설정과 Codex URL을 함께 변경한다.

검증 결과:

- 설치된 UE 5.8의 내장 플러그인 바이너리 및 설정 클래스 확인.
- 별도 빈 맵 인스턴스에서 별도의 강제 시작 플래그 없이 서버 자동 시작 확인.
- MCP 초기화 및 3개 검색용 메타 도구 확인, 52개 도구 모음 발견.
- `editor_toolset.toolsets.scene.SceneTools` 스키마 조회 및 `get_current_level` 호출로 `/Engine/Maps/Entry` 반환 확인.
- 외부 플러그인 삭제 후 `msc_vrEditor Win64 Development` 빌드 성공.
- 검증 인스턴스는 종료했다. 전체 편집 기능을 모두 검증한 것은 아니다.

로그: `Saved/official-mcp-verification.txt`, `Saved/official-mcp-build.log`, `Saved/Logs/Official-MCP-check.log`.
Python MCP 검사기는 세션 종료 시 서버의 HTTP 202 응답에 대한 경고를 출력할 수 있다. 초기화·도구 호출은 성공했다.

### 2026-10-15 연결 복구 확인

프로젝트에서 `ModelContextProtocol`·`AllToolsets` 활성화와 자동 시작 설정이 빠져 있어8000 포트 연결이 거절되는 상태를 확인했다. 위 프로젝트·설정 파일을 복구하고 Editor 빌드를 통과한 뒤 서버의 `list_toolsets`·`describe_toolset`·`call_tool`을 실제 호출했다. 현재 등록된 공식 도구로 액터/컴포넌트 조회·속성/변환 수정·에셋 저장·맵 재로드·뷰포트 캡처까지 확인했다. 대상별 수정·검증 범위는 [충전기 모델 안내](CHARGER_MODEL.md)와 [창고 안내](WAREHOUSE.md)를 따른다.

이번 연결에서는 Codex 세션의 네이티브 도구 목록에 Unreal 서버가 노출되지 않아, 프로젝트에 이미 설치된 MCP SDK로 같은 HTTP 서버를 호출했다. UI 자동화나 콘솔 입력으로 맵을 수정하지 않았다. `SceneTools.save_actor`는 외부 액터 패키지를 에셋으로 찾지 못하는 오류가 있어, 저장에는 공식 `AssetTools.save_assets`의 빈 경로 목록으로 변경 패키지를 저장하는 기능을 사용했다.

## Blender / Photoshop

| 구성 | 버전 / 커밋 | 위치 |
| --- | --- | --- |
| [mcp-for-blender](https://github.com/ahujasid/mcp-for-blender) | 2.1.1 / `41a1843`, 애드온 1.7 / protocol 11 | `Tools/MCP/mcp-for-blender/` |
| [photoshop-mcp](https://github.com/alisaitteke/photoshop-mcp) | 1.7.21 / `2d1a3b1` | `Tools/MCP/photoshop-mcp/` |
| Python / MCP SDK | 3.11.16 / 1.30.0 | `Tools/MCP/python-env/`, 본체 `Tools/MCP/uv-python/` |
| uv | 0.12.19, 기존 WinGet 설치 | `C:/Users/ht515/AppData/Local/Microsoft/WinGet/Packages/astral-sh.uv_Microsoft.Winget.Source_8wekyb3d8bbwe/uv.exe` |
| Node | v24.19.0, Codex 실행 환경 | `C:/Users/ht515/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe` |
| npm | 기존 시스템 설치 | `C:/Program Files/nodejs/npm.cmd` |
| Blender | Steam 3.6.23 | `C:/Program Files (x86)/Steam/steamapps/common/Blender/blender.exe` |
| Blender 애드온 | 활성화·설정 저장 완료 | `%APPDATA%/Blender Foundation/Blender/3.6/scripts/addons/blender_mcp.py` |
| Photoshop | 2026 | `C:/Program Files/Adobe/Adobe Photoshop 2026/Photoshop.exe` |

Blender를 실행하면 MCP 애드온이 기본적으로 `127.0.0.1:9876`에서 자동 시작한다. 연결되지 않으면 N 패널 → MCP for Blender → Start MCP Server를 누른다. 기존 CATS 등 애드온은 유지했다. 실제 장면 조회 및 36개 MCP 도구 목록 확인을 완료했다.

Photoshop을 실행하면 Windows COM으로 연결한다. 기본 도구는 별도 Photoshop 애드온이 필요 없다. 125개 도구 목록과 `photoshop_ping`, `photoshop_get_state`를 검증했다. Neural Filters용 선택적 UXP 브리지는 설치·로드하지 않았다. 해당 기능이 필요하면 Adobe UXP Developer Tools에서 `Tools/MCP/photoshop-mcp/uxp-plugin/manifest.json`을 추가하고 Load한다.

Blender와 Photoshop의 MCP 프로세스는 Codex가 실행한다. uv로 Python 환경과 Blender 패키지를 설치했고, Photoshop은 저장소의 잠금 파일로 `npm ci --ignore-scripts --no-audit --no-fund` 후 TypeScript 서버를 빌드했다. 별도 웹 채팅 UI는 설치하지 않았다. Blender·Photoshop telemetry는 프로젝트 환경변수에서 껐고 외부 생성 서비스 API 키는 설정하지 않았다.

`Tools/MCP/`의 의존성·런타임은 Git에서 제외한다. Blender·Photoshop 설정에는 이 PC의 절대 경로가 있으므로 프로젝트 이동 또는 Codex 런타임 경로 변경 시 수정해야 한다.

## 연결 검사

프로젝트 루트에서:

```powershell
codex mcp get unreal
# Unreal Editor 실행 후 공식 HTTP 연결과 읽기 전용 레벨 조회
& .\Tools\MCP\python-env\Scripts\python.exe .\Scripts\verify_mcp.py --server unreal --live
# 세 앱 실행 후 전체 연결 확인
& .\Tools\MCP\python-env\Scripts\python.exe .\Scripts\verify_mcp.py --live
```

프로젝트 설정 방식: [Codex 공식 MCP 문서](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
