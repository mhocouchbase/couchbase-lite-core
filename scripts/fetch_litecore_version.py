#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
A script for fetching prebuilt LiteCore artifacts from the Couchbase build artifact server.

REQUIREMENTS: Git must be installed on the machine this script is run on

This script can be extended in the following way:

Create a file called "platform_fetch.py" with a function subdirectory_for_variant(os: str, abi: str) inside of it.
This function should examine the os / abi combination and return a relative directory to use that will be appended
to the output directory when a download occurs.  This file can either be placed in the same directory as this
script, or the path to its parent directory passed in via the --ext-path argument.

Here is a list of the current values you can expect from each variant:
|       VARIANT       |    OS    |      ABI      |
| ------------------- | -------- | ------------- |
| android-x86_64      | android  | x86_64        |
| android-x86         | android  | x86           |
| android-armeabi-v7a | android  | armeabi-v7a   |
| android-arm64-v8a   | android  | arm64-v8a     |
| centos6             | centos6  | x86_64        |
| linux               | linux    | x86_64        |
| macosx              | macos    | x86_64        |
| ios                 | ios      | <empty>       | <-- multiple architectures all in one
| windows-arm-store   | windows  | arm-store     |
| windows-win32       | windows  | x86           |
| windows-win32-store | windows  | x86-store     |
| windows-win64       | windows  | x86_64        |
| windows-win64-store | windows  | x86_64-store  |
"""

import argparse
import os
from fetch_litecore_base import download_variant, VALID_PLATFORMS, resolve_platform_path, import_platform_extensions, calculate_variants, check_variant, conditional_print, set_quiet
from git import Repo

def validate_build(build: str):
    if build == None:
        print("Build is None, aborting...")
        exit(1)

    build_parts = build.split('-')
    if len(build_parts) < 2:
        print(f"!!! Malformed build {build}.  Must be of the form 3.1.0-97 or 3.1.0-97-EE")
        exit(1)

    return build_parts

def get_cbl_build(repo: str) -> str:
    ce_repo = Repo(repo)
    build = ""
    for line in ce_repo.commit().message.splitlines():
        if line.startswith("Build-To-Use:"):
            build = line.split(":")[1].strip()

    return build

def download_litecore(variants, debug: bool, dry: bool, build: str, repo: str, ee: bool, output_path: str) -> int:
    download_folder = ""
    if build is None:
        build = get_cbl_build(repo)
        if ee:
            build += "-EE"
    
    build_parts = validate_build(build)
    download_folder = f"http://latestbuilds.service.couchbase.com/builds/latestbuilds/couchbase-lite-core/{build_parts[0]}/{build_parts[1]}"
    conditional_print(f"--- Using URL {download_folder}/<filename>")
    
    failed_count = 0
    for v in variants:
        if dry:
            failed_count += check_variant(download_folder, v, build, debug, output_path)
        else:
            failed_count += download_variant(download_folder, v, build, debug, output_path)

    return failed_count

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Fetch a specific prebuilt LiteCore by build version')
    parser.add_argument('-v', '--variants', nargs='+', type=str, help='A space separated list of variants to download', required=True, choices=VALID_PLATFORMS, metavar="PLATFORM")
    parser.add_argument('-d', '--debug', action='store_true', help='If specified, download debug variants')
    parser.add_argument('-D', '--dry-run', action='store_true', help='Check for existience of indicated artifacts, but do not perform download')
    parser.add_argument('-b', '--build', type=str, help="The build version to download (e.g. 3.1.0-97 or 3.1.0-97-EE).  Required if repo is not specified.")
    parser.add_argument('--ee', action='store_true', help="If specified, download the enterprise variant of LiteCore")
    parser.add_argument('-r', '--repo', type=str, help="The path to the CE LiteCore repo.  Required if build not specified.")
    parser.add_argument('-x', '--ext-path', type=str, help="The path in which the platform specific extensions to this script are defined (platform_fetch.py).  If a relative path is passed, it will be relative to fetch_litecore.py.  By default it is the current working directory.",
        default=os.getcwd())
    parser.add_argument('-o', '--output', type=str, help="The directory in which to save the downloaded artifacts", default=os.getcwd())
    parser.add_argument('-q', '--quiet', action='store_true', help="Suppress all output except during dry run.")

    args = parser.parse_args()

    if not args.build and not args.repo:
        print("!!! Neither CE repo path nor build defined, aborting...")
        parser.print_usage()
        exit(-1)

    full_path = resolve_platform_path(args.ext_path)
    import_platform_extensions(full_path)

    final_variants = calculate_variants(args.variants)
    set_quiet(args.quiet)
    exit(download_litecore(final_variants, args.debug, args.dry_run, args.build, args.repo, args.ee, args.output))