#!/usr/bin/env python3
"""
TraceX - Software Log Generator
Generates realistic logs with injected failure cascades for testing and evaluation.

Scenarios:
- db_timeout: Database connection timeout cascading into multiple API failures (500s)
- memory_leak: Gradual memory pressure leading to allocation failures, worker crashes, timeouts
- network_outage: Gateway / socket reset causing DB disconnects and downstream API errors
- auth_failure: Expired JWT / auth service error triggering cascading 401/403 API errors
- mixed: Normal background noise with multiple interleaved micro-outages

Options:
--lines: Total log lines (default 500)
--scenario: Failure scenario (db_timeout, memory_leak, network_outage, auth_failure, mixed)
--format: Output format: text (default), csv, json
--out: Output file path
--live: Append lines in real time (simulates active service monitoring)
--interval: Delay in seconds between live line emissions (default 1.0)
"""

import argparse
import datetime
import json
import random
import sys
import time

LEVELS = ["INFO", "WARN", "ERROR", "FATAL"]
SOURCES = ["DATABASE", "API", "NETWORK", "MEMORY", "AUTH", "CACHE", "DISK"]

NORMAL_MESSAGES = {
    "DATABASE": [
        "Connection pool active with 24 idle connections",
        "Executing SELECT * FROM users WHERE active = true",
        "Transaction committed successfully (txn_id=0x{hex:04x})",
        "Query completed in {ms}ms: SELECT count(*) FROM orders",
        "Read replica health check OK",
        "Periodic vacuum completed in {ms}ms"
    ],
    "API": [
        "GET /api/v1/health status=200 duration={ms}ms",
        "GET /api/v1/users/profile status=200 duration={ms}ms",
        "POST /api/v1/orders/checkout status=201 duration={ms}ms",
        "GET /api/v1/catalog/items?page=1 status=200 duration={ms}ms",
        "PUT /api/v1/settings status=200 duration={ms}ms",
        "GET /metrics scraped successfully"
    ],
    "NETWORK": [
        "TCP keep-alive acknowledged from peer 10.0.4.{ip}",
        "Outbound request to payment-gateway latency={ms}ms",
        "DNS resolution for cluster.internal completed in 2ms",
        "TLS handshake succeeded with partner API",
        "Mesh proxy sync completed",
        "Ingress packet rate within normal thresholds"
    ],
    "MEMORY": [
        "JVM heap usage at {pct}% of 4096MB",
        "Garbage collection pause young_gen: {ms}ms",
        "Buffer cache utilization normal",
        "Resident memory size stable at {mb}MB",
        "Arena allocator reclaimed {mb}MB"
    ],
    "AUTH": [
        "Token verification succeeded for user_id={uid}",
        "OAuth2 session refreshed for client_id=app-{uid}",
        "RBAC permissions validated for role=editor",
        "Session cookie validated",
        "Public key rotated successfully"
    ],
    "CACHE": [
        "Cache hit for key 'product:{uid}' (ttl=300s)",
        "Redis cluster slot verification OK",
        "Eviction policy LRU ran, 0 keys evicted",
        "Cache warm-up completed for top 100 items"
    ],
    "DISK": [
        "Write checkpoint flushed to disk in {ms}ms",
        "Volume /data free space at {pct}%",
        "Log rotation completed for access.log"
    ]
}

def format_event(ts: datetime.datetime, level: str, source: str, msg: str, fmt: str) -> str:
    ts_str = ts.strftime("%Y-%m-%d %H:%M:%S")
    if fmt == "csv":
        # escape double quotes if needed
        clean_msg = msg.replace('"', '""')
        return f'{ts_str},{level},{source},"{clean_msg}"'
    elif fmt == "json":
        return json.dumps({
            "ts": ts_str,
            "level": level,
            "source": source,
            "msg": msg
        })
    else: # text
        return f"{ts_str} {level} {source} {msg}"

def generate_normal_event(ts: datetime.datetime) -> tuple:
    source = random.choice(SOURCES)
    level_weights = [0.85, 0.12, 0.03, 0.00]
    level = random.choices(LEVELS, weights=level_weights)[0]
    
    template = random.choice(NORMAL_MESSAGES.get(source, ["System heartbeat OK"]))
    msg = template.format(
        ms=random.randint(5, 120),
        hex=random.randint(1000, 65535),
        ip=random.randint(2, 254),
        pct=random.randint(30, 65),
        mb=random.randint(250, 1800),
        uid=random.randint(10000, 99999)
    )
    if level == "WARN":
        msg = "Latency elevated: " + msg
    elif level == "ERROR":
        msg = "Transient error (retried): " + msg

    return ts, level, source, msg

