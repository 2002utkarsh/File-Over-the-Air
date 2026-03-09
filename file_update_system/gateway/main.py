"""
FOTA Gateway Service — AWS IoT Core + UART Bridge

Connects to AWS IoT Core via MQTT to receive firmware update jobs,
downloads signed artifacts from S3, verifies SHA-256 integrity,
enforces SemVer compatibility, and stages binaries for the device.

Architecture:
    AWS IoT Core ──MQTT──► This Gateway ──UART──► Embedded Device
"""

import hashlib
import json
import logging
import os
import serial
import sys
import time
import re

import boto3
from packaging import version as semver

# Optional: AWS IoT Device SDK v2 for MQTT
try:
    from awscrt import io, mqtt
    from awsiot import mqtt_connection_builder, iotjobs
    AWSIOT_AVAILABLE = True
except ImportError:
    AWSIOT_AVAILABLE = False

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)
log = logging.getLogger("fota-gateway")

# ── Configuration ────────────────────────────────────────────────────────

# AWS IoT Core
IOT_ENDPOINT    = os.environ.get("AWS_IOT_ENDPOINT", "")        # e.g. abcdef-ats.iot.us-east-1.amazonaws.com
IOT_THING_NAME  = os.environ.get("AWS_IOT_THING_NAME", "fota-device")
IOT_CERT_PATH   = os.environ.get("AWS_IOT_CERT", "certs/device.pem.crt")
IOT_KEY_PATH    = os.environ.get("AWS_IOT_KEY",  "certs/private.pem.key")
IOT_CA_PATH     = os.environ.get("AWS_IOT_CA",   "certs/AmazonRootCA1.pem")

# S3 fallback (used when IoT Core is not configured)
S3_BUCKET       = os.environ.get("AWS_S3_BUCKET", "my-fota-project-bucket")

# UART connection to embedded device
UART_PORT       = os.environ.get("UART_PORT", "COM3")
UART_BAUD       = int(os.environ.get("UART_BAUD", "115200"))

# Local paths
DOWNLOAD_DIR    = "downloads"


# ── Device Communication (UART) ─────────────────────────────────────────

def query_device_version(port: str = UART_PORT, baud: int = UART_BAUD) -> str | None:
    """
    Query the device's firmware version via UART shell.
    Sends the 'version' command and parses the VERSION:<semver> response.
    Returns the version string or None on failure.
    """
    try:
        with serial.Serial(port, baud, timeout=3) as ser:
            # Clear any pending data
            ser.reset_input_buffer()

            # Send version command
            ser.write(b"\nversion\n")
            time.sleep(0.5)

            # Read and parse response
            response = ser.read(ser.in_waiting or 256).decode("utf-8", errors="replace")
            match = re.search(r"VERSION:(\d+\.\d+\.\d+)", response)
            if match:
                ver = match.group(1)
                log.info(f"Device reports version: {ver}")
                return ver
            else:
                log.warning(f"Could not parse version from device response: {response!r}")
                return None
    except serial.SerialException as e:
        log.error(f"UART error: {e}")
        return None


# ── SHA-256 Verification ─────────────────────────────────────────────────

def compute_sha256(file_path: str) -> str:
    """Compute the SHA-256 hex digest of a file."""
    h = hashlib.sha256()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_firmware(file_path: str, expected_sha256: str) -> bool:
    """Verify firmware integrity via SHA-256."""
    actual = compute_sha256(file_path)
    if actual == expected_sha256:
        log.info(f"SHA-256 verified: {actual}")
        return True
    else:
        log.error(f"SHA-256 MISMATCH!")
        log.error(f"  Expected: {expected_sha256}")
        log.error(f"  Actual:   {actual}")
        return False


# ── SemVer Dependency Logic ──────────────────────────────────────────────

def check_version_compatibility(
    device_version: str,
    latest_version: str,
    min_device_version: str | None = None
) -> dict:
    """
    Check if an update is needed and compatible.
    
    Returns:
        dict with keys:
            - update_available: bool
            - compatible: bool
            - reason: str
    """
    dev_ver = semver.parse(device_version)
    new_ver = semver.parse(latest_version)

    # No update if device is already at or ahead of cloud version
    if dev_ver >= new_ver:
        return {
            "update_available": False,
            "compatible": True,
            "reason": f"Device ({device_version}) is up to date"
        }

    # Check minimum device version requirement
    if min_device_version:
        min_ver = semver.parse(min_device_version)
        if dev_ver < min_ver:
            return {
                "update_available": True,
                "compatible": False,
                "reason": (
                    f"Incompatible: device ({device_version}) is below "
                    f"minimum required version ({min_device_version}). "
                    f"An intermediate update may be needed."
                )
            }

    return {
        "update_available": True,
        "compatible": True,
        "reason": f"Update available: {device_version} → {latest_version}"
    }


