"""Download/inspect original public packages; do not execute vendor programs."""
import concurrent.futures
import hashlib
import io
import json
import pathlib
import re
import struct
import urllib.request
import zipfile

root = pathlib.Path(__file__).resolve().parent
out = root / "candidates" / "afu-package-review"
out.mkdir(parents=True, exist_ok=True)
urls = [
    ("ami-current.zip", "https://f.hubspotusercontent10.net/hubfs/9443417/Support/BIOS_Firmware_Update/Aptio_V_AMI_Firmware_Update_Utility.zip"),
    ("asrock-afulnx.zip", "https://download.asrock.com/TSD/Afulnx.zip"),
]

def inspect_zip(data, prefix, rows, depth=0):
    if depth > 4:
        raise RuntimeError("Nested archive depth exceeded")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            if member.is_dir():
                continue
            if member.file_size > 40000000:
                raise RuntimeError("Archive member exceeds limit")
            contents = archive.read(member)
            name = prefix + "!" + member.filename
            row = dict(name=name, bytes=len(contents), sha256=hashlib.sha256(contents).hexdigest())
            if contents[:4] == b"\x7fELF":
                row.update(format="ELF", elf_class=contents[4], machine=struct.unpack_from("<H", contents, 18)[0])
                if pathlib.PurePosixPath(member.filename).name.lower() == "afulnx_64":
                    (out / "afulnx_64").write_bytes(contents)
            elif contents[:2] == b"MZ":
                row["format"] = "MZ"
                offset = struct.unpack_from("<I", contents, 60)[0]
                if contents[offset:offset+4] == b"PE\0\0":
                    row.update(format="PE", machine=struct.unpack_from("<H", contents, offset+4)[0],
                               subsystem=struct.unpack_from("<H", contents, offset+24+68)[0])
            else:
                row["format"] = "other"
            if member.filename.lower().endswith("readme.txt") or row["format"] == "ELF":
                strings = [m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{5,}", contents)]
                row["version_strings"] = [s[:200] for s in strings if re.search(r"5\.\d+\.\d+(?:\.\d+)?|AFULNX|Copyright", s, re.I)][:20]
                if member.filename.lower().endswith("readme.txt") and "asrock" in prefix:
                    (out / "asrock-readme.txt").write_bytes(contents)
            rows.append(row)
            if member.filename.lower().endswith(".zip"):
                inspect_zip(contents, name, rows, depth+1)

def download(item):
    name, url = item
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "MagicControl-research"}), timeout=45) as response:
            data = response.read(40000001)
            modified, final = response.headers.get("Last-Modified"), response.url
        if len(data) > 40000000:
            raise RuntimeError("Package exceeds size limit")
        (out / name).write_bytes(data)
        members = []
        inspect_zip(data, name, members)
        return dict(file=name, url=url, final_url=final, last_modified=modified, bytes=len(data),
                    sha256=hashlib.sha256(data).hexdigest(), members=members)
    except Exception as error:
        return dict(file=name, url=url, error=str(error))

if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(download, urls))
    (root / "afu-package-review.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    for row in results:
        print(json.dumps({k: v for k, v in row.items() if k != "members"}))
        for member in row.get("members", []):
            if member.get("format") == "ELF" or member.get("version_strings"):
                print(json.dumps(member))
