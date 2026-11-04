# Notes index

Search the repo's research notes (`docs/`, the mods' READMEs and notes) by meaning and by exact strings, from the
Claude desktop app or the command line. Local, free, no API needed.

- **Vectors:** `all-MiniLM-L6-v2` (through `fastembed`, on the CPU). Notes are split by heading into chunks of about
  200 tokens, each embedded with its file and heading so a chunk that only says `+0x1E` keeps its subject.
- **Keywords:** SQLite FTS5 (BM25) over the same chunks, so `0x37C5F90`, `StratagemInfo` and `add_damage` find their
  note exactly.
- **Merged** by reciprocal rank fusion; a query with an identifier in it weights the keyword side higher.
- **Kept current:** each search re-indexes only the files whose content changed (a hash check), so edits and pulls
  show up without a rebuild.
- **Optional:** `rerank=True` has Claude Haiku re-order the results (needs `ANTHROPIC_API_KEY`).

Only markdown is indexed. Never put a game dump (`*.dump`) or disassembly listings in the indexed folders.

## Setup (Windows)

```
cd tools\notes-index
python -m pip install -r requirements.txt
python notes_index.py search "how do arcs link to their damage"
```

The first run downloads MiniLM (about 90 MB) into `tools/notes-index/.index/models` and indexes the notes (about 20
seconds); after that, a search in the app takes well under a second once the server has loaded the model. The index lives in `.index/` (ignored by git); delete the
folder to rebuild from scratch.

## In the Claude desktop app

Add the server to the desktop app's MCP config file, `claude_desktop_config.json`, with your own paths:

```json
{
  "mcpServers": {
    "hellforge-notes": {
      "command": "C:\\Path\\To\\python.exe",
      "args": ["C:\\Path\\To\\hellforge-mods\\tools\\notes-index\\server.py"]
    }
  }
}
```

Restart the app. Claude then has two tools:

- `search_notes(query, limit=8, rerank=False)`: the best chunks with their file and heading.
- `reindex_notes()`: re-index now and say what changed (searches already do this).

For `rerank`, add `"env": {"ANTHROPIC_API_KEY": "..."}` to the server entry. `NOTES_RERANK_MODEL` picks the model
(default `claude-haiku-5-5`).

## Settings (environment variables)

| Variable | Default | |
|---|---|---|
| `NOTES_REPO` | this repo | The folder whose notes are indexed. |
| `NOTES_DB` | `tools/notes-index/.index/notes.sqlite` | Where the index is kept. |
| `NOTES_RERANK_MODEL` | `claude-haiku-5-5` | The model `rerank` uses. |

What gets indexed is `GLOBS` at the top of `notes_index.py`.