# ── S3 Manifest Polling (Fallback) ──────────────────────────────────────

def check_for_updates_s3():
    """
    Poll S3 for firmware updates (fallback when IoT Core is not configured).
    Includes SHA-256 verification and SemVer compatibility checks.
    """
    s3 = boto3.client("s3")
    
    log.info("─── FOTA Gateway (S3 Polling Mode) ───")
    
    # 1. Query device version via UART
    device_version = query_device_version()
    if not device_version:
        log.warning("Could not query device version, using fallback 0.0.0")
        device_version = "0.0.0"
    
    log.info(f"Device version: {device_version}")
    log.info(f"Checking updates from: {S3_BUCKET}")

    try:
        # 2. Fetch manifest
        log.info("Fetching manifest.json...")
        response = s3.get_object(Bucket=S3_BUCKET, Key="manifest.json")
        manifest = json.loads(response["Body"].read().decode("utf-8"))

        latest_ver = manifest.get("latest_version")
        sha256_hash = manifest.get("sha256")
        min_device_ver = manifest.get("min_device_version")
        firmware_s3_key = manifest.get("firmware_uri", "").split(f"s3://{S3_BUCKET}/")[-1]

        log.info(f"Cloud version:  {latest_ver}")

        # 3. Check version compatibility
        compat = check_version_compatibility(device_version, latest_ver, min_device_ver)
        
        if not compat["update_available"]:
            log.info(f"✅ {compat['reason']}")
            return
        
        if not compat["compatible"]:
            log.error(f"❌ {compat['reason']}")
            return

        log.info(f"📢 {compat['reason']}")

        # 4. Download firmware
        local_path = download_firmware(s3, firmware_s3_key, latest_ver)
        if not local_path:
            return

        # 5. Verify SHA-256
        if sha256_hash:
            if not verify_firmware(local_path, sha256_hash):
                log.error("❌ Firmware integrity check failed — aborting update")
                os.remove(local_path)
                return
        else:
            log.warning("⚠️  No SHA-256 hash in manifest — skipping verification")

        log.info("✅ Firmware ready for flashing")
        log.info(f"   Binary: {local_path}")

    except s3.exceptions.NoSuchKey:
        log.error("❌ manifest.json not found in bucket")
    except Exception as e:
        log.error(f"❌ Error: {e}")


# ── IoT Core MQTT Mode ──────────────────────────────────────────────────

