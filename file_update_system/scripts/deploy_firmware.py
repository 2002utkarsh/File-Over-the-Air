"""
FOTA Deployment Script — Upload Firmware + Manifest to AWS S3

Uploads a firmware binary to S3, computes its SHA-256 digest,
and creates/updates a manifest.json with version info, hash,
and SemVer dependency constraints.

Usage:
    python deploy_firmware.py --file firmware.bin --bucket my-bucket --version 1.1.0
    python deploy_firmware.py --file firmware.bin --bucket my-bucket --version 1.1.0 --min-version 1.0.0
    python deploy_firmware.py --file firmware.bin --bucket my-bucket --version 1.1.0 --dry-run
"""

import argparse
import hashlib
import json
import os
import sys
from datetime import datetime, timezone


def compute_sha256(file_path: str) -> str:
    """Compute the SHA-256 hex digest of a file."""
    h = hashlib.sha256()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def build_manifest(file_path: str, bucket_name: str, version: str,
                   min_version: str | None = None) -> dict:
    """Build the manifest dictionary with SHA-256 and SemVer metadata."""
    file_name = os.path.basename(file_path)
    s3_key = f"firmware/{version}/{file_name}"

    manifest = {
        "latest_version": version,
        "firmware_uri": f"s3://{bucket_name}/{s3_key}",
        "download_url": f"https://{bucket_name}.s3.amazonaws.com/{s3_key}",
        "release_date": datetime.now(timezone.utc).isoformat(),
        "size_bytes": os.path.getsize(file_path),
        "sha256": compute_sha256(file_path),
    }

    if min_version:
        manifest["min_device_version"] = min_version

    return manifest, s3_key


def upload_to_s3(file_path: str, bucket_name: str, version: str,
                 min_version: str | None = None, dry_run: bool = False):
    """
    Upload firmware binary and manifest to S3.
    With --dry-run, only prints the manifest without uploading.
    """
    if not os.path.exists(file_path):
        print(f"Error: Firmware file not found at {file_path}")
        sys.exit(1)

    manifest, s3_key = build_manifest(file_path, bucket_name, version, min_version)

    print("─── FOTA Deployment ───")
    print(f"Bucket:      {bucket_name}")
    print(f"Firmware:    {os.path.basename(file_path)}")
    print(f"Version:     {version}")
    print(f"SHA-256:     {manifest['sha256']}")
    print(f"Size:        {manifest['size_bytes']} bytes")
    if min_version:
        print(f"Min Version: {min_version}")

    if dry_run:
        print("\n[DRY RUN] — not uploading to S3")
        print("\nManifest:")
        print(json.dumps(manifest, indent=4))
        return

    import boto3
    s3 = boto3.client("s3")

    try:
        # Step 1: Upload firmware binary
        print(f"\nStep 1/2: Uploading binary to s3://{bucket_name}/{s3_key}...")
        s3.upload_file(file_path, bucket_name, s3_key)
        print("  → Success!")

        # Step 2: Upload manifest
        print("Step 2/2: Updating manifest.json...")
        s3.put_object(
            Bucket=bucket_name,
            Key="manifest.json",
            Body=json.dumps(manifest, indent=4),
            ContentType="application/json"
        )
        print("  → Success!")

        print("\n─── Deployment Complete ───")
        print("Manifest:")
        print(json.dumps(manifest, indent=4))

    except Exception as e:
        print(f"\nFAILED: {e}")
        sys.exit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Deploy FOTA Firmware to AWS S3")
    parser.add_argument("--file", required=True,
                        help="Path to the firmware binary (.bin)")
    parser.add_argument("--bucket", required=True,
                        help="AWS S3 Bucket Name")
    parser.add_argument("--version", required=True,
                        help="Firmware Version (SemVer, e.g. 1.0.1)")
    parser.add_argument("--min-version", default=None,
                        help="Minimum device version required for this update (SemVer)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print manifest without uploading to S3")

    args = parser.parse_args()
    upload_to_s3(args.file, args.bucket, args.version, args.min_version, args.dry_run)
