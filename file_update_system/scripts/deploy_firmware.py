import argparse
import boto3
import json
import os
import sys
from datetime import datetime

def upload_to_s3(file_path, bucket_name, version):
    """
    Uploads firmware binary and updates manifest.json in S3.
    """
    s3 = boto3.client('s3')
    
    if not os.path.exists(file_path):
        print(f"Error: Firmware file not found at {file_path}")
        sys.exit(1)
        
    file_name = os.path.basename(file_path)
    s3_key_firmware = f"firmware/{version}/{file_name}"
    s3_key_manifest = "manifest.json"
    
    print(f"--- FOTA Deployment Start ---")
    print(f"Target Bucket: {bucket_name}")
    print(f"Firmware: {file_name}")
    print(f"Version: {version}")
    
    try:
        print(f"Step 1/2: Uploading binary to s3://{bucket_name}/{s3_key_firmware}...")
        s3.upload_file(file_path, bucket_name, s3_key_firmware)
        print("  -> Success!")
        print(f"Step 2/2: Updating manifest.json...")
        
        manifest = {
            "latest_version": version,
            "firmware_uri": f"s3://{bucket_name}/{s3_key_firmware}",
            "download_url": f"https://{bucket_name}.s3.amazonaws.com/{s3_key_firmware}",
            "release_date": datetime.utcnow().isoformat() + "Z",
            "size_bytes": os.path.getsize(file_path)
        }
        
        s3.put_object(
            Bucket=bucket_name,
            Key=s3_key_manifest,
            Body=json.dumps(manifest, indent=4),
            ContentType='application/json'
        )
        print("  -> Success!")
        
        print("\n--- Deployment Complete ---")
        print("Manifest Content:")
        print(json.dumps(manifest, indent=4))
        
    except Exception as e:
        print(f"\nFAILED: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Deploy FOTA Firmware to AWS S3')
    parser.add_argument('--file', required=True, help='Path to the firmware binary (.bin)')
    parser.add_argument('--bucket', required=True, help='AWS S3 Bucket Name')
    parser.add_argument('--version', required=True, help='Firmware Version (e.g., 1.0.1)')
    
    args = parser.parse_args()
    
    upload_to_s3(args.file, args.bucket, args.version)
