# interface.py
# allows this script to connect to the executable

import subprocess
from typing import List
from pathlib import Path

def parse_search_output(output: str) -> List[str]:
    """for now we assume the executable output will just be a list of urls"""
    urls = []
    for line in output.strip().split('\n'):
        urls.append(line.strip())

    return urls

def get_results(query: str, exe_path: str = "./build/engine", top_k: int = 10) -> List[str]:
    if not Path(exe_path).exists():
        raise FileNotFoundError(f"executable {exe_path} not found, run make")

    # assuming certain cli args but can change
    res = subprocess.run(
        [ exe_path, "--query", query, "--limit", str(top_k) ],
        capture_output = True,
            text = True,
            timeout = 30,
            check = True
    )

    urls = parse_search_output(res.stdout)

    return urls

