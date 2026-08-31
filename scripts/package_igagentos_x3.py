#!/usr/bin/env python3
"""Create a versioned iGAgentOS X3 wireless/SD-card update package."""

import argparse
import configparser
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from datetime import date
from pathlib import Path
from urllib.parse import urlsplit

from package_nightly_target import EXPECTED_PARTITIONS, verify_firmware


ESP32_C3_CHIP_ID = 0x0005
X3_X4_BOARD_TAG = 'x4'
BUILD_ENVIRONMENT = 'gh_release'
SEMVER = re.compile(r'^[0-9]+\.[0-9]+\.[0-9]+$')
SAFE_LABEL = re.compile(r'^[A-Za-z0-9][A-Za-z0-9.+-]*$')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def git_value(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


def git_is_dirty(root):
    status = subprocess.check_output(
        ['git', 'status', '--porcelain'], cwd=root, text=True
    ).strip()
    return bool(status)


def read_product_config(root):
    config = configparser.ConfigParser()
    if not config.read(root / 'platformio.ini'):
        raise SystemExit(f'missing platformio.ini under {root}')
    try:
        product_name = config['igagentos']['name']
        product_version = config['igagentos']['version']
        base_version = config['crosspoint']['version']
    except KeyError as error:
        raise SystemExit(f'missing firmware version field: {error}') from error
    if product_name != 'iGAgentOS':
        raise SystemExit(f'unsupported product name: {product_name!r}')
    if not SEMVER.fullmatch(product_version):
        raise SystemExit(f'iGAgentOS version must be SemVer x.y.z: {product_version!r}')
    return product_name, product_version, base_version


def validate_development_origin(origin):
    if origin is None:
        return None
    parsed = urlsplit(origin)
    if (
        parsed.scheme not in {'http', 'https'}
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or parsed.path not in {'', '/'}
    ):
        raise SystemExit(f'invalid development origin: {origin!r}')
    return origin.rstrip('/')


def firmware_contains(path, markers):
    remaining = set(markers)
    overlap = b''
    max_marker_size = max(len(marker) for marker in remaining)
    with path.open('rb') as stream:
        while chunk := stream.read(64 * 1024):
            data = overlap + chunk
            remaining = {marker for marker in remaining if marker not in data}
            if not remaining:
                return True
            overlap = data[-(max_marker_size - 1):]
    return False


def write_instructions(path, product_name, product_version, development_origin):
    development_text = (
        f'本包已附带开发服务地址：{development_origin}\n'
        if development_origin
        else '本包未附带开发服务地址；如需本地联调，请在 SD 卡根目录创建 igrowth-development.txt。\n'
    )
    path.write_text(
        f'''{product_name} X3 v{product_version} 刷机说明（无需读卡器）

1. 设备：Xteink X3（与 X4 共用 ESP32-C3 固件）。
2. 在设备打开“文件传输 → 加入网络”，让电脑和设备连接同一可信 Wi-Fi。
3. 浏览器打开设备屏幕显示的 IP，进入文件管理，把 firmware.bin 上传到 SD 卡根目录；同名文件会覆盖。也可运行：curl -f -F "file=@firmware.bin" "http://<设备IP>/upload?path=/"
4. 返回设备，进入“设置 → SD 卡固件更新”，选择 firmware.bin 并确认。普通设置更新也可选择本包中的 iGAgentOS-X3-v{product_version}.bin；firmware.bin 是标准文件名，也是恢复刷机约定名。
5. iGAgentOS-X3-v{product_version}.bin 与 firmware.bin 内容完全相同，只用于归档和区分版本。
6. 刷机前可运行：shasum -a 256 -c SHA256SUMS.txt
7. 刷机时保持电量充足，不要断电；设备会自动重启。启动页和“设置 → 关于”应显示 {product_name} / {product_version}。
8. 文件传输服务没有登录验证，只能在可信私有网络使用；上传完成后退出“文件传输”。

{development_text}开发地址必须是同一局域网可访问的 http://<私网IP>:<端口>，例如 http://192.168.1.20:2048。可用同一“文件传输”网页把 igrowth-development.txt 上传到 SD 卡根目录，无需读卡器。在 AirPage 设置里选择“开发”即可连接该地址；选择“线上”则使用 https://igrowth.cc。

完整说明：https://github.com/0x1abin/crossmux/blob/main/docs/igagentos-x3-operations.md
''',
        encoding='utf-8',
    )


def package(
    *,
    root,
    firmware,
    output_root,
    package_date,
    git_commit,
    source_dirty=False,
    development_origin=None,
    rollback=None,
    rollback_label=None,
):
    root = Path(root).resolve()
    firmware = Path(firmware).resolve()
    output_root = Path(output_root).resolve()
    rollback = Path(rollback).resolve() if rollback else None
    product_name, product_version, base_version = read_product_config(root)

    if not re.fullmatch(r'[0-9]{8}', package_date):
        raise SystemExit(f'package date must use YYYYMMDD: {package_date!r}')
    if not re.fullmatch(r'[0-9a-f]{40}', git_commit):
        raise SystemExit('git commit must be a 40-character lowercase SHA')
    development_origin = validate_development_origin(development_origin)
    if bool(rollback) != bool(rollback_label):
        raise SystemExit('--rollback and --rollback-label must be provided together')
    if rollback_label and not SAFE_LABEL.fullmatch(rollback_label):
        raise SystemExit(f'invalid rollback label: {rollback_label!r}')
    if not firmware.is_file():
        raise SystemExit(f'missing firmware: {firmware}')
    if rollback and not rollback.is_file():
        raise SystemExit(f'missing rollback firmware: {rollback}')
    if firmware.stat().st_size > EXPECTED_PARTITIONS['app0'][1]:
        raise SystemExit('firmware exceeds the X3/X4 OTA app slot')

    verify_firmware(firmware, ESP32_C3_CHIP_ID, X3_X4_BOARD_TAG)
    identity_markers = {product_name.encode(), product_version.encode()}
    if not firmware_contains(firmware, identity_markers):
        raise SystemExit(f'firmware does not embed {product_name} {product_version}')

    package_name = f'{product_name}-X3-v{product_version}-{package_date}'
    output_root.mkdir(parents=True, exist_ok=True)
    output = output_root / package_name
    if output.exists():
        raise SystemExit(f'package already exists; bump the version before rebuilding: {output}')

    temporary = Path(tempfile.mkdtemp(prefix=f'.{package_name}-', dir=output_root))
    try:
        sd_firmware = temporary / 'firmware.bin'
        versioned_firmware = temporary / f'{product_name}-X3-v{product_version}.bin'
        shutil.copyfile(firmware, sd_firmware)
        shutil.copyfile(firmware, versioned_firmware)

        if development_origin:
            (temporary / 'igrowth-development.txt').write_text(
                development_origin + '\n', encoding='utf-8'
            )

        rollback_asset = None
        if rollback:
            rollback_name = f'{product_name}-X3-rollback-{rollback_label}.bin'
            rollback_path = temporary / rollback_name
            shutil.copyfile(rollback, rollback_path)
            rollback_asset = {
                'file': rollback_name,
                'size': rollback_path.stat().st_size,
                'sha256': sha256(rollback_path),
            }

        manifest_name = f'{product_name}-X3-v{product_version}-manifest.json'
        manifest = {
            'schemaVersion': 1,
            'package': package_name,
            'packageDate': package_date,
            'product': {'name': product_name, 'version': product_version},
            'target': 'Xteink X3',
            'compatibleModels': ['xteink_x3', 'xteink_x4'],
            'buildEnvironment': BUILD_ENVIRONMENT,
            'baseFirmware': {'name': 'CrossMux', 'version': base_version},
            'gitCommit': git_commit,
            'sourceDirty': source_dirty,
            'firmware': {
                'sdCardFile': sd_firmware.name,
                'archiveFile': versioned_firmware.name,
                'size': sd_firmware.stat().st_size,
                'sha256': sha256(sd_firmware),
            },
        }
        if development_origin:
            manifest['developmentOrigin'] = development_origin
        if rollback_asset:
            manifest['rollback'] = rollback_asset
        (temporary / manifest_name).write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
        )

        write_instructions(
            temporary / '刷机说明.txt',
            product_name,
            product_version,
            development_origin,
        )
        checksum_paths = sorted(
            (path for path in temporary.iterdir() if path.name != 'SHA256SUMS.txt'),
            key=lambda path: path.name,
        )
        (temporary / 'SHA256SUMS.txt').write_text(
            ''.join(f'{sha256(path)}  {path.name}\n' for path in checksum_paths),
            encoding='utf-8',
        )
        temporary.replace(output)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return output


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, default=root / '.pio/build/gh_release/firmware.bin')
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--date', default=date.today().strftime('%Y%m%d'))
    parser.add_argument('--development-origin')
    parser.add_argument('--rollback', type=Path)
    parser.add_argument('--rollback-label')
    args = parser.parse_args()
    output = package(
        root=root,
        firmware=args.firmware,
        output_root=args.output_root,
        package_date=args.date,
        git_commit=git_value(root, 'rev-parse', 'HEAD'),
        source_dirty=git_is_dirty(root),
        development_origin=args.development_origin,
        rollback=args.rollback,
        rollback_label=args.rollback_label,
    )
    print(output)


if __name__ == '__main__':
    main()
