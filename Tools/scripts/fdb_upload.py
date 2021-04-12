#!/usr/bin/env python3

import argparse
import json
import subprocess
import tempfile
from typing import List

def upload_recording(recording_path: str, tests: List[str]) -> str:
    output_file = tempfile.NamedTemporaryFile()
    subprocess.run(["fdb", "replay", "share", "--json-output-file", output_file.name, recording_path])
    output = json.load(output_file)
    key = output["key"]
    return key

def upload_recordings_and_show_results(recording_metadata_file: str) -> None:
    with open(recording_metadata_file, "r") as f:
        metadata = json.load(f)

    for recording in metadata["recordings"]:
        tests = recording["tests"]
        recording_path = recording["recording_path"]

        print()
        print(f"Uploading recording for tests: {tests}")

        key = upload_recording(recording_path, tests)

        print("Upload complete.")
        print("To fetch and replay this recording, use the following command:")
        print(f"  fdb replay debug {key}") 

def main() -> None:
    parser = argparse.ArgumentParser(description="Upload recordings captured by fdb")
    parser.add_argument("recording_metadata_file", type=str, help="File containing info about captured recordings")

    args = parser.parse_args()

    upload_recordings_and_show_results(args.recording_metadata_file)

if __name__ == "__main__":
    main()