def run_iot_core_gateway():
    """
    Connect to AWS IoT Core via MQTT and listen for firmware update jobs.
    Uses the IoT Jobs service for update orchestration.
    """
    if not AWSIOT_AVAILABLE:
        log.error("awsiotsdk not installed. Install via: pip install awsiotsdk")
        sys.exit(1)

    if not IOT_ENDPOINT:
        log.error("AWS_IOT_ENDPOINT not set")
        sys.exit(1)

    log.info("─── FOTA Gateway (IoT Core Mode) ───")
    log.info(f"Endpoint:  {IOT_ENDPOINT}")
    log.info(f"Thing:     {IOT_THING_NAME}")

    # Build MQTT connection
    event_loop_group = io.EventLoopGroup(1)
    host_resolver = io.DefaultHostResolver(event_loop_group)
    client_bootstrap = io.ClientBootstrap(event_loop_group, host_resolver)

    mqtt_connection = mqtt_connection_builder.mtls_from_path(
        endpoint=IOT_ENDPOINT,
        cert_filepath=IOT_CERT_PATH,
        pri_key_filepath=IOT_KEY_PATH,
        ca_filepath=IOT_CA_PATH,
        client_bootstrap=client_bootstrap,
        client_id=IOT_THING_NAME,
        clean_session=False,
        keep_alive_secs=30,
    )

    log.info("Connecting to AWS IoT Core...")
    connect_future = mqtt_connection.connect()
    connect_future.result()
    log.info("✅ Connected to AWS IoT Core")

    # Create IoT Jobs client
    jobs_client = iotjobs.IotJobsClient(mqtt_connection)

    # Subscribe to job notifications
    def on_job_received(response):
        """Handle incoming IoT Job (firmware update)."""
        if response.execution is None:
            log.info("No pending jobs")
            return

        job = response.execution
        job_id = job.job_id
        job_doc = job.job_document

        log.info(f"📢 Received job: {job_id}")
        log.info(f"   Document: {json.dumps(job_doc, indent=2)}")

        # Extract fields from job document
        s3_key = job_doc.get("s3_key", "")
        fw_version = job_doc.get("version", "")
        sha256_hash = job_doc.get("sha256", "")
        min_device_ver = job_doc.get("min_device_version")
        bucket = job_doc.get("bucket", S3_BUCKET)

        # Report IN_PROGRESS
        update_job_status(jobs_client, job_id, iotjobs.JobStatus.IN_PROGRESS)

        try:
            # Check device version
            device_version = query_device_version()
            if not device_version:
                device_version = "0.0.0"

            # SemVer compatibility check
            compat = check_version_compatibility(device_version, fw_version, min_device_ver)
            if not compat["compatible"]:
                log.error(f"❌ {compat['reason']}")
                update_job_status(jobs_client, job_id, iotjobs.JobStatus.FAILED,
                                  {"reason": compat["reason"]})
                return

            # Download firmware
            s3 = boto3.client("s3")
            local_path = download_firmware(s3, s3_key, fw_version)
            if not local_path:
                update_job_status(jobs_client, job_id, iotjobs.JobStatus.FAILED,
                                  {"reason": "Download failed"})
                return

            # Verify SHA-256
            if sha256_hash:
                if not verify_firmware(local_path, sha256_hash):
                    os.remove(local_path)
                    update_job_status(jobs_client, job_id, iotjobs.JobStatus.FAILED,
                                      {"reason": "SHA-256 mismatch"})
                    return

            log.info("✅ Firmware verified and ready")
            update_job_status(jobs_client, job_id, iotjobs.JobStatus.SUCCEEDED,
                              {"version": fw_version})

        except Exception as e:
            log.error(f"Job failed: {e}")
            update_job_status(jobs_client, job_id, iotjobs.JobStatus.FAILED,
                              {"reason": str(e)})

    # Subscribe to next job notification
    subscribe_future, _ = jobs_client.subscribe_to_next_changed_job_execution(
        request=iotjobs.NextJobExecutionChangedSubscriptionRequest(
            thing_name=IOT_THING_NAME
        ),
        qos=mqtt.QoS.AT_LEAST_ONCE,
        callback=on_job_received,
    )
    subscribe_future.result()
    log.info("Subscribed to IoT Jobs — waiting for updates...")

    # Request any pending job
    publish_future = jobs_client.publish_start_next_pending_job_execution(
        request=iotjobs.StartNextPendingJobExecutionRequest(
            thing_name=IOT_THING_NAME
        ),
        qos=mqtt.QoS.AT_LEAST_ONCE,
    )
    publish_future.result()

    # Keep alive
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("Shutting down...")
        disconnect_future = mqtt_connection.disconnect()
        disconnect_future.result()


def update_job_status(jobs_client, job_id, status, details=None):
    """Report job execution status back to IoT Core."""
    try:
        request = iotjobs.UpdateJobExecutionRequest(
            thing_name=IOT_THING_NAME,
            job_id=job_id,
            status=status,
            status_details={"info": json.dumps(details)} if details else None,
        )
        jobs_client.publish_update_job_execution(
            request, qos=mqtt.QoS.AT_LEAST_ONCE
        )
        log.info(f"Job {job_id}: status → {status}")
    except Exception as e:
        log.error(f"Failed to update job status: {e}")


# ── Firmware Download ────────────────────────────────────────────────────

def download_firmware(s3_client, s3_key: str, ver: str) -> str | None:
    """Download firmware binary from S3. Returns local path or None."""
    os.makedirs(DOWNLOAD_DIR, exist_ok=True)
    local_path = os.path.join(DOWNLOAD_DIR, f"firmware_{ver}.bin")

    log.info(f"Downloading {s3_key} → {local_path}")
    try:
        s3_client.download_file(S3_BUCKET, s3_key, local_path)
        size_kb = os.path.getsize(local_path) / 1024
        log.info(f"✅ Download complete ({size_kb:.1f} KB)")
        return local_path
    except Exception as e:
        log.error(f"❌ Download failed: {e}")
        return None


# ── Entry Point ──────────────────────────────────────────────────────────

if __name__ == "__main__":
    if IOT_ENDPOINT and AWSIOT_AVAILABLE:
        # Full IoT Core mode with MQTT job notifications
        run_iot_core_gateway()
    else:
        # Fallback: direct S3 manifest polling
        if not IOT_ENDPOINT:
            log.info("AWS_IOT_ENDPOINT not set — falling back to S3 polling mode")
        check_for_updates_s3()
