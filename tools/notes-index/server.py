"""MCP server for the notes index: search_notes(query) over the repo's research notes.

Runs locally over stdio, started by the Claude desktop app (see README.md). Each search first re-indexes files that
changed since the last one (a hash check, then MiniLM only for those files), so results follow the notes as you edit or
pull them. With ANTHROPIC_API_KEY set, rerank=True has Claude Haiku order the results and drop irrelevant ones.
"""
from __future__ import annotations

try:  # mcp 2.x
    from mcp.server.mcpserver import MCPServer as Server
except ImportError:  # mcp 1.x
    from mcp.server.fastmcp import FastMCP as Server

import notes_index

mcp = Server("hellforge-notes")
_db = None


def db():
    global _db
    if _db is None:
        _db = notes_index.connect()
    return _db


@mcp.tool()
def search_notes(query: str, limit: int = 8, rerank: bool = False) -> str:
    """Search the HellForge mods repo's research notes (docs/, mod READMEs) by meaning and by exact strings.

    Works for questions ("how do arcs link to their damage?") and identifiers (0x37C5F90, StratagemInfo, add_damage).
    Returns the best chunks with their file and heading, so the full note can be opened if needed.

    Args:
        query: a question, topic, address, struct or function name.
        limit: how many chunks to return (1-20).
        rerank: have Claude Haiku re-order the results (needs ANTHROPIC_API_KEY; costs a small API call).
    """
    limit = max(1, min(int(limit), 20))
    notes_index.index(db())
    results = notes_index.search(query, k=limit * 2 if rerank else limit, db=db())
    if rerank:
        results = notes_index.rerank(query, results, limit)
    if not results:
        return "No notes matched."
    return "\n\n---\n\n".join(
        f"## {r['path']} > {r['heading']}\n(score {r['score']}, similarity {r['similarity']}"
        f"{', keyword match' if r['keyword_hit'] else ''})\n\n{r['text']}"
        for r in results[:limit]
    )


@mcp.tool()
def reindex_notes() -> str:
    """Re-index the notes now and report what changed (searches already do this on their own)."""
    stats = notes_index.index(db())
    return (f"{stats['files']} files, {stats['changed']} changed, {stats['removed']} removed; "
            f"{stats['new_chunks']} chunks embedded, {stats['chunks']} in the index.")


if __name__ == "__main__":
    mcp.run()
