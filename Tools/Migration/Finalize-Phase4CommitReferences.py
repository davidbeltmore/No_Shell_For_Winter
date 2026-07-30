"""Replace Phase 4 manifest placeholders with the commits that contain them."""

import csv
import io
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
EXPECTED = {
    "Docs/Migration/Evidence/Phase4_EFProjectCoreContent_ConfigAutomation.json": {
        "count": 32,
        "commit": "671feda",
    },
    "Docs/Migration/Evidence/Phase4_ModernUI_ConfigBuild.json": {
        "count": 127,
        "commit": "4a66c74",
    },
    "Docs/Migration/Evidence/Phase4_ProceduralContracts_ContentBuild.json": {
        "count": 20,
        "commit": "259bf42",
    },
    "Docs/Migration/Evidence/Phase4_DungeonGeneration_ContentBuild.json": {
        "count": 1,
        "commit": "95fcd1b",
    },
}


def render_row(row, line_ending):
    stream = io.StringIO(newline="")
    writer = csv.writer(
        stream,
        quoting=csv.QUOTE_ALL,
        lineterminator=line_ending,
    )
    writer.writerow(row)
    return stream.getvalue().encode("utf-8")


raw_lines = MANIFEST.read_bytes().splitlines(keepends=True)
if not raw_lines:
    raise RuntimeError("Migration manifest is empty")
header = next(csv.reader([raw_lines[0].decode("utf-8-sig").rstrip("\r\n")]))
indices = {name: index for index, name in enumerate(header)}
if not {"TestEvidence", "Commit", "Result"}.issubset(indices):
    raise RuntimeError("Migration manifest schema differs from the audited schema")

counts = {evidence: 0 for evidence in EXPECTED}
for line_index in range(1, len(raw_lines)):
    encoded = raw_lines[line_index]
    line_ending = "\r\n" if encoded.endswith(b"\r\n") else "\n"
    row = next(csv.reader([encoded.decode("utf-8").rstrip("\r\n")]))
    if len(row) != len(header):
        raise RuntimeError("Malformed manifest row at line {}".format(line_index + 1))
    evidence = row[indices["TestEvidence"]]
    expected = EXPECTED.get(evidence)
    if not expected:
        continue
    if row[indices["Result"]] != "PASS":
        raise RuntimeError("Refusing to finalize a non-PASS manifest row")
    row[indices["Commit"]] = expected["commit"]
    raw_lines[line_index] = render_row(row, line_ending)
    counts[evidence] += 1

for evidence, expected in EXPECTED.items():
    if counts[evidence] != expected["count"]:
        raise RuntimeError(
            "Commit-reference row count differs for {}: {} != {}".format(
                evidence, counts[evidence], expected["count"]
            )
        )

MANIFEST.write_bytes(b"".join(raw_lines))
print("PHASE4_COMMIT_REFERENCES_PASS rows={}".format(sum(counts.values())))
