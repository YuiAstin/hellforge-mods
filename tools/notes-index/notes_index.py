"""Notes index: hybrid search over the repo's research notes (MiniLM vectors + SQLite FTS5 keywords).

Notes are split by heading into chunks of about 200 tokens (MiniLM reads 256 at most), each tagged with its file and
heading path. A search runs both the vector search (cosine over all-MiniLM-L6-v2 embeddings, brute force: the notes are
small) and a keyword search (FTS5, BM25), then merges the two rankings by reciprocal rank fusion, so an address like
0x37C5F90 and a question like "how do arcs link to damage" both find their note. Only files whose content changed are
re-embedded.

CLI:
    python notes_index.py index            re-index changed files
    python notes_index.py search "query"   search (indexes first)
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = Path(os.environ.get("NOTES_REPO", HERE.parent.parent)).resolve()
DB = Path(os.environ.get("NOTES_DB", HERE / ".index" / "notes.sqlite"))
MODEL = "sentence-transformers/all-MiniLM-L6-v2"
# What gets indexed, relative to the repo root. Never the game dump (*.dump): only markdown notes.
GLOBS = ["docs/**/*.md", "mods/*/README.md", "mods/*/*.md", "probes/README.md", "README.md", "CHANGELOG.md"]
CHUNK_WORDS = 150  # ~200 tokens; MiniLM truncates past 256
RRF_K = 60

_model = None


def model():
    global _model
    if _model is None:
        from fastembed import TextEmbedding

        # cached beside the index, not in the temp folder (Windows clears that)
        _model = TextEmbedding(MODEL, cache_dir=str(DB.parent / "models"))
    return _model


def embed(texts: list[str]) -> np.ndarray:
    vecs = np.array(list(model().embed(texts)), dtype=np.float32)
    norms = np.linalg.norm(vecs, axis=1, keepdims=True)
    return vecs / np.maximum(norms, 1e-9)


@dataclass
class Chunk:
    path: str
    heading: str
    text: str


def files() -> list[Path]:
    seen: dict[Path, None] = {}
    for g in GLOBS:
        for p in sorted(REPO.glob(g)):
            if p.is_file() and p.suffix == ".md":
                seen[p] = None
    return list(seen)


def chunk_markdown(rel: str, text: str) -> list[Chunk]:
    """Split by heading; long sections into ~CHUNK_WORDS-word pieces on paragraph (then line) boundaries."""
    chunks: list[Chunk] = []
    trail: list[str] = []
    body: list[str] = []
    fence = False

    def flush():
        section = "\n".join(body).strip()
        body.clear()
        if not section:
            return
        heading = " > ".join(trail) or Path(rel).stem
        piece: list[str] = []
        count = 0
        units = re.split(r"\n\s*\n", section)
        expanded: list[str] = []
        for u in units:  # a paragraph longer than a chunk splits on its lines
            expanded.extend(u.splitlines() if len(u.split()) > CHUNK_WORDS else [u])
        for u in expanded:
            n = len(u.split())
            if piece and count + n > CHUNK_WORDS:
                chunks.append(Chunk(rel, heading, "\n\n".join(piece)))
                piece, count = [], 0
            piece.append(u)
            count += n
        if piece:
            chunks.append(Chunk(rel, heading, "\n\n".join(piece)))

    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
        m = None if fence else re.match(r"^(#{1,6})\s+(.*\S)\s*$", line)
        if m:
            flush()
            level = len(m.group(1))
            trail[:] = trail[: level - 1] + [m.group(2)]
            continue
        body.append(line)
    flush()
    return chunks


def connect() -> sqlite3.Connection:
    DB.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(DB)
    db.executescript(
        """
        CREATE TABLE IF NOT EXISTS files (path TEXT PRIMARY KEY, hash TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS chunks (id INTEGER PRIMARY KEY, path TEXT NOT NULL, heading TEXT NOT NULL,
                                           text TEXT NOT NULL, vec BLOB NOT NULL);
        CREATE INDEX IF NOT EXISTS chunks_path ON chunks(path);
        CREATE VIRTUAL TABLE IF NOT EXISTS fts USING fts5(heading, text, content='chunks', content_rowid='id');
        CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);
        """
    )
    row = db.execute("SELECT value FROM meta WHERE key='model'").fetchone()
    if row and row[0] != MODEL:  # another model's vectors: start over
        db.executescript("DELETE FROM chunks; DELETE FROM files; INSERT INTO fts(fts) VALUES('rebuild');")
    db.execute("INSERT OR REPLACE INTO meta VALUES ('model', ?)", (MODEL,))
    return db


def index(db: sqlite3.Connection | None = None) -> dict:
    db = db or connect()
    present = {p.relative_to(REPO).as_posix(): p for p in files()}
    known = dict(db.execute("SELECT path, hash FROM files"))
    changed, removed = [], [p for p in known if p not in present]
    for rel, p in present.items():
        data = p.read_bytes()
        h = hashlib.sha256(data).hexdigest()
        if known.get(rel) != h:
            changed.append((rel, h, data.decode("utf-8", "replace")))
    for rel in removed + [c[0] for c in changed]:
        for (cid, heading, text) in db.execute("SELECT id, heading, text FROM chunks WHERE path=?", (rel,)).fetchall():
            db.execute("INSERT INTO fts(fts, rowid, heading, text) VALUES('delete', ?, ?, ?)", (cid, heading, text))
        db.execute("DELETE FROM chunks WHERE path=?", (rel,))
        db.execute("DELETE FROM files WHERE path=?", (rel,))
    new = 0
    for rel, h, text in changed:
        chunks = chunk_markdown(rel, text)
        if chunks:
            # the embedded text carries its file and heading, so a chunk that only says "+0x1E" keeps its subject
            vecs = embed([f"{c.path} - {c.heading}\n{c.text}" for c in chunks])
            for c, v in zip(chunks, vecs):
                cur = db.execute("INSERT INTO chunks(path, heading, text, vec) VALUES (?,?,?,?)",
                                 (c.path, c.heading, c.text, v.tobytes()))
                db.execute("INSERT INTO fts(rowid, heading, text) VALUES (?,?,?)", (cur.lastrowid, c.heading, c.text))
            new += len(chunks)
        db.execute("INSERT INTO files VALUES (?,?)", (rel, h))
    db.commit()
    total = db.execute("SELECT COUNT(*) FROM chunks").fetchone()[0]
    return {"files": len(present), "changed": len(changed), "removed": len(removed), "new_chunks": new,
            "chunks": total}


def _fts_query(q: str) -> str:
    # each word as a quoted prefix term, ORed. The tokenizer splits on punctuation, so game.dll+0x5A2BB0 is indexed
    # as game, dll, 0x5a2bb0 and a query for +0x5A2BB0 or 0x5A2BB0 finds it
    words = re.findall(r"\w+", q)
    return " OR ".join(f'"{w}"*' for w in words[:24])


def search(query: str, k: int = 8, db: sqlite3.Connection | None = None) -> list[dict]:
    db = db or connect()
    rows = db.execute("SELECT id, path, heading, text, vec FROM chunks").fetchall()
    if not rows:
        return []
    ids = [r[0] for r in rows]
    mat = np.frombuffer(b"".join(r[4] for r in rows), dtype=np.float32).reshape(len(rows), -1)
    sims = mat @ embed([query])[0]
    vec_rank = [ids[i] for i in np.argsort(-sims)[: max(k * 4, 30)]]
    kw_rank: list[int] = []
    fq = _fts_query(query)
    if fq:
        kw_rank = [r[0] for r in db.execute(
            "SELECT rowid FROM fts WHERE fts MATCH ? ORDER BY bm25(fts, 2.0, 1.0) LIMIT ?", (fq, max(k * 4, 30)))]
    # an exact identifier (0x37C5F90, +0x1E, StratagemInfo, add_damage) means the words matter more than the meaning
    exact = bool(re.search(r"\b0x[0-9a-fA-F]+\b|\w+_\w+|[a-z]+[A-Z]\w*|[A-Z][a-z]+[A-Z]\w*", query))
    weights = (1.0, 3.0 if exact else 1.0)
    score: dict[int, float] = {}
    for weight, rank_list in zip(weights, (vec_rank, kw_rank)):
        for pos, cid in enumerate(rank_list):
            score[cid] = score.get(cid, 0.0) + weight / (RRF_K + pos + 1)
    by_id = {r[0]: r for r in rows}
    sim_by_id = dict(zip(ids, sims.tolist()))
    out = []
    for cid in sorted(score, key=score.get, reverse=True)[:k]:
        r = by_id[cid]
        out.append({"path": r[1], "heading": r[2], "text": r[3], "score": round(score[cid], 4),
                    "similarity": round(sim_by_id[cid], 3), "keyword_hit": cid in kw_rank})
    return out


def rerank(query: str, results: list[dict], k: int) -> list[dict]:
    """Optional: Claude Haiku orders the results by relevance and drops the irrelevant ones (needs ANTHROPIC_API_KEY)."""
    if not results or not os.environ.get("ANTHROPIC_API_KEY"):
        return results[:k]
    import anthropic

    listing = "\n\n".join(f"[{i}] {r['path']} - {r['heading']}\n{r['text'][:1200]}" for i, r in enumerate(results))
    msg = anthropic.Anthropic().messages.create(
        model=os.environ.get("NOTES_RERANK_MODEL", "claude-haiku-5-5"),
        max_tokens=200,
        messages=[{"role": "user", "content":
                   f"Question: {query}\n\nNotes:\n{listing}\n\nReply with only a JSON array of the note numbers that "
                   f"help answer the question, most useful first, at most {k}. [] if none do."}],
    )
    text = "".join(b.text for b in msg.content if getattr(b, "type", "") == "text")
    try:
        order = [i for i in json.loads(re.search(r"\[.*?\]", text, re.S).group(0)) if 0 <= int(i) < len(results)]
    except (AttributeError, ValueError, TypeError):
        return results[:k]
    return [results[int(i)] for i in order][:k]


def main(argv: list[str]) -> int:
    if len(argv) >= 1 and argv[0] == "index":
        print(json.dumps(index(), indent=2))
        return 0
    if len(argv) >= 2 and argv[0] == "search":
        db = connect()
        index(db)
        for r in search(" ".join(argv[1:]), db=db):
            mark = "kw+vec" if r["keyword_hit"] else "vec"
            print(f"{r['score']:.4f} {mark:6} {r['path']} > {r['heading']}")
            print("   " + r["text"][:220].replace("\n", " ") + "\n", flush=True)
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
