"""Local server for the ChessAi analysis board.

Serves the UI from ./static and exposes a small JSON API backed by UCI engines
(Stockfish and ChessAi). Uses only the Python standard library.

    python app/server.py            -> opens http://127.0.0.1:8765 in the browser
    python app/desktop.py           -> same UI in a native window
"""
import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parent
# When frozen by PyInstaller, bundled files live in sys._MEIPASS
BUNDLE = pathlib.Path(getattr(sys, "_MEIPASS", ROOT))
STATIC = BUNDLE / "static"
EXE = ".exe" if sys.platform == "win32" else ""
CPUS = os.cpu_count() or 2


def _find(*candidates):
    return next((p for p in candidates if p.exists()), candidates[0])


ENGINE_SPECS = {
    "stockfish": {
        "label": "Stockfish 19",
        "path": _find(BUNDLE / "engines" / f"stockfish{EXE}"),
        "options": {"Threads": max(1, CPUS // 2), "Hash": 256},
    },
    "chessai": {
        "label": "ChessAi",
        "path": _find(BUNDLE / "engines" / f"chessai{EXE}", ROOT.parent / "engine" / f"chessai{EXE}"),
        "options": {"Hash": 128},
    },
}
DEFAULT_ENGINE = "stockfish" if ENGINE_SPECS["stockfish"]["path"].exists() else "chessai"

FEN_RE = re.compile(r"^[pnbrqkPNBRQK1-8/]+ [wb] (-|[KQkq]{1,4}) (-|[a-h][36])( \d+ \d+)?$")
INFO_RE = re.compile(
    r"depth (?P<depth>\d+).*?multipv (?P<multipv>\d+) score (?P<kind>cp|mate) (?P<score>-?\d+)"
    r".*?nodes (?P<nodes>\d+) nps (?P<nps>\d+).*?time (?P<time>\d+) pv (?P<pv>.+)$"
)
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # don't flash console windows from the GUI app


class Engine:
    """One persistent engine process; searches are serialized, and a new request stops the running one."""

    def __init__(self, path, options):
        self.path = path
        self.options = dict(options)  # currently applied UCI options
        self.lock = threading.Lock()        # one search at a time
        self.write_lock = threading.Lock()  # guards stdin
        self.busy = False
        self.proc = None
        self._start()

    def _start(self):
        self.proc = subprocess.Popen(
            [str(self.path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, bufsize=1, cwd=str(self.path.parent), creationflags=NO_WINDOW,
        )
        self._send("uci")
        self._read_until("uciok")
        for name, value in self.options.items():
            self._send(f"setoption name {name} value {value}")
        self._send("isready")
        self._read_until("readyok")

    def _send(self, cmd):
        with self.write_lock:
            self.proc.stdin.write(cmd + "\n")
            self.proc.stdin.flush()

    def _read_until(self, prefix):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("engine exited unexpectedly")
            if line.startswith(prefix):
                return line.strip()

    def analyze(self, fen, movetime_ms, multipv, options=None):
        if self.busy:
            try:
                self._send("stop")
            except OSError:
                pass
        with self.lock:
            if self.proc.poll() is not None:
                self._start()
            changed = {k: v for k, v in (options or {}).items() if self.options.get(k) != v}
            if changed:
                for name, value in changed.items():
                    self._send(f"setoption name {name} value {value}")
                self.options.update(changed)
                self._send("isready")  # resizing the hash table can take a moment
                self._read_until("readyok")
            self._send(f"setoption name MultiPV value {multipv}")
            self._send(f"position fen {fen}")
            self._send(f"go movetime {movetime_ms}")
            self.busy = True
            lines = {}
            try:
                while True:
                    raw = self.proc.stdout.readline()
                    if not raw:
                        raise RuntimeError("engine exited unexpectedly")
                    raw = raw.strip()
                    if raw.startswith("bestmove"):
                        best = raw.split()[1]
                        break
                    lines.update(self._parse_info(raw))
            finally:
                self.busy = False
            return {"bestmove": None if best in ("0000", "(none)") else best,
                    "lines": [lines[k] for k in sorted(lines)]}

    def close(self):
        try:
            self._send("quit")
            self.proc.wait(timeout=2)
        except Exception:
            self.proc.kill()

    @staticmethod
    def _parse_info(raw):
        """Returns {multipv_index: line} for a completed 'info ... pv ...' line, else {}."""
        if "lowerbound" in raw or "upperbound" in raw:
            return {}  # aspiration-window partial result
        m = INFO_RE.search(raw)
        if not m:
            return {}
        return {int(m["multipv"]): {
            "depth": int(m["depth"]),
            "scoreType": m["kind"],
            "score": int(m["score"]),
            "nodes": int(m["nodes"]),
            "nps": int(m["nps"]),
            "time": int(m["time"]),
            "pv": m["pv"].split(),
        }}


_engines = {}
_engines_lock = threading.Lock()


def get_engine(name):
    with _engines_lock:
        if name not in _engines:
            spec = ENGINE_SPECS[name]
            _engines[name] = Engine(spec["path"], spec["options"])
        return _engines[name]


def close_engines():
    with _engines_lock:
        for e in _engines.values():
            e.close()
        _engines.clear()


def valid_fen(fen):
    if not FEN_RE.match(fen):
        return False
    board = fen.split()[0]
    ranks = board.split("/")
    if len(ranks) != 8 or board.count("K") != 1 or board.count("k") != 1:
        return False
    return all(sum(int(c) if c.isdigit() else 1 for c in r) == 8 for r in ranks)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC), **kwargs)

    def log_message(self, fmt, *args):
        pass  # keep the console quiet

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if self.path == "/api/engines":
            available = [{"id": k, "label": v["label"]} for k, v in ENGINE_SPECS.items() if v["path"].exists()]
            return self._json(200, {"engines": available, "default": DEFAULT_ENGINE, "cpus": CPUS,
                                    "defaultThreads": ENGINE_SPECS["stockfish"]["options"]["Threads"]})
        super().do_GET()

    def do_POST(self):
        if self.path != "/api/analyze":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(min(length, 10_000)) or b"{}")
            fen = str(body.get("fen", "")).strip()
            movetime = max(100, min(int(body.get("movetime", 2000)), 60_000))
            multipv = max(1, min(int(body.get("multipv", 3)), 5))
            engine = str(body.get("engine", DEFAULT_ENGINE))
            threads = int(body.get("threads", 0))  # 0 = engine default
            hash_mb = int(body.get("hash", 0))
        except (ValueError, TypeError):
            return self._json(400, {"error": "bad request"})
        if not valid_fen(fen):
            return self._json(400, {"error": "invalid FEN"})
        if engine not in ENGINE_SPECS or not ENGINE_SPECS[engine]["path"].exists():
            return self._json(400, {"error": f"engine '{engine}' is not available"})
        defaults = ENGINE_SPECS[engine]["options"]
        options = {"Hash": max(16, min(hash_mb, 4096)) if hash_mb else defaults["Hash"]}
        if "Threads" in defaults:  # ChessAi is single-threaded
            options["Threads"] = max(1, min(threads, CPUS)) if threads else defaults["Threads"]
        try:
            result = get_engine(engine).analyze(fen, movetime, multipv, options)
        except (RuntimeError, OSError) as e:
            return self._json(500, {"error": str(e)})
        result["engine"] = ENGINE_SPECS[engine]["label"]
        self._json(200, result)

    def _json(self, status, obj):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def make_server(port=8765):
    """Creates the HTTP server on 127.0.0.1; port=0 picks a free port."""
    return ThreadingHTTPServer(("127.0.0.1", port), Handler)


if __name__ == "__main__":
    if not any(s["path"].exists() for s in ENGINE_SPECS.values()):
        sys.exit("No engine found. Build ChessAi (cd engine && mingw32-make) or add app/engines/stockfish.exe")
    server = make_server()
    url = f"http://127.0.0.1:{server.server_address[1]}"
    print(f"ChessAi analysis board running at {url}  (Ctrl+C to stop)")
    if "--no-browser" not in sys.argv:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        close_engines()
