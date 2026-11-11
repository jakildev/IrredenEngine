#!/usr/bin/env python3
"""Cross-arm grading of the slab capture matrix: per-case missing/excess, control
byte-identity against zero, moving==static, and the probe collision census."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
arms = {a: json.loads((root / f"captures-{a}.json").read_text()) for a in ("zero", "associative", "direct")}


def grade(entry):
    v = entry.get("versus_control")
    return (v["missing_shadow_outside_band"], v["excess_shadow_outside_band"]) if v else None


def census(entry):
    out = {}
    shots = entry["shots"] if isinstance(entry.get("shots"), list) else [entry]
    for shot in shots:
        for cascade, c in (shot.get("probe") or {}).get("cascades", {}).items():
            acc = out.setdefault(cascade, {})
            for key in ("texels", "overflow", "direct_collisions", "complete_rows",
                        "complete_modulo_collisions", "overflowed_rows",
                        "overflowed_retained_modulo_collisions"):
                acc[key] = acc.get(key, 0) + c[key]
    return out


rows = []
for name, zero in arms["zero"]["cases"].items():
    row = {"case": name}
    for arm, report in arms.items():
        entry = report["cases"][name]
        row[arm] = grade(entry)
        if arm != "zero":
            if "receiver_sha256" in entry:
                row[f"{arm}_receiver_eq_zero"] = entry["receiver_sha256"] == zero["receiver_sha256"]
                row[f"{arm}_frame_eq_zero"] = entry["frame_sha256"] == zero["frame_sha256"]
            row[f"{arm}_census"] = census(entry)
        if isinstance(entry.get("shots"), list):
            row[f"{arm}_moving_vs_static"] = [
                (s["versus_control"]["missing_shadow_outside_band"],
                 s["versus_control"]["excess_shadow_outside_band"]) for s in entry["shots"]]
    rows.append(row)
(root / "cross-arm.json").write_text(json.dumps(rows, indent=1) + "\n")

print("| case | zero miss/excess | assoc miss/excess | direct miss/excess | assoc rx=zero | direct rx=zero | direct collisions (c0/c1) | assoc complete mod-coll / overflowed rows (c0/c1) |")
print("|---|---|---|---|---|---|---|---|")
for r in rows:
    if "zero_moving_vs_static" in r:
        for arm in arms:
            bad = [p for p in r[f"{arm}_moving_vs_static"] if p != (0, 0)]
            print(f"| {r['case']} | {arm} moving==static: {'yes' if not bad else bad} |")
        continue
    dc = r["direct_census"]
    ac = r["associative_census"]
    fmt = lambda c, k: "/".join(str(c.get(x, {}).get(k, 0)) for x in ("0", "1"))
    print(f"| {r['case']} | {r['zero']} | {r['associative']} | {r['direct']} | "
          f"{r.get('associative_receiver_eq_zero')} | {r.get('direct_receiver_eq_zero')} | "
          f"{fmt(dc, 'direct_collisions')} | {fmt(ac, 'complete_modulo_collisions')} ; {fmt(ac, 'overflowed_rows')} |")
