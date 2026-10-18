"""Resolve explicit native build inputs without installing numeric dependencies."""
import hashlib
from pathlib import Path
import platform
import shutil
import subprocess


def identity(path):
    path = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return {'path': str(path), 'sha256': digest.hexdigest()}


def executable(value):
    selected = shutil.which(str(value))
    if selected is None:
        raise RuntimeError('Executable was not found: ' + str(value))
    return Path(selected).resolve(strict=True)


def output(command):
    return subprocess.check_output(list(map(str, command)), text=True, timeout=10).strip()


def arguments(parser):
    parser.add_argument('--cc', help='C compiler executable; defaults to xcrun clang')
    parser.add_argument('--sdk', type=Path, help='Exact macOS SDK directory; defaults to xcrun macosx SDK')
    parser.add_argument('--mpfr-prefix', type=Path, help='Installed MPFR prefix containing include/ and lib/libmpfr.a')
    parser.add_argument('--gmp-prefix', type=Path, help='Installed GMP prefix containing include/ and lib/libgmp.a')
    parser.add_argument('--pkg-config', default='pkg-config', help='Discovery executable used for omitted numeric prefixes')


def resolve(args):
    if platform.system() != 'Darwin':
        raise RuntimeError('The qualified native static worker profile requires macOS')
    compiler = executable(args.cc) if args.cc else Path(output(['xcrun', '--find', 'clang'])).resolve(strict=True)
    sdk = (args.sdk if args.sdk else Path(output(['xcrun', '--sdk', 'macosx', '--show-sdk-path']))).resolve(strict=True)
    if not sdk.is_dir():
        raise ValueError('SDK selection must be a directory')
    settings = next((sdk / name for name in ('SDKSettings.json', 'SDKSettings.plist') if (sdk / name).is_file()), None)
    if settings is None:
        raise ValueError('Selected SDK has no identity settings file')
    discovery = []
    numeric = {}
    pkg = None
    for name in ('mpfr', 'gmp'):
        prefix = getattr(args, name + '_prefix')
        if prefix is not None:
            prefix = prefix.resolve(strict=True)
            include, library = prefix / 'include', prefix / 'lib'
            selection = {'kind': 'explicit-prefix', 'prefix': str(prefix)}
        else:
            if pkg is None:
                pkg = executable(args.pkg_config)
            include = Path(output([pkg, '--variable=includedir', name])).resolve(strict=True)
            library = Path(output([pkg, '--variable=libdir', name])).resolve(strict=True)
            selection = {'kind': 'pkg-config', 'package': name,
                         'version': output([pkg, '--modversion', name])}
        header = (include / (name + '.h')).resolve(strict=True)
        archive = (library / ('lib' + name + '.a')).resolve(strict=True)
        if not header.is_file() or not archive.is_file():
            raise ValueError('Numeric selection must provide a regular header and static archive: ' + name)
        numeric[name] = {'include': header.parent, 'header': header, 'archive': archive}
        discovery.append({'name': name, 'selection': selection,
                          'header': identity(header), 'archive': identity(archive)})
    tools = [compiler, settings, Path(__file__)] + ([pkg] if pkg else [])
    return {'compiler': compiler, 'sdk': sdk, 'numeric': numeric,
            'identities': [identity(path) for path in tools],
            'selection': {'numeric': discovery, 'sdkSettings': identity(settings),
                          'pkgConfig': identity(pkg) if pkg else None}}


def unchanged(identities):
    if any(identity(item['path'])['sha256'] != item['sha256'] for item in identities):
        raise RuntimeError('Selected native build input changed during compilation')