def get_cascade_events(start_ts: datetime.datetime, scenario: str) -> list:
    events = []
    
    if scenario == "db_timeout":
        # Root cause: DB Connection timeout
        events.append((start_ts, "ERROR", "DATABASE", "Connection timeout: pool exhausted after 30000ms"))
        # Downstream cascades: API failures within 2-8 seconds
        events.append((start_ts + datetime.timedelta(seconds=1), "ERROR", "API", "GET /api/v1/orders/checkout returned 500 Internal Server Error (DB connection pool timeout)"))
        events.append((start_ts + datetime.timedelta(seconds=2), "ERROR", "API", "POST /api/v1/users/login returned 500 Database query execution failed"))
        events.append((start_ts + datetime.timedelta(seconds=3), "FATAL", "DATABASE", "Deadlock detected during rollback of transaction 0x9AF2"))
        events.append((start_ts + datetime.timedelta(seconds=4), "ERROR", "API", "GET /api/v1/catalog returned 503 Service Unavailable: DB backend down"))
        events.append((start_ts + datetime.timedelta(seconds=6), "ERROR", "API", "POST /api/v1/payments returned 500 Connection to replica database refused"))

    elif scenario == "memory_leak":
        # Root cause: Memory pressure
        events.append((start_ts, "WARN", "MEMORY", "High usage: Memory consumption exceeded 92% threshold"))
        events.append((start_ts + datetime.timedelta(seconds=2), "FATAL", "MEMORY", "OutOfMemory error: unable to allocate 128MB buffer heap exhausted"))
        # Downstream cascades:
        events.append((start_ts + datetime.timedelta(seconds=4), "ERROR", "DATABASE", "Query cancelled: server OutOfMemory during hash join"))
        events.append((start_ts + datetime.timedelta(seconds=5), "ERROR", "API", "GET /api/v1/reports returned 500 OutOfMemory worker thread crashed"))
        events.append((start_ts + datetime.timedelta(seconds=7), "FATAL", "API", "Process crash: SIGABRT received from native memory allocator"))
        events.append((start_ts + datetime.timedelta(seconds=9), "ERROR", "NETWORK", "Connection refused: backend service process terminated unexpectedly"))

    elif scenario == "network_outage":
        # Root cause: Network failure
        events.append((start_ts, "FATAL", "NETWORK", "Network interface eth0: Connection refused, packet loss 100% gateway unreachable"))
        # Downstream cascades:
        events.append((start_ts + datetime.timedelta(seconds=1), "ERROR", "DATABASE", "Connection refused: database host 10.0.1.50 unreachable"))
        events.append((start_ts + datetime.timedelta(seconds=2), "ERROR", "API", "POST /api/v1/sync returned 502 Bad Gateway: network socket error"))
        events.append((start_ts + datetime.timedelta(seconds=3), "ERROR", "NETWORK", "DNS failure: failed to resolve internal service mesh hostnames"))
        events.append((start_ts + datetime.timedelta(seconds=5), "ERROR", "API", "GET /api/v1/telemetry returned 504 Gateway Timeout socket reset by peer"))

    elif scenario == "auth_failure":
        # Root cause: Auth service failure / expired token cascade
        events.append((start_ts, "ERROR", "AUTH", "Token verification failed: RSA signature verification error / auth service unreachable"))
        # Downstream cascades:
        events.append((start_ts + datetime.timedelta(seconds=1), "ERROR", "API", "GET /api/v1/dashboard returned 401 Unauthorized token expired"))
        events.append((start_ts + datetime.timedelta(seconds=2), "ERROR", "API", "POST /api/v1/action returned 403 Forbidden invalid credentials"))
        events.append((start_ts + datetime.timedelta(seconds=4), "ERROR", "API", "GET /api/v1/settings returned 401 Unauthorized auth service rejected token"))
        events.append((start_ts + datetime.timedelta(seconds=5), "WARN", "AUTH", "Excessive 401 response burst detected: potential credential replay or key sync failure"))

    elif scenario == "mixed":
        # DB cascade followed later by Memory pressure
        events.extend(get_cascade_events(start_ts, "db_timeout"))
        events.extend(get_cascade_events(start_ts + datetime.timedelta(seconds=45), "memory_leak"))

    return events

def main():
    parser = argparse.ArgumentParser(description="TraceX Log Generator")
    parser.add_argument("--lines", type=int, default=500, help="Total log lines to generate")
    parser.add_argument("--scenario", choices=["db_timeout", "memory_leak", "network_outage", "auth_failure", "mixed"], default="db_timeout")
    parser.add_argument("--format", choices=["text", "csv", "json"], default="text", help="Log output format")
    parser.add_argument("--out", type=str, default="", help="Output file path (default stdout)")
    parser.add_argument("--live", action="store_true", help="Simulate live logging mode with real-time streaming")
    parser.add_argument("--interval", type=float, default=1.0, help="Interval in seconds for live logging")
    
    args = parser.parse_args()

    if args.out:
        import os
        dirname = os.path.dirname(args.out)
        if dirname:
            os.makedirs(dirname, exist_ok=True)
    out_file = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout

    start_time = datetime.datetime.now() - datetime.timedelta(seconds=args.lines * 2 if not args.live else 10)
    current_time = start_time

    # Position the cascade around 40% into the log stream
    cascade_line = int(args.lines * 0.4) if args.lines > 20 else 5
    cascade_events = get_cascade_events(current_time + datetime.timedelta(seconds=cascade_line * 2), args.scenario)
    cascade_idx = 0

    try:
        for i in range(args.lines):
            current_time += datetime.timedelta(seconds=random.randint(1, 3))
            
            # Check if we should emit a cascade event
            if i >= cascade_line and cascade_idx < len(cascade_events):
                ts, level, source, msg = cascade_events[cascade_idx]
                cascade_idx += 1
            else:
                ts, level, source, msg = generate_normal_event(current_time)
            
            line = format_event(ts, level, source, msg, args.format)
            out_file.write(line + "\n")
            out_file.flush()

            if args.live:
                time.sleep(args.interval)

    finally:
        if args.out and out_file != sys.stdout:
            out_file.close()

    if args.out:
        print(f"Generated {args.lines} lines of logs in {args.out} with scenario '{args.scenario}'")

if __name__ == "__main__":
    main()
