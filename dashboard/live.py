"""
Live terminal dashboard — connects to the C++ server's /ws/feed SSE stream
and renders a live updating table using the `rich` library.

Usage:
    pip install rich requests websockets
    python dashboard/live.py [http://localhost:8000]
"""
from __future__ import annotations

import json
import sys
import time
import threading
from datetime import datetime
from typing import Any, Dict, List, Optional

import requests
from rich.console import Console
from rich.layout import Layout
from rich.live import Live
from rich.panel import Panel
from rich.table import Table

BASE_URL = sys.argv[1].rstrip("/") if len(sys.argv) > 1 else "http://localhost:8000"

console = Console()

_snapshot: Dict[str, Any] = {}
_recent_trades: List[Dict] = []
_lock = threading.Lock()


# ─────────────────────────────────────────────────────────────────────────────
# Formatting helpers
# ─────────────────────────────────────────────────────────────────────────────
def _fmt(v: Optional[float], dp: int = 4) -> str:
    return f"{v:,.{dp}f}" if v is not None else "—"


# ─────────────────────────────────────────────────────────────────────────────
# Render
# ─────────────────────────────────────────────────────────────────────────────
def build_panel() -> Panel:
    with _lock:
        snap = dict(_snapshot)
        trades = list(_recent_trades[:5])

    ts_raw   = snap.get("timestamp")
    ts_str   = datetime.fromtimestamp(ts_raw).strftime("%H:%M:%S.%f")[:-3] if ts_raw else "—"
    best_bid = snap.get("best_bid")
    best_ask = snap.get("best_ask")
    spr_pct  = snap.get("spread_pct")

    # Header
    header = Table.grid(expand=True)
    header.add_column(ratio=1); header.add_column(ratio=1); header.add_column(ratio=1)
    header.add_row(
        f"[bold cyan]Time:[/] {ts_str}",
        f"[bold green]Bid:[/] {_fmt(best_bid)}",
        f"[bold red]Ask:[/] {_fmt(best_ask)}",
    )
    spread_str = f"{spr_pct:.4f}%" if spr_pct is not None else "—"
    header.add_row("", f"[bold yellow]Spread:[/] {spread_str}", "")

    # Book depth
    book_tbl = Table(show_header=True, header_style="bold white", expand=True,
                     title="[bold]Order Book Depth (top 3)[/]")
    book_tbl.add_column("Bid Qty",   justify="right", style="green")
    book_tbl.add_column("Bid Price", justify="right", style="bold green")
    book_tbl.add_column("Ask Price", justify="left",  style="bold red")
    book_tbl.add_column("Ask Qty",   justify="left",  style="red")

    bids = snap.get("bid_depth", [])[:3]
    asks = snap.get("ask_depth", [])[:3]
    for i in range(max(len(bids), len(asks))):
        bid = bids[i] if i < len(bids) else None
        ask = asks[i] if i < len(asks) else None
        book_tbl.add_row(
            _fmt(bid["total_quantity"]) if bid else "—",
            _fmt(bid["price"])          if bid else "—",
            _fmt(ask["price"])          if ask else "—",
            _fmt(ask["total_quantity"]) if ask else "—",
        )

    # Trades
    trade_tbl = Table(show_header=True, header_style="bold white", expand=True,
                      title="[bold]Last 5 Trades[/]")
    trade_tbl.add_column("Price",    justify="right")
    trade_tbl.add_column("Qty",      justify="right")
    trade_tbl.add_column("Time (ns)",justify="right")
    for tr in trades:
        trade_tbl.add_row(
            f"[cyan]{_fmt(tr.get('price'))}[/]",
            _fmt(tr.get("qty")),
            str(tr.get("timestamp", "—")),
        )

    layout = Layout()
    layout.split_column(
        Layout(header,    size=3),
        Layout(book_tbl,  size=10),
        Layout(trade_tbl, size=8),
    )
    return Panel(layout, title="[bold magenta]Order Book — Live Feed (C++)[/]",
                 border_style="bright_blue")


# ─────────────────────────────────────────────────────────────────────────────
# SSE receiver thread
# ─────────────────────────────────────────────────────────────────────────────
def sse_receiver() -> None:
    url = f"{BASE_URL}/ws/feed"
    console.print(f"[green]Connecting to[/] {url}")
    while True:
        try:
            with requests.get(url, stream=True, timeout=10) as resp:
                for line in resp.iter_lines():
                    if not line:
                        continue
                    try:
                        data = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    with _lock:
                        _snapshot.update(data)
                        lt = data.get("last_trade")
                        if lt:
                            already = any(
                                t.get("timestamp") == lt.get("timestamp")
                                for t in _recent_trades
                            )
                            if not already:
                                _recent_trades.insert(0, lt)
                                if len(_recent_trades) > 20:
                                    _recent_trades.pop()
        except Exception as exc:
            console.print(f"[red]Stream error:[/] {exc} — retrying in 2s")
            time.sleep(2)


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────
def main() -> None:
    t = threading.Thread(target=sse_receiver, daemon=True)
    t.start()

    with Live(build_panel(), console=console, refresh_per_second=5, screen=True) as live:
        try:
            while True:
                time.sleep(0.2)
                live.update(build_panel())
        except KeyboardInterrupt:
            pass
    console.print("[yellow]Dashboard stopped.[/]")


if __name__ == "__main__":
    main()
