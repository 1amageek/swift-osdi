#!/usr/bin/env python3
"""Build the pinned external compiler profile with bounded process ownership."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

PROFILE = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def observe(command, cwd, environment=None):
    return subprocess.check_output(command, cwd=cwd, env=environment, text=True,
                                   stderr=subprocess.STDOUT, timeout=30).strip()


def stop_group(process):
    for sig, grace in [(signal.SIGTERM, 3), (signal.SIGKILL, 5)]:
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=grace)
            return
        except subprocess.TimeoutExpired:
            continue
    raise RuntimeError('Owned compiler process group did not stop')


def bounded(command, cwd, environment, output, label, timeout, minimum_free_bytes):
    start = time.monotonic()
    stdout_path, stderr_path = output/f'{label}.stdout.log', output/f'{label}.stderr.log'
    observations = []
    reason = None
    with stdout_path.open('w') as stdout, stderr_path.open('w') as stderr:
        process = subprocess.Popen(command, cwd=cwd, env=environment, stdout=stdout,
                                   stderr=stderr, start_new_session=True)
        try:
            while process.poll() is None:
                elapsed = time.monotonic() - start
                free = shutil.disk_usage(output).free
                observations.append({'elapsedSeconds':elapsed, 'freeBytes':free})
                if free < minimum_free_bytes:
                    reason = 'minimum-free-storage'
                    break
                if elapsed >= timeout:
                    reason = 'timeout'
                    break
                try:
                    process.wait(timeout=min(2, timeout-elapsed))
                except subprocess.TimeoutExpired:
                    pass
        finally:
            if process.poll() is None:
                stop_group(process)
    return {'command':list(map(str,command)), 'workingDirectory':str(cwd),
            'timeoutSeconds':timeout, 'minimumFreeBytes':minimum_free_bytes,
            'exitCode':process.returncode, 'refusal':reason,
            'elapsedSeconds':time.monotonic()-start, 'observations':observations,
            'stdout':str(stdout_path), 'stderr':str(stderr_path)}


def source_snapshot(source):
    entries = subprocess.check_output(['git','ls-files','--stage','-z'], cwd=source).split(b'\0')
    result = {}
    for entry in entries:
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        mode, identity, stage = metadata.split()
        require(stage == b'0', 'Source index has an unresolved merge')
        name = os.fsdecode(raw_name)
        result[name] = 'gitlink:'+identity.decode() if mode == b'160000' else sha256(source/name)
    return result


def self_check():
    with tempfile.TemporaryDirectory(prefix='openvaf-build-driver-') as name:
        output = Path(name)
        success = bounded([sys.executable,'-c','print("ok")'], output, os.environ.copy(), output, 'success', 10, 0)
        assert success['exitCode'] == 0 and success['refusal'] is None
        failure = bounded([sys.executable,'-c','raise SystemExit(7)'], output, os.environ.copy(), output, 'failure', 10, 0)
        assert failure['exitCode'] == 7 and failure['refusal'] is None
        timeout = bounded([sys.executable,'-c','import time; time.sleep(60)'], output, os.environ.copy(), output, 'timeout', .02, 0)
        assert timeout['refusal'] == 'timeout' and timeout['exitCode'] != 0
        resource = bounded([sys.executable,'-c','import time; time.sleep(60)'], output, os.environ.copy(), output, 'resource', 10, shutil.disk_usage(output).total+1)
        assert resource['refusal'] == 'minimum-free-storage' and resource['exitCode'] != 0
    print('Build driver self-check: PASS (success, nonzero exit, timeout, resource stop)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--llvm-prefix', type=Path)
    parser.add_argument('--target-dir', type=Path, help='Reusable caller-owned Cargo cache outside source')
    parser.add_argument('--timeout-seconds', type=int, default=600)
    parser.add_argument('--test-timeout-seconds', type=int, default=180)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--minimum-free-gib', type=float, default=3)
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    if args.self_check:
        self_check()
        return
    if not all([args.source, args.output, args.llvm_prefix]):
        parser.error('--source, --output and --llvm-prefix are required')
    if not 1 <= args.timeout_seconds <= 600 or not 1 <= args.test_timeout_seconds <= 600 or not 1 <= args.jobs <= 2 or args.minimum_free_gib < 3:
        parser.error('Timeouts must be 1...600 seconds, jobs 1...2, and minimum free storage at least 3 GiB')
    source, output, llvm = args.source.resolve(), args.output.resolve(), args.llvm_prefix.resolve()
    if source == output or source in output.parents:
        parser.error('Output must be outside the source checkout')
    target = args.target_dir.resolve() if args.target_dir else output/'target'
    if target == source or source in target.parents:
        parser.error('Cargo target cache must be outside source')
    output.mkdir(parents=True, exist_ok=True)
    receipt_path = output/'receipt.json'
    if receipt_path.exists():
        parser.error('Output already has a receipt; choose a fresh evidence directory')
    shutil.copyfile(Path(__file__), output/'build-driver.py')
    receipt = {'buildDriver':{'path':str(output/'build-driver.py'),'sha256':sha256(output/'build-driver.py')},
               'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'source':str(source), 'output':str(output), 'results':[], 'passed':False}
    try:
        profile_path = PROFILE/'Source.json'
        profile = json.loads(profile_path.read_text())
        patch_path = PROFILE/profile['patch']['path']
        require(sha256(patch_path) == profile['patch']['sha256'], 'Tracked patch identity mismatch')
        commit = observe(['git','rev-parse','HEAD'], source)
        require(commit == profile['upstream']['commit'], 'Wrong upstream revision')
        require(observe(['git','remote','get-url','origin'], source) == profile['upstream']['repository'], 'Wrong upstream repository')
        require(not observe(['git','diff','--cached','--name-only'], source), 'Unexpected staged source changes')
        diff = subprocess.check_output(['git','diff','--binary','--no-ext-diff'], cwd=source)
        if not diff:
            subprocess.run(['git','apply','--check',str(patch_path)],cwd=source,check=True,timeout=30)
            subprocess.run(['git','apply',str(patch_path)],cwd=source,check=True,timeout=30)
            diff = subprocess.check_output(['git','diff','--binary','--no-ext-diff'],cwd=source)
        require(diff == patch_path.read_bytes(), 'Checkout changes differ from the exact tracked patch')
        require(not observe(['git','ls-files','--others','--exclude-standard'],source), 'Unexpected untracked source files')
        before = source_snapshot(source)
        (output/'source-before.json').write_text(json.dumps(before,indent=2)+'\n')
        shutil.copyfile(source/'Cargo.lock', output/'Cargo.lock')
        environment = os.environ.copy()
        for name in list(environment):
            if name.startswith('LLVM_SYS_') or name in {'RUSTFLAGS','CARGO_ENCODED_RUSTFLAGS','RUST_CHECK','CARGO_BUILD_TARGET','CARGO_PROFILE_DEV_DEBUG','CARGO_PROFILE_RELEASE_DEBUG','CARGO_INCREMENTAL'}:
                environment.pop(name)
        overrides = {'LLVM_SYS_221_PREFIX':str(llvm), 'PATH':str(llvm/'bin')+os.pathsep+environment.get('PATH',''),
                     'CC':str(llvm/'bin/clang'), 'CXX':str(llvm/'bin/clang++'), 'CARGO_TARGET_DIR':str(target),
                     'CARGO_BUILD_JOBS':str(args.jobs), 'CARGO_PROFILE_DEV_DEBUG':'0',
                     'CARGO_PROFILE_RELEASE_DEBUG':'0', 'CARGO_INCREMENTAL':'0'}
        environment.update(overrides)
        llvm_config, clang = llvm/'bin/llvm-config', llvm/'bin/clang'
        require(observe([str(llvm_config),'--version'],source) == '22.1.8', 'Expected LLVM 22.1.8')
        rust_version = observe(['rustc','--version'],source,environment)
        require(rust_version.startswith('rustc 1.92.0 '), 'Expected Rust 1.92.0')
        sysroot = Path(observe(['rustc','--print','sysroot'],source,environment))
        header = source/f"openvaf/osdi/header/osdi_{profile['profile']['osdiMajor']}_{profile['profile']['osdiMinor']}.h"
        receipt['abiHeader'] = {'path':str(header),'sha256':sha256(header),
                                'version':f"{profile['profile']['osdiMajor']}.{profile['profile']['osdiMinor']}"}
        receipt.update({'profileSHA256':sha256(profile_path),'upstreamCommit':commit,'patchSHA256':sha256(patch_path),
                        'cargoLockSHA256':sha256(source/'Cargo.lock'),'commandEnvironment':overrides,
                        'rustVersion':rust_version,'cargoVersion':observe(['cargo','--version'],source,environment),
                        'rustCompiler':{'path':str(sysroot/'bin/rustc'),'sha256':sha256(sysroot/'bin/rustc')},
                        'llvm':{'prefix':str(llvm),'version':observe([str(llvm_config),'--version'],source),
                                'llvmConfigSHA256':sha256(llvm_config),'clangVersion':observe([str(clang),'--version'],source),
                                'clangSHA256':sha256(clang),'librarySHA256':sha256(llvm/'lib/libLLVM.dylib')}})
        minimum_free = int(args.minimum_free_gib*1024**3)
        require(shutil.disk_usage(output).free >= minimum_free, 'Insufficient storage before build')
        build = profile['build']
        require(build['mode'] == 'release', 'Production compiler cache qualification requires release mode')
        command = ['cargo','build','--release','-p',build['package'],'--features',build['llvmFeature'],'--locked','--message-format=json-render-diagnostics']
        result = bounded(command,source,environment,output,'build',args.timeout_seconds,minimum_free)
        receipt['results'].append(result)
        receipt_path.write_text(json.dumps(receipt,indent=2)+'\n')
        require(result['exitCode']==0 and result['refusal'] is None, 'Compiler build failed')
        package_ids = set()
        for line in Path(result['stdout']).read_text().splitlines():
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if event.get('reason') == 'compiler-artifact':
                package_ids.add(event['package_id'])
        receipt['compiledPackageIDs'] = sorted(package_ids)
        binary = target/build['mode']/build['binary']
        require(binary.is_file(), 'Build produced no compiler executable')
        help_result = bounded([str(binary),'--help'],source,environment,output,'help',30,minimum_free)
        receipt['results'].append(help_result)
        require(help_result['exitCode']==0 and help_result['refusal'] is None, 'Compiler help invocation failed')
        require(profile['profile']['argument'] in Path(help_result['stdout']).read_text(), 'Built binary lacks the patched profile argument')
        if 'precision' in profile:
            precise=profile['precision']
            semantic=PROFILE/precise['semanticProfile']['path']
            precision_header=(PROFILE/precise['programHeader']['path']).resolve()
            require(sha256(semantic)==precise['semanticProfile']['sha256'],'Precision semantic profile identity mismatch')
            require(sha256(precision_header)==precise['programHeader']['sha256'],'Precision program header identity mismatch')
            require(precise['argument'] in Path(help_result['stdout']).read_text(),'Built compiler lacks precision profile argument')
            receipt['precisionSemanticProfileSHA256']=sha256(semantic)
            receipt['precisionProgramHeaderSHA256']=sha256(precision_header)

        products = output/'products';products.mkdir()
        retained = products/build['binary'];shutil.copy2(binary,retained)
        receipt['compiler']={'path':str(retained),'sha256':sha256(retained),'bytes':retained.stat().st_size,
                             'linkedLibraries':observe(['/usr/bin/otool','-L',str(retained)],source)}
        test = bounded(['cargo','test','--release','-p',profile['regression']['package'],profile['regression']['testFilter'],'--locked'],source,environment,output,'focused-test',args.test_timeout_seconds,minimum_free)
        receipt['results'].append(test)
        require(test['exitCode']==0 and test['refusal'] is None, 'Focused upstream regression failed')
        test_log = Path(test['stdout']).read_text()
        counts = [int(n) for n in re.findall(r'test result: ok\. (\d+) passed;',test_log)]
        require(counts and sum(counts)>0, 'Focused upstream regression executed no tests')
        if 'precision' in profile:
            regression=profile['precision']['regression']
            test=bounded(['cargo','test','--release','-p',regression['package'],regression['testFilter'],'--locked'],source,environment,output,'precision-tests',args.test_timeout_seconds,minimum_free)
            receipt['results'].append(test)
            require(test['exitCode']==0 and test['refusal'] is None,'Precision compiler regression failed')
            counts=[int(n) for n in re.findall(r'test result: ok\. (\d+) passed;',Path(test['stdout']).read_text())]
            require(counts and sum(counts)>0,'Precision compiler regression executed no tests')

        if 'precision' in profile and 'analysisRegression' in profile['precision']:
            regression=profile['precision']['analysisRegression']
            test=bounded(['cargo','test','--release','-p',regression['package'],'--features',regression['features'],
                          '--lib',regression['testFilter'],'--locked'],source,environment,output,'analysis-type-test',args.test_timeout_seconds,minimum_free)
            receipt['results'].append(test)
            require(test['exitCode']==0 and test['refusal'] is None,'Analysis fact type regression failed')
            counts=[int(n) for n in re.findall(r'test result: ok\. (\d+) passed;',Path(test['stdout']).read_text())]
            require(counts and sum(counts)>0,'Analysis fact regression executed no tests')

        after = source_snapshot(source)
        (output/'source-after.json').write_text(json.dumps(after,indent=2)+'\n')
        receipt['sourceUnchanged'] = before==after
        require(receipt['sourceUnchanged'], 'Build or test changed tracked source inputs')
        receipt['passed']=True
    except Exception as error:
        receipt['failure']={'type':type(error).__name__,'message':str(error)}
    finally:
        if 'before' in locals() and 'sourceUnchanged' not in receipt:
            try:
                after = source_snapshot(source)
                (output/'source-after.json').write_text(json.dumps(after,indent=2)+'\n')
                receipt['sourceUnchanged'] = before == after
            except Exception as error:
                receipt['sourceInspectionFailure'] = str(error)
        receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat()
        receipt['freeBytesAfter']=shutil.disk_usage(output).free
        receipt_path.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:receipt[k] for k in ['passed','compiler','failure'] if k in receipt},indent=2))
    if not receipt['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
