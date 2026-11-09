#!/usr/bin/env python3
"""Segmented timing analyzer for #193 line B2.

Modes:
  default        analyze one or more segmented-probe jsonl files (browser_round.js v2)
  --replay       analyze legacy @604-era campaign jsonl (10s sampling, no WS marks)
  --selftest     run synthetic-trace correctness checks of the segment math

Segment model (per client, per WS connection cycle):
  seg1_page_load_s            nav_start -> appsite_video_present
  seg2_ws_open_first_msg_s    ws open -> first WS message        (per cycle)
  seg3_join_first_frame_s     first WS message -> first decodable frame
                              (vfc callback or readyState>=2 advancing)
  seg4 steady window          fps from vfc deltas, stalls (>1s vfc gap),
                              teardowns (readyState drops / extra WS cycles),
                              post-offline recovery (offline_cleared -> next
                              decodable frame; NEVER if none until end)
"""
import argparse
import json
import math
import sys
import tempfile
import os


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    k = max(0, math.ceil(p / 100.0 * len(s)) - 1)
    return s[k]


def med(values):
    return pct(values, 50)


def fmt(v, unit="s"):
    if v is None:
        return "NEVER" if unit == "r" else "-"
    return f"{v:.2f}"


def load_jsonl(path):
    recs = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                recs.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return recs


# ---------------------------------------------------------------- new format
def analyze_probe(recs, label=""):
    clients = {}
    for r in recs:
        c = r.get("client") or ("c1" if r.get("kind") in ("meta", "event") else None)
        if not c:
            continue
        clients.setdefault(c, {"marks": {}, "ws": [], "samples": [], "final": None,
                               "events": [], "decodable": [], "ready_drops": []})
        cl = clients[c]
        k = r.get("kind")
        if k == "mark":
            cl["marks"][r["mark"]] = r["t_epoch"]
        elif k == "ws":
            cl["ws"].append(r)
        elif k == "sample":
            cl["samples"].append(r)
        elif k == "probe_final":
            cl["final"] = r.get("probe")
            if cl["final"]:
                cl["decodable"] = [e["t"] / 1000.0 for e in cl["final"].get("decodableEvents", [])]
                cl["ready_drops"] = [e["t"] / 1000.0 for e in cl["final"].get("readyDrops", [])]
    out = {"label": label, "clients": {}}
    for c, cl in clients.items():
        seg = {}
        marks = cl["marks"]
        if "nav_start" in marks and "appsite_video_present" in marks:
            seg["seg1_page_load_s"] = marks["appsite_video_present"] - marks["nav_start"]
        if "appsite_frame_attached" in marks and "appsite_video_present" in marks:
            seg["iframe_attach_to_video_s"] = marks["appsite_video_present"] - marks["appsite_frame_attached"]

        cycles = {}
        for w in cl["ws"]:
            key = (w.get("client"), w.get("url"))
            cycles.setdefault(key, {}).setdefault(w.get("event"), []).append(w["t_epoch"])
        seg2s, seg3s = [], []
        for key, ev in cycles.items():
            opens = ev.get("open") or []
            firsts = ev.get("first-msg") or []
            if opens and firsts:
                fm = min(firsts)
                op = max([o for o in opens if o <= fm] or [min(opens)])
                seg2s.append(fm - op)
                later = [d for d in cl["decodable"] if d >= fm - 0.001]
                if later:
                    seg3s.append(later[0] - fm)
                else:
                    seg3s.append(None)
        seg["seg2_ws_open_first_msg_s"] = seg2s or None
        seg["seg3_join_first_frame_s"] = [x for x in seg3s if x is not None] or None
        seg["seg3_unresolved_cycles"] = sum(1 for x in seg3s if x is None)

        decodable = cl["decodable"]
        if decodable:
            seg["first_decodable_from_probe_install_s"] = decodable[0] - (cl["final"]["firstVfc"] or decodable[0])
            steady = [s for s in cl["samples"]]
            fps_vals = [s["vfc_fps"] for s in steady if s.get("vfc_fps") is not None and s.get("vfc_fps", 0) > 0]
            seg["steady_fps_median"] = med(fps_vals)
            seg["steady_fps_p05"] = pct(fps_vals, 5)
            stalls = 0
            for i in range(1, len(decodable)):
                if decodable[i] - decodable[i - 1] > 1.0:
                    stalls += 1
            seg["steady_stall_recoveries"] = stalls
        seg["teardown_ready_drops"] = len(cl["ready_drops"])
        seg["ws_cycles"] = len(cycles)
        out["clients"][c] = seg
    # events are context-level
    ev = [r for r in recs if r.get("kind") == "event"]
    off_clear = [r["t_epoch"] for r in ev if r.get("event") == "offline_cleared"]
    if off_clear and clients:
        t = off_clear[0]
        later = [d for cl in clients.values() if cl["decodable"] for d in cl["decodable"] if d >= t]
        out["post_offline_recovery_s"] = (min(later) - t) if later else None
    return out


