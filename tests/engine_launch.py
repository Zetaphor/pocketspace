"""Build catalog-only Pocketspace launch arguments for test scenes."""
from pathlib import Path


def catalog_arguments(scene):
    path = Path(scene).resolve(strict=True)
    if path.suffix != ".js":
        raise ValueError(f"expected a JavaScript scene: {path}")
    return ["--scripts-dir", str(path.parent), "--launch", path.name]
