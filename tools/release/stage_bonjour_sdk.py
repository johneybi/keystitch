"""Stage the pinned Windows build SDK from its release asset or a local archive."""
import argparse
import hashlib
import io
from pathlib import Path
import tempfile
import urllib.request
import zipfile


URL = ("https://github.com/nelsonjchen/mDNSResponder/releases/download/"
       "v2019.05.08.1/x64_RelWithDebInfo.zip")
SIZE = 64457
SHA256 = "aad489258d047e396a43efff4270571c82e7da5d33cba1710b784b66fd4011f9"
FILES = ("Include/dns_sd.h", "Lib/x64/dnssd.lib", "Lib/x64/dnssd.pdb")


def stage(stream, destination):
    data = stream.read(SIZE + 1)
    if len(data) != SIZE or hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("Bonjour SDK bytes do not match the pinned release asset")
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Choose a fresh SDK destination: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            sdk = Path(temporary) / "sdk"
            for name in FILES:
                target = sdk / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(name))
            sdk.rename(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--archive", type=Path, help="Use a previously downloaded ZIP")
    args = parser.parse_args()
    try:
        with (args.archive.open("rb") if args.archive else urllib.request.urlopen(URL, timeout=60)) as source:
            stage(source, args.destination)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"SDK staging failed: {error}\n")
    print(f"Verified Bonjour SDK SHA256 {SHA256}")
