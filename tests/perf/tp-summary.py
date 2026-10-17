#!/usr/bin/env python3
"""Summarise an xctrace time-profile export (xcrun xctrace export --xpath
'.../table[@schema="time-profile"]'): where the main thread spent its
samples, by function and by library, self and inclusive.
usage: tests/perf/tp-summary.py export.xml [--thread Main] [--top 30]"""
import argparse, collections, xml.etree.ElementTree as ET

ap = argparse.ArgumentParser()
ap.add_argument("xml")
ap.add_argument("--thread", default="Main Thread")
ap.add_argument("--top", type=int, default=30)
ap.add_argument("--under", default="", help="only samples whose stack has this function")
a = ap.parse_args()

root = ET.parse(a.xml).getroot()
ids = {}
for el in root.iter():
    if el.get("id"):
        ids[el.get("id")] = el

def res(el):
    return ids[el.get("ref")] if el.get("ref") else el

self_f = collections.Counter(); incl_f = collections.Counter()
self_l = collections.Counter(); incl_l = collections.Counter()
n = 0
for row in root.iter("row"):
    th = row.find("thread")
    if th is None or a.thread not in (res(th).get("fmt") or ""):
        continue
    bt = row.find("tagged-backtrace")
    if bt is None:
        continue
    bt = res(bt)
    inner = bt.find("backtrace")
    frames = [res(f) for f in (res(inner) if inner is not None else bt).iter("frame")]
    fn, libs = [], []
    for f in frames:
        b = f.find("binary")
        lib = res(b).get("name") if b is not None else "?"
        fn.append(f"{f.get('name') or '?'}  [{lib}]")
        libs.append(lib)
    if not fn:
        continue
    if a.under and not any(a.under in x for x in fn):
        continue
    n += 1
    self_f[fn[0]] += 1; self_l[libs[0]] += 1
    for x in set(fn): incl_f[x] += 1
    for x in set(libs): incl_l[x] += 1

print(f"{n} samples (1 ms each) on '{a.thread}'" + (f" under {a.under}" if a.under else ""))
for title, c in (("self, by library", self_l), ("inclusive, by library", incl_l),
                 ("self, by function", self_f), ("inclusive, by function", incl_f)):
    print(f"--- {title}")
    for k, v in c.most_common(a.top):
        print(f"{v:5d}  {k}")
