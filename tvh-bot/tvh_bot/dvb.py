from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Literal

DvbEvent = Literal["drop", "recover"]


def scan_dvb_adapters(path: Path | str = Path("/dev/dvb")) -> list[str]:
    root = Path(path)
    if not root.exists():
        return []
    return sorted(
        item.name
        for item in root.iterdir()
        if item.is_dir() and item.name.startswith("adapter")
    )


@dataclass
class DvbMonitor:
    expected_count: int
    was_healthy: bool | None = None

    def evaluate(self, adapters: Iterable[str]) -> DvbEvent | None:
        current_count = len(list(adapters))
        healthy = current_count >= self.expected_count

        if self.was_healthy is None:
            self.was_healthy = healthy
            return None

        if self.was_healthy and not healthy:
            self.was_healthy = False
            return "drop"

        if not self.was_healthy and healthy:
            self.was_healthy = True
            return "recover"

        return None
