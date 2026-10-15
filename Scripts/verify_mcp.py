"""Check configured MCP initialization/tools and optionally read live app state."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import tomllib
from contextlib import asynccontextmanager

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.client.streamable_http import streamable_http_client

ROOT = Path(__file__).resolve().parents[1]


@asynccontextmanager
async def connect(config):
    if 'url' in config:
        async with streamable_http_client(config['url']) as (reader, writer, _):
            yield reader, writer
        return
    params = StdioServerParameters(
        command=config['command'], args=config.get('args', []),
        cwd=config.get('cwd'), env={**os.environ, **config.get('env', {})},
    )
    async with stdio_client(params) as (reader, writer):
        yield reader, writer


async def verify(name, config, live):
    async with connect(config) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            tools = (await session.list_tools()).tools
            print(json.dumps({'server': name, 'tools': len(tools)}, ensure_ascii=False), flush=True)
            if live:
                if name == 'unreal':
                    toolset = 'editor_toolset.toolsets.scene.SceneTools'
                    for tool, arguments in [
                        ('list_toolsets', {}),
                        ('describe_toolset', {'toolset_name': toolset}),
                        ('call_tool', {'toolset_name': toolset, 'tool_name': 'get_current_level', 'arguments': {}}),
                    ]:
                        result = await session.call_tool(tool, arguments)
                        if result.isError:
                            raise RuntimeError(f'Unreal {tool} failed: {result}')
                    print(json.dumps({'server': name, 'live': result.model_dump()}, ensure_ascii=False), flush=True)
                    return
                names = {'unreal': 'get_scene_info', 'blender': 'get_scene_info',
                         'photoshop': 'photoshop_get_state'}
                arguments = {'user_prompt': 'Read-only installation connection check'} if name == 'blender' else {}
                if name == 'photoshop':
                    ping = await session.call_tool('photoshop_ping', {})
                    print(json.dumps({'server': name, 'ping': ping.model_dump()}, ensure_ascii=False), flush=True)
                    if ping.isError:
                        raise RuntimeError('Photoshop connection failed')
                result = await session.call_tool(names[name], arguments)
                print(json.dumps({'server': name, 'live': result.model_dump()}, ensure_ascii=False), flush=True)
                if result.isError or any(getattr(c, 'text', '').startswith('Error') for c in result.content):
                    raise RuntimeError(f'{name} live check failed')


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--server', choices=['unreal', 'blender', 'photoshop'])
    args = parser.parse_args()
    config = tomllib.loads((ROOT / '.codex/config.toml').read_text(encoding='utf-8-sig'))
    for name in ['unreal', 'blender', 'photoshop']:
        if args.server and args.server != name:
            continue
        await asyncio.wait_for(verify(name, config['mcp_servers'][name], args.live), timeout=90)


if __name__ == '__main__':
    asyncio.run(main())
