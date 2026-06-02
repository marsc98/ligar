"""
Captura eventos do monitor KWS e salva em JSONL.
Uso: python capture_monitor.py <IP> [--out logs/session.jsonl]

Formato de saída (uma linha por evento):
{"ts":"01:42:07","rms":763,"var":0.61,"word":null,"dists":{"ligar":4.3,"garbage":4.8},"rejected":null}
"""
import argparse
import asyncio
import json
import sys
from datetime import datetime
from pathlib import Path

try:
    import websockets
except ImportError:
    sys.exit("Instale: pip install websockets")


async def capture(ip: str, out_path: Path):
    url = f"ws://{ip}/monitor"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Conectando em {url}")
    print(f"Salvando em {out_path}")
    print("Ctrl+C para parar\n")

    async with websockets.connect(url) as ws:
        with out_path.open("a") as f:
            async for raw in ws:
                try:
                    data = json.loads(raw)
                except json.JSONDecodeError:
                    continue

                event = {
                    "ts": datetime.now().strftime("%H:%M:%S"),
                    "rms": data.get("rms"),
                    "var": round(data["var"], 3) if "var" in data else None,
                    "word": data.get("word"),
                    "dists": data.get("dists", {}),
                    "rejected": data.get("rejected"),
                    "garbage_dist": data.get("garbage_dist"),
                }

                line = json.dumps(event, separators=(",", ":"))
                f.write(line + "\n")
                f.flush()

                # print resumido no terminal
                parts = [f"{event['ts']} rms={event['rms']:.0f}"]
                if event["var"] is not None:
                    parts.append(f"var={event['var']:.2f}")
                for w, d in event["dists"].items():
                    parts.append(f"{w}={d:.1f}")
                if event["word"]:
                    parts.append(f"→ DETECTOU: {event['word']}")
                if event["rejected"]:
                    parts.append(f"[{event['rejected']}]")
                print("  ".join(parts))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ip", help="IP da ESP32")
    parser.add_argument("--out", default=None, help="Arquivo de saída (.jsonl)")
    args = parser.parse_args()

    if args.out is None:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = Path(f"logs/monitor_{ts}.jsonl")
    else:
        out_path = Path(args.out)

    try:
        asyncio.run(capture(args.ip, out_path))
    except KeyboardInterrupt:
        print(f"\nSalvo em {out_path}")


if __name__ == "__main__":
    main()