def print_probe_report(results):
    rows = []
    for res in results:
        for c, seg in res["clients"].items():
            rows.append((res["label"] or os.path.basename(res.get("path", "?")), c, seg))
    print(f"{'file':<28} {'client':<7} {'seg1_page':>10} {'seg2_ws':>8} {'seg3_join':>9} "
          f"{'fps_med':>8} {'stalls':>6} {'tears':>6} {'wscyc':>5}")
    for label, c, seg in rows:
        s2 = seg.get("seg2_ws_open_first_msg_s")
        s3 = seg.get("seg3_join_first_frame_s")
        s2v = f"{med(s2):.3f}" if s2 else "-"
        s3v = f"{med(s3):.3f}" if s3 else (f"UNRES x{seg.get('seg3_unresolved_cycles', 0)}" if seg.get("seg3_unresolved_cycles") else "-")
        print(f"{label:<28} {c:<7} {fmt(seg.get('seg1_page_load_s')):>10} {s2v:>8} {s3v:>9} "
              f"{fmt(seg.get('steady_fps_median')):>8} {seg.get('steady_stall_recoveries', 0):>6} "
              f"{seg.get('teardown_ready_drops', 0):>6} {seg.get('ws_cycles', 0):>5}")
    for res in results:
        if "post_offline_recovery_s" in res:
            print(f"post-offline recovery ({res['label']}): {fmt(res['post_offline_recovery_s']) or 'NEVER'}")


def aggregate_probe(results):
    agg = {}
    for res in results:
        for c, seg in res["clients"].items():
            for key in ("seg1_page_load_s",):
                v = seg.get(key)
                if v is not None:
                    agg.setdefault(key, []).append(v)
            for key in ("seg2_ws_open_first_msg_s", "seg3_join_first_frame_s"):
                vs = seg.get(key)
                if vs:
                    agg.setdefault(key, []).extend(vs)
    print("\naggregate (n / median / P95):")
    for key, vals in agg.items():
        print(f"  {key:<32} n={len(vals):<3} median={med(vals):.3f}  p95={pct(vals, 95):.3f}")


# ---------------------------------------------------------------- legacy replay
def analyze_legacy(recs, label=""):
    meta = next((r for r in recs if r.get("kind") == "meta"), None)
    if not meta:
        return None
    t0 = meta["start_wall"]
    navs = [r for r in recs if r.get("kind") == "nav"]
    readies = [r for r in recs if r.get("kind") == "ready"]
    samples = [r for r in recs if r.get("kind") == "sample"]
    samples2 = [r for r in recs if r.get("kind") == "sample2"]
    events = [r for r in recs if r.get("kind") == "event"]

    def nav_step(step, client="c1"):
        r = next((x for x in navs if x.get("step") == step and x.get("client", "c1") == client), None)
        return r["t_epoch"] if r else None

    seg1 = None
    t_appsite = nav_step("appsite_frame")
    if t_appsite:
        seg1 = t_appsite - t0
    host_open = nav_step("host_player_open")
    seg23_bound = None
    seg23_quant = None
    if host_open and readies:
        seg23_bound = readies[0]["t_epoch"] - host_open
        if samples:
            first_s = samples[0].get("t_in_win", 0)
            seg23_quant = 10.0
    ready_ts = readies[0]["t_epoch"] if readies else None

    fps_vals, prev_c, prev_t, stalls, not_ready = [], None, None, 0, 0
    frozen_counts = []
    for s in samples:
        vfc = s.get("vfc") or {}
        c, t = vfc.get("count", 0), s["t_epoch"]
        v = s.get("video") or {}
        if v.get("ready", 0) < 2:
            not_ready += 1
        if prev_c is not None:
            dt = t - prev_t
            if dt > 0 and c > prev_c:
                fps_vals.append((c - prev_c) / dt)
            if c == prev_c:
                frozen_counts.append(t)
        prev_c, prev_t = c, t
        _ = frozen_counts
    vfc_last = prev_c

    post_off = None
    off_clear = next((e["t_epoch"] for e in events if e.get("event") == "offline_cleared"), None)
    offline_window = off_clear is not None
    if off_clear:
        pre_off_max = max(((x.get("vfc") or {}).get("count", 0) for x in samples if x["t_epoch"] < off_clear),
                          default=-1)
        resumed = next((s["t_epoch"] for s in samples
                        if s["t_epoch"] >= off_clear
                        and (s.get("vfc") or {}).get("count", 0) > pre_off_max), None)
        if resumed is None and ready_ts and ready_ts >= off_clear:
            resumed = ready_ts
        post_off = (resumed - off_clear) if resumed else None

    return {
        "label": label, "path": label,
        "seg1_page_load_s": seg1,
        "seg23_player_start_to_ready_bound_s": seg23_bound,
        "seg23_sampling_quantization_s": seg23_quant,
        "seg2_ws_open_first_msg_s": "UNRESOLVED-legacy-no-ws-instrumentation",
        "seg3_join_first_frame_s": "UNRESOLVED-legacy-no-ws-instrumentation",
        "ready_at_all": ready_ts is not None,
        "steady_fps_median": med(fps_vals) if fps_vals else None,
        "steady_not_ready_samples": not_ready,
        "steady_total_samples": len(samples),
        "post_offline_recovery_s": post_off,
        "offline_window": offline_window,
        "vfc_frozen_at_count": vfc_last,
        "dual_c2_samples": len(samples2),
    }


