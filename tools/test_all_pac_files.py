#!/usr/bin/env python3
"""Comprehensive PAC Archive Test Suite for DMC Native Reader (Linux / Portable).
Tests all .pac / .PAC archives using the optimized dmc_native_reader_cli binary.
Collects format compliance, entry counts, slot classifications, health checks,
and crash-free execution stats.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed

def find_cli_binary():
    candidates = [
        os.path.abspath("build_release/dmc_native_reader_cli"),
        os.path.abspath("linux/.build/bin/dmc_native_reader_cli"),
        os.path.abspath("build_test/dmc_native_reader_cli"),
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None

def parse_inspect_output(stdout_text):
    info = {
        "ok": True,
        "family": "UNKNOWN",
        "domain": "UNKNOWN",
        "support": "UNKNOWN",
        "evidence": "UNKNOWN",
        "identity": "UNKNOWN",
        "renderable": False,
        "composite_parts": 0,
        "children": 0,
        "health": "UNKNOWN",
        "health_notices": [],
        "capabilities": [],
        "declared_slots": 0,
        "populated_slots": 0,
        "slot_types": Counter(),
        "entries": [],
    }

    for line in stdout_text.splitlines():
        line = line.strip()
        if line.startswith("family:"):
            info["family"] = line.split(":", 1)[1].strip()
        elif line.startswith("domain:"):
            info["domain"] = line.split(":", 1)[1].strip()
        elif line.startswith("support:"):
            info["support"] = line.split(":", 1)[1].strip()
        elif line.startswith("evidence:"):
            info["evidence"] = line.split(":", 1)[1].strip()
        elif line.startswith("identity:"):
            info["identity"] = line.split(":", 1)[1].strip()
        elif line.startswith("renderable:"):
            info["renderable"] = line.split(":", 1)[1].strip().lower() == "true"
        elif line.startswith("composite_parts:"):
            try: info["composite_parts"] = int(line.split(":", 1)[1].strip())
            except: pass
        elif line.startswith("children:"):
            try: info["children"] = int(line.split(":", 1)[1].strip())
            except: pass
        elif line.startswith("health:"):
            info["health"] = line.split(":", 1)[1].strip()
        elif line.startswith("capabilities:"):
            info["capabilities"] = line.split(":", 1)[1].strip().split()
        elif "DeclaredSlots:" in line:
            m = re.search(r"DeclaredSlots:\s*(\d+)", line)
            if m: info["declared_slots"] = int(m.group(1))
        elif "PopulatedSlots:" in line:
            m = re.search(r"PopulatedSlots:\s*(\d+)", line)
            if m: info["populated_slots"] = int(m.group(1))
        elif re.match(r"^#\d+\s*·\s*([^·\s]+)", line):
            m = re.match(r"^#\d+\s*·\s*([^·\s]+)", line)
            if m:
                st = m.group(1).strip()
                info["slot_types"][st] += 1
                info["entries"].append(line)
        elif line.startswith("🟠"):
            info["health_notices"].append(line)

    return info

def test_single_pac(cli_path, file_path, timeout_sec):
    t0 = time.time()
    try:
        st = os.stat(file_path)
        is_cloud = (st.st_blocks == 0 and st.st_size > 0)
        size_b = st.st_size
    except:
        is_cloud = False
        size_b = 0

    result = {
        "path": file_path,
        "name": os.path.basename(file_path),
        "size_bytes": size_b,
        "is_cloud": is_cloud,
        "returncode": -1,
        "elapsed_sec": 0.0,
        "error": None,
        "parsed": None,
    }

    cmd = [cli_path, "inspect", file_path, "all"]
    for attempt in range(2):
        cur_timeout = timeout_sec if attempt == 0 else timeout_sec + 30
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=cur_timeout)
            result["returncode"] = proc.returncode
            result["elapsed_sec"] = time.time() - t0
            if proc.returncode == 0:
                result["parsed"] = parse_inspect_output(proc.stdout)
                result["parsed"]["slot_types"] = dict(result["parsed"]["slot_types"])
                result["error"] = None
                break
            else:
                result["error"] = proc.stderr.strip() or f"exit code {proc.returncode}"
        except subprocess.TimeoutExpired:
            result["elapsed_sec"] = time.time() - t0
            result["error"] = f"timed out after {cur_timeout}s"
            if attempt == 0 and is_cloud:
                continue
        except Exception as e:
            result["elapsed_sec"] = time.time() - t0
            result["error"] = str(e)
            break

    return result

def main():
    parser = argparse.ArgumentParser(description="Test all PAC files with DMC Native Reader CLI")
    parser.add_argument("--local-only", action="store_true", default=False, help="Test only locally cached files")
    parser.add_argument("--all", action="store_true", default=False, help="Test all files including cloud download")
    parser.add_argument("--workers", type=int, default=8, help="Number of parallel workers")
    parser.add_argument("--limit", type=int, default=0, help="Limit number of files tested (0 = unlimited)")
    parser.add_argument("--timeout", type=int, default=30, help="Per-file timeout in seconds")
    parser.add_argument("--out-json", type=str, default="build_release/pac_test_results.json", help="Output JSON path")
    parser.add_argument("--out-md", type=str, default="docs/port/PAC_COMPREHENSIVE_TEST_REPORT.md", help="Output Markdown report path")
    args = parser.parse_args()

    cli_path = find_cli_binary()
    if not cli_path:
        print("ERROR: dmc_native_reader_cli executable not found! Please build it first.", file=sys.stderr)
        sys.exit(1)
    print(f"Using CLI binary: {cli_path}")

    # Discover PAC files
    search_dirs = [
        "/Users/mariayavdoshnyak/orca/projects/dmc3-clean-engine-mac/assets/void",
        "/Users/mariayavdoshnyak/Desktop/Dante Awakening Dirty Room.app/Contents/Resources",
        "/Users/mariayavdoshnyak/Library/CloudStorage/GoogleDrive-rusak.viktor.vr@gmail.com/Mi unidad/DMC Rengine/DMC HD Vanilla/GData afs",
    ]

    discovered = []
    seen = set()
    for d in search_dirs:
        if not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if f.lower().endswith(".pac"):
                    full_p = os.path.join(root, f)
                    real_p = os.path.realpath(full_p)
                    if real_p not in seen:
                        seen.add(real_p)
                        discovered.append(full_p)

    print(f"Discovered {len(discovered)} unique PAC archives across search paths.")

    # Filter local vs cloud
    target_files = []
    for f in discovered:
        try:
            st = os.stat(f)
            is_cloud = (st.st_blocks == 0 and st.st_size > 0)
            if args.local_only and is_cloud:
                continue
            target_files.append(f)
        except:
            pass

    if args.limit > 0:
        target_files = target_files[:args.limit]

    local_count = sum(1 for f in target_files if os.stat(f).st_blocks > 0)
    cloud_count = len(target_files) - local_count
    print(f"Target set: {len(target_files)} files ({local_count} local on SSD, {cloud_count} cloud placeholders)")

    if not target_files:
        print("No files to test based on selection filters.")
        return

    # Run tests
    print(f"Executing batch verification across {args.workers} worker threads (timeout: {args.timeout}s)...")
    results = []
    t_start = time.time()
    completed = 0
    passed = 0
    failed = 0
    total_slots = 0
    total_bytes = 0
    aggregate_slot_types = Counter()
    family_counts = Counter()
    health_counts = Counter()

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(test_single_pac, cli_path, f, args.timeout): f for f in target_files}
        for fut in as_completed(futures):
            res = fut.result()
            results.append(res)
            completed += 1
            total_bytes += res["size_bytes"]

            if res["returncode"] == 0 and res["parsed"] and res["parsed"]["family"] in ("PAC", "PNST", "FXBANK", "DEMO"):
                passed += 1
                p = res["parsed"]
                family_counts[p["family"]] += 1
                health_counts[p["health"]] += 1
                total_slots += p["children"]
                for st, count in p["slot_types"].items():
                    aggregate_slot_types[st] += count
            else:
                failed += 1
                print(f"  [FAIL] {res['name']}: {res['error']}", flush=True)

            if completed % 50 == 0 or completed == len(target_files):
                elapsed = time.time() - t_start
                rate = completed / elapsed if elapsed > 0 else 0
                pct = (completed / len(target_files)) * 100
                print(f"Progress: {completed}/{len(target_files)} ({pct:.1f}%) | "
                      f"Passed: {passed} | Failed: {failed} | "
                      f"Rate: {rate:.1f} files/s", flush=True)

    t_total = time.time() - t_start
    print("\n================== TEST RUN COMPLETE ==================")
    print(f"Total Archives Tested: {len(results)}")
    print(f"Successful Tests:      {passed} ({(passed/len(results))*100:.1f}%)")
    print(f"Failures / Crashes:    {failed}")
    print(f"Total Bytes Read:      {total_bytes / (1024*1024):.2f} MB")
    print(f"Total Slots Discovered:{total_slots}")
    print(f"Time Taken:            {t_total:.2f} s ({len(results)/t_total:.1f} files/sec)")
    print("\nFormat Distribution:")
    for fam, cnt in family_counts.most_common():
        print(f"  {fam}: {cnt}")
    print("\nHealth Status:")
    for h, cnt in health_counts.most_common():
        print(f"  {h}: {cnt}")
    print("\nAggregate Slot Content Types (Sub-Resources):")
    for st, cnt in aggregate_slot_types.most_common(20):
        print(f"  {st:16s}: {cnt}")

    # Write JSON results
    os.makedirs(os.path.dirname(os.path.abspath(args.out_json)), exist_ok=True)
    with open(args.out_json, "w") as fp:
        json.dump({
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "cli_binary": cli_path,
            "total_tested": len(results),
            "passed": passed,
            "failed": failed,
            "total_slots": total_slots,
            "total_bytes": total_bytes,
            "elapsed_seconds": t_total,
            "family_distribution": dict(family_counts),
            "health_distribution": dict(health_counts),
            "slot_types_distribution": dict(aggregate_slot_types),
            "files": results,
        }, fp, indent=2)
    print(f"\nWrote full JSON test details to: {args.out_json}")

    # Write Markdown summary report
    os.makedirs(os.path.dirname(os.path.abspath(args.out_md)), exist_ok=True)
    with open(args.out_md, "w") as fp:
        fp.write(f"# DMC Native Reader PAC Verification Report\n\n")
        fp.write(f"- **Date:** {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime())}\n")
        fp.write(f"- **Tested Archives:** {len(results)}\n")
        fp.write(f"- **Pass Rate:** {passed} / {len(results)} ({(passed/len(results))*100:.2f}%)\n")
        fp.write(f"- **Failures / Crashes:** {failed}\n")
        fp.write(f"- **Total Archive Data:** {total_bytes / (1024*1024):.2f} MB\n")
        fp.write(f"- **Total Discovered Slots / Sub-Resources:** {total_slots}\n")
        fp.write(f"- **Execution Time:** {t_total:.2f}s ({len(results)/t_total:.1f} files/sec)\n\n")

        fp.write("## Format Distribution\n\n")
        fp.write("| Format Family | Count | Description |\n")
        fp.write("|---|---|---|\n")
        for fam, cnt in family_counts.most_common():
            desc = "Standard DMC Relative-Slot Archive" if fam == "PAC" else \
                   "Compressed Multi-Slot Container" if fam == "PNST" else \
                   "Cutscene Scene & Movie Archive" if fam == "DEMO" else \
                   "Effect Bank Manifest Archive"
            fp.write(f"| `{fam}` | **{cnt}** | {desc} |\n")

        fp.write("\n## Sub-Resource / Slot Types Discovered\n\n")
        fp.write("| Slot Type | Total Count | Category |\n")
        fp.write("|---|---|---|\n")
        for st, cnt in aggregate_slot_types.most_common():
            cat = "3D Mesh Model" if st == "MOD" else \
                  "Texture Package" if st == "PTX" else \
                  "Animation Motion" if st == "MOT" else \
                  "Nested Archive" if st == "PAC" else \
                  "Shadow Volume" if st == "SHW" else \
                  "Collision Hitbox" if st == "HITS" else \
                  "Motion Script" if "Motion" in st else \
                  "Effect Bank" if st == "FXBANK" else "Game Resource"
            fp.write(f"| `{st}` | **{cnt}** | {cat} |\n")

        fp.write("\n## Health Breakdown\n\n")
        fp.write("| Health Grade | Files | Description |\n")
        fp.write("|---|---|---|\n")
        for h, cnt in health_counts.most_common():
            fp.write(f"| `{h}` | **{cnt}** | {'Clean strictly conforming data' if h == 'clean' else 'Readable with minor notices'} |\n")

    print(f"Wrote Markdown summary report to: {args.out_md}")

if __name__ == "__main__":
    main()
