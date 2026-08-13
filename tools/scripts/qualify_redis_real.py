#!/usr/bin/env python3
"""Qualify Toka's Redis client against real loopback services.

This is interoperability evidence, not a replacement for the deterministic
protocol fixtures.  It deliberately fails closed: a runner that cannot bind
loopback ports through Docker reports ``not-run`` and exits 2, never pass.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
REDIS_VERSIONS = ("7.4-alpine", "8.2-alpine")
PASSWORD = "toka-password"


class QualificationFailure(RuntimeError):
    pass


def command(args: list[str], *, cwd: Path | None = None, timeout: int = 120) -> str:
    completed = subprocess.run(
        args,
        cwd=cwd or ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    if completed.returncode:
        raise QualificationFailure(
            f"command failed ({completed.returncode}): {' '.join(args)}\n{completed.stdout}"
        )
    return completed.stdout.strip()


def cleanup_command(args: list[str]) -> None:
    subprocess.run(args, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def record(report_path: Path | None, payload: dict[str, Any]) -> None:
    if report_path is None:
        return
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def runner_prerequisite(tokac: Path) -> tuple[bool, str]:
    if not tokac.is_file() or not os.access(tokac, os.X_OK):
        return False, f"Toka compiler is not executable: {tokac}"
    if shutil.which("docker") is None:
        return False, "Docker CLI is not installed"
    if shutil.which("openssl") is None:
        return False, "openssl is not installed"
    try:
        return True, command(["docker", "version", "--format", "{{.Server.Version}}"], timeout=15)
    except QualificationFailure as error:
        return False, f"Docker daemon is unavailable or cannot publish loopback ports: {error}"


def compile_fixture(tokac: Path, package_include: str, source: str, output: Path) -> None:
    command(
        [
            str(tokac),
            "-I",
            "lib",
            "-I",
            package_include,
            source,
            "-o",
            str(output),
        ],
        timeout=180,
    )


def published_port(container: str, container_port: int) -> int:
    published = command(["docker", "port", container, f"{container_port}/tcp"], timeout=20)
    line = published.splitlines()[0].strip()
    try:
        host, port = line.rsplit(":", 1)
        if host not in {"127.0.0.1", "[::1]"}:
            raise ValueError("not a loopback publication")
        return int(port)
    except ValueError as error:
        raise QualificationFailure(f"unexpected Docker loopback mapping {line!r}: {error}") from error


def wait_ready(args: list[str], *, attempts: int = 40) -> None:
    last_error = ""
    for _ in range(attempts):
        completed = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if completed.returncode == 0:
            return
        last_error = completed.stdout.strip()
        time.sleep(0.25)
    raise QualificationFailure(f"service did not become ready: {' '.join(args)}\n{last_error}")


def make_certificates(work: Path) -> tuple[Path, Path, Path]:
    # Redis runs as a container user that cannot traverse TemporaryDirectory's
    # default 0700 mode. These are one-run test certificates only.
    work.chmod(0o755)
    ca_key = work / "ca.key"
    ca_cert = work / "ca.crt"
    server_key = work / "server.key"
    server_csr = work / "server.csr"
    server_cert = work / "server.crt"
    extensions = work / "server.ext"
    extensions.write_text(
        "[v3_req]\n"
        "subjectAltName=DNS:localhost,IP:127.0.0.1\n"
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=serverAuth\n",
        encoding="utf-8",
    )
    command([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes",
        "-keyout", str(ca_key), "-out", str(ca_cert), "-days", "1",
        "-subj", "/CN=toka-real-service-test-ca",
    ])
    command([
        "openssl", "req", "-newkey", "rsa:2048", "-sha256", "-nodes",
        "-keyout", str(server_key), "-out", str(server_csr),
        "-subj", "/CN=localhost",
    ])
    command([
        "openssl", "x509", "-req", "-in", str(server_csr),
        "-CA", str(ca_cert), "-CAkey", str(ca_key), "-CAcreateserial",
        "-out", str(server_cert), "-days", "1", "-sha256",
        "-extfile", str(extensions), "-extensions", "v3_req",
    ])
    ca_cert.chmod(0o644)
    server_cert.chmod(0o644)
    server_key.chmod(0o644)
    return ca_cert, server_cert, server_key


def docker_name(prefix: str) -> str:
    return f"toka-{prefix}-{uuid.uuid4().hex[:12]}"


def service_log(container: str) -> str:
    completed = subprocess.run(["docker", "logs", container], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    return completed.stdout[-8000:]


def qualify_redis(
    fixture: Path, version: str, *, tls: bool, certificate_dir: Path, ca_cert: Path
) -> dict[str, str]:
    name = docker_name("redis")
    image = f"redis:{version}"
    try:
        run_args = [
            "docker", "run", "-d", "--name", name,
            "-p", "127.0.0.1::6379",
            "-v", f"{certificate_dir}:/certs:ro",
            image, "redis-server", "--bind", "0.0.0.0", "--protected-mode", "no",
            "--requirepass", PASSWORD,
        ]
        if tls:
            run_args += [
                "--port", "0", "--tls-port", "6379",
                "--tls-cert-file", "/certs/server.crt",
                "--tls-key-file", "/certs/server.key",
                "--tls-ca-cert-file", "/certs/ca.crt",
                "--tls-auth-clients", "no",
            ]
        else:
            run_args += ["--port", "6379"]
        command(run_args, timeout=180)
        if tls:
            wait_ready([
                "docker", "exec", name, "redis-cli", "--tls", "--cacert", "/certs/ca.crt",
                "-a", PASSWORD, "PING",
            ])
        else:
            wait_ready(["docker", "exec", name, "redis-cli", "-a", PASSWORD, "PING"])
        port = published_port(name, 6379)
        command([str(fixture), str(port), PASSWORD, str(ca_cert) if tls else "-"], timeout=60)
        version_text = command(["docker", "exec", name, "redis-server", "--version"], timeout=20)
        return {"server": version_text, "transport": "tls-private-ca" if tls else "tcp-auth"}
    except Exception as error:
        raise QualificationFailure(f"Redis {version} ({'TLS' if tls else 'TCP'}) failed: {error}\n{service_log(name)}") from error
    finally:
        cleanup_command(["docker", "rm", "-f", name])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tokac", type=Path, default=ROOT / "build/bin/tokac")
    parser.add_argument("--report", type=Path, help="write a JSON evidence report")
    args = parser.parse_args()
    report_path = args.report.resolve() if args.report else None
    report: dict[str, Any] = {
        "contract": "redis-real-service-v1",
        "redis_images": list(REDIS_VERSIONS),
        "status": "not-run",
    }

    eligible, prerequisite = runner_prerequisite(args.tokac.resolve())
    if not eligible:
        report["reason"] = prerequisite
        record(report_path, report)
        print(f"NOT RUN: {prerequisite}", file=sys.stderr)
        return 2
    report["docker_server"] = prerequisite

    try:
        with tempfile.TemporaryDirectory(prefix="toka-redis-real-") as temporary:
            work = Path(temporary)
            redis_fixture = work / "redis-real-service-v1"
            redis_clone_fixture = work / "redis-clone-ownership-v1"
            compile_fixture(args.tokac.resolve(), "official/redis/lib", "official/redis/tests/real_service_v1.tk", redis_fixture)
            compile_fixture(args.tokac.resolve(), "official/redis/lib", "official/redis/tests/clone_ownership_v1.tk", redis_clone_fixture)
            command([str(redis_clone_fixture)], timeout=60)
            ca_cert, _, _ = make_certificates(work)
            redis_evidence: dict[str, dict[str, str]] = {}
            for version in REDIS_VERSIONS:
                redis_evidence[f"{version}:tcp-auth"] = qualify_redis(
                    redis_fixture, version, tls=False, certificate_dir=work, ca_cert=ca_cert
                )
                redis_evidence[f"{version}:tls-private-ca"] = qualify_redis(
                    redis_fixture, version, tls=True, certificate_dir=work, ca_cert=ca_cert
                )
        report["redis"] = redis_evidence
        report["status"] = "passed"
        record(report_path, report)
        print("PASS: real Redis compatibility matrix")
        return 0
    except Exception as error:
        report["status"] = "failed"
        report["reason"] = str(error)
        record(report_path, report)
        print(f"FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