def print_legacy_report(results):
    print(f"{'file':<40} {'seg1_page':>10} {'pstart->ready':>14} {'quant':>6} {'fps_med':>8} "
          f"{'notready':>9} {'post-off-recov':>15}")
    for r in results:
        if not r:
            continue
        po = r["post_offline_recovery_s"]
        po_s = f"{po:.2f}" if po is not None else ("NEVER" if r.get("offline_window") else "-")
        print(f"{r['label']:<40} {fmt(r['seg1_page_load_s']):>10} "
              f"{fmt(r['seg23_player_start_to_ready_bound_s']):>14} "
              f"{fmt(r['seg23_sampling_quantization_s']):>6} "
              f"{fmt(r['steady_fps_median']):>8} "
              f"{r['steady_not_ready_samples']:>4}/{r['steady_total_samples']:<4} "
              f"{po_s:>15}")
    print("\nlegacy-format resolution limits (why v2 probe exists):")
    for r in results:
        if r:
            print(f"  {r['label']}: seg2={r['seg2_ws_open_first_msg_s']} seg3={r['seg3_join_first_frame_s']}")


# ---------------------------------------------------------------- selftest
def selftest():
    now = 1_800_000_000.0
    ok = [0]

    def check(name, got, want, tol=0.001):
        good = (got is not None and want is not None and abs(got - want) <= tol) or (got == want)
        ok[0] += 1 if good else 0
        print(f"  {'PASS' if good else 'FAIL'} {name}: got={got} want={want}")

    # trace 1: clean late join + one reconnect cycle that recovers
    recs = [
        {"kind": "meta", "start_wall": now, "dur": 60, "tag": "synthetic-clean", "dual": False},
        {"kind": "mark", "client": "c1", "mark": "nav_start", "t_epoch": now},
        {"kind": "mark", "client": "c1", "mark": "appsite_frame_attached", "t_epoch": now + 3.0},
        {"kind": "mark", "client": "c1", "mark": "appsite_video_present", "t_epoch": now + 3.5},
        {"kind": "ws", "client": "c1", "event": "created", "url": "ws://x/stream", "t_epoch": now + 3.6},
        {"kind": "ws", "client": "c1", "event": "open", "url": "ws://x/stream", "t_epoch": now + 4.0},
        {"kind": "ws", "client": "c1", "event": "first-msg", "url": "ws://x/stream", "t_epoch": now + 4.2},
        {"kind": "probe_final", "client": "c1", "probe": {
            "firstVfc": None, "vfcCount": 5,
            "decodableEvents": [
                {"t": (now + 4.45) * 1000, "how": "vfc"},
                {"t": (now + 44.0) * 1000, "how": "vfc"},
            ],
            "readyDrops": [{"t": (now + 30.0) * 1000, "from": 2, "to": 0}],
            "wv": [], "vfcTail": []}},
        {"kind": "sample", "client": "c1", "t_in_win": 10, "video": {"ready": 2, "w": 1280, "ct": 1.0, "paused": False},
         "vfc": {"count": 3, "last_at": None}, "vfc_fps": 25.0},
        {"kind": "sample", "client": "c1", "t_in_win": 11, "video": {"ready": 2, "w": 1280, "ct": 2.0, "paused": False},
         "vfc": {"count": 28, "last_at": None}, "vfc_fps": 25.0},
    ]
    res = analyze_probe(recs, "synthetic-clean")
    seg = res["clients"]["c1"]
    check("seg1 page load 3.5s", seg.get("seg1_page_load_s"), 3.5)
    check("seg2 ws 0.2s", med(seg.get("seg2_ws_open_first_msg_s") or []), 0.2)
    check("seg3 join 0.25s", med(seg.get("seg3_join_first_frame_s") or []), 0.25)
    check("teardown drop counted", seg.get("teardown_ready_drops"), 1)
    check("steady fps", seg.get("steady_fps_median"), 25.0)

    # trace 2: offline never recovers (E2b OFFLINE shape)
    recs2 = [
        {"kind": "meta", "start_wall": now, "dur": 180, "tag": "synthetic-offline", "dual": False},
        {"kind": "mark", "client": "c1", "mark": "nav_start", "t_epoch": now},
        {"kind": "mark", "client": "c1", "mark": "appsite_video_present", "t_epoch": now + 19.0},
        {"kind": "event", "event": "offline_set", "t_epoch": now + 90.0},
        {"kind": "event", "event": "offline_cleared", "t_epoch": now + 110.0},
        {"kind": "probe_final", "client": "c1", "probe": {
            "firstVfc": None, "vfcCount": 800,
            "decodableEvents": [{"t": (now + 30.0) * 1000, "how": "vfc"}],
            "readyDrops": [], "wv": [], "vfcTail": []}},
    ]
    res2 = analyze_probe(recs2, "synthetic-offline")
    check("seg1 19.0s", res2["clients"]["c1"].get("seg1_page_load_s"), 19.0)
    check("post-offline NEVER", res2.get("post_offline_recovery_s"), None)

    # trace 3: legacy replay shape (@604 OFFLINE)
    recs3 = [
        {"t_epoch": now, "kind": "meta", "start_wall": now, "dur": 180, "tag": "OFFLINE"},
        {"t_epoch": now + 8.2, "kind": "nav", "step": "login_done", "client": "c1"},
        {"t_epoch": now + 8.8, "kind": "nav", "step": "app_tab", "client": "c1"},
        {"t_epoch": now + 18.9, "kind": "nav", "step": "appsite_frame", "client": "c1"},
        {"t_epoch": now + 27.0, "kind": "nav", "step": "host_player_open", "client": "c1"},
        {"t_epoch": now + 37.0, "kind": "ready", "video": {"ready": 2, "w": 1280, "h": 720}},
        {"t_epoch": now + 37.0, "kind": "sample", "t_in_win": 10, "video": {"ready": 2, "w": 1280, "h": 720, "paused": False}, "vfc": {"count": 31, "lastMedia": 1.65}},
        {"t_epoch": now + 47.0, "kind": "sample", "t_in_win": 20, "video": {"ready": 2, "w": 1280, "h": 720, "paused": False}, "vfc": {"count": 126, "lastMedia": 7.19}},
        {"t_epoch": now + 90.1, "kind": "event", "event": "offline_set", "t_in_win": 90},
        {"t_epoch": now + 97.0, "kind": "sample", "t_in_win": 60, "video": {"ready": 2, "w": 1280, "h": 720, "paused": False}, "vfc": {"count": 510, "lastMedia": 32.7}},
        {"t_epoch": now + 110.1, "kind": "event", "event": "offline_cleared", "t_in_win": 110},
        {"t_epoch": now + 117.0, "kind": "sample", "t_in_win": 80, "video": {"ready": 0, "w": 0, "h": 0, "paused": True}, "vfc": {"count": 510, "lastMedia": 32.7}},
        {"t_epoch": now + 177.0, "kind": "sample", "t_in_win": 140, "video": {"ready": 0, "w": 0, "h": 0, "paused": True}, "vfc": {"count": 510, "lastMedia": 32.7}},
    ]
    res3 = analyze_legacy(recs3, "synthetic-legacy")
    check("legacy seg1 18.9s", res3["seg1_page_load_s"], 18.9)
    check("legacy pstart->ready bound 10s", res3["seg23_player_start_to_ready_bound_s"], 10.0)
    check("legacy post-offline NEVER", res3["post_offline_recovery_s"], None)

    print(f"\nselftest: {ok[0]}/10 checks passed")
    return 0 if ok[0] == 10 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--replay", action="store_true", help="legacy @604-era campaign jsonl")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        sys.exit(selftest())
    if not args.files:
        ap.error("need jsonl files (or --selftest)")
    results = []
    if args.replay:
        for p in args.files:
            r = analyze_legacy(load_jsonl(p), os.path.basename(p))
            r["path"] = p
            results.append(r)
        print_legacy_report(results)
    else:
        for p in args.files:
            r = analyze_probe(load_jsonl(p), os.path.basename(p))
            r["path"] = p
            results.append(r)
        print_probe_report(results)
        aggregate_probe(results)
    if args.json:
        print(json.dumps(results, indent=1, default=str))


if __name__ == "__main__":
    main()
