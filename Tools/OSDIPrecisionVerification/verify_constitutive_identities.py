#!/usr/bin/env python3
"""Build/run bounded internal source-identity verification with exact receipts.

Actual manifest: {"cases":[{"id":...,"program":...,"setup":...,"request":...}]}
Request is the existing exact range request retaining all four coordinate bit
planes and both contexts. No model compilation, transient, public transport,
parameter change, source-box refinement or fallback is performed here.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import selectors
import signal
import struct
import subprocess
import time
import native_build
from constitutive_fixtures import emit

ROOT = Path(__file__).resolve().parents[2]


def source_proof_files(root):
    directory = root/'Tools/OSDIPrecisionVerification'
    return (directory/'ConstitutiveIdentityTests.c', directory/'DecisionCoverTests.c',
            directory/'constitutive_fixtures.py')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2); stream.write('\n')


def run(command, output, timeout):
    start = time.monotonic()
    with (output.with_suffix('.stdout')).open('x') as stdout, (output.with_suffix('.stderr')).open('x') as stderr:
        try:
            result = subprocess.run(list(map(str, command)), stdout=stdout, stderr=stderr, timeout=timeout)
            record = {'command': list(map(str, command)), 'returncode': result.returncode,
                      'elapsedSeconds': time.monotonic()-start, 'timeoutSeconds': timeout}
        except subprocess.TimeoutExpired:
            record = {'command': list(map(str, command)), 'timedOut': True,
                      'elapsedSeconds': time.monotonic()-start, 'timeoutSeconds': timeout}
    save(output.with_suffix('.json'), record)
    if record.get('returncode') != 0:
        raise RuntimeError('Bounded stage failed: '+str(output))
    return record


def read_capture(case, program_pin):
    request=Path(case['request']).resolve(strict=True);data=request.read_bytes()
    if case.get('requestFormat')!='control-v1-four-planes' or len(data)<464 or len(data)%8:
        raise ValueError('Exact captured 464-byte control plus four aligned planes is required')
    u32=lambda at:struct.unpack_from('<I',data,at)[0]
    u64=lambda at:struct.unpack_from('<Q',data,at)[0]
    active,raw=u64(32),u64(40)
    if (u64(0)!=0x313056504944534f or u32(8)!=2 or u32(12)!=464 or
        u32(24)!=0x01020304 or not active or active>raw or len(data)!=464+4*active*8 or
        data[64:96].hex()!=program_pin['sha256'] or
        not 53<=u32(60)<=u32(416)<=u32(56)):
        raise ValueError('Captured source geometry/operation/precision/program binding is invalid')
    # This saved format has no mapped slab. Preserve all unused header bytes;
    # they are not fabricated into an IPC request or a new worker identity.
    if u64(16) or any(data[128:352]):
        raise ValueError('Unsupported capture container geometry')
    contexts=[]
    for at in (368,392):
        mode,reserved,high,low=struct.unpack_from('<IIdd',data,at)
        if mode!=2 or reserved or not math.isfinite(high) or not math.isfinite(low) or high+low!=high:
            raise ValueError('Captured time context is invalid')
        contexts.append({'mode':mode,'timeHighBits':u64(at+8),'timeLowBits':u64(at+16)})
    words=struct.unpack_from('<'+'Q'*(4*active),data,464)
    values=struct.unpack_from('<'+'d'*(4*active),data,464)
    for i in range(active):
        lh,ll,uh,ul=(values[i+j*active] for j in range(4))
        if not all(map(math.isfinite,(lh,ll,uh,ul))) or lh+ll!=lh or uh+ul!=uh or (lh,ll)>(uh,ul):
            raise ValueError('Captured coordinate interval is invalid')
    binding=[]
    for key in ('sourceInputManifest','priorCoverReceipt','priorQualification'):
        pin=case[key];path=Path(pin['path']).resolve(strict=True)
        if digest(path)!=pin['sha256']:raise ValueError('Original binding changed: '+key)
        binding.append(native_build.identity(path))
    original=json.loads(Path(case['sourceInputManifest']['path']).read_text())
    family=next(f for f in original['families'] if f['family']==case['sourceFamily'])
    if (family['programSHA256']!=program_pin['sha256'] or family['setupSHA256']!=case['setupSHA256'] or
        family['requestSHA256']!=case['requestSHA256'] or family['activeUnknownCount']!=active or
        family['rawNodeCount']!=raw or family['precisionBits']!=u32(416)):
        raise ValueError('Original source/setup/topology scope mismatch')
    expected=family['centerHighBits']+family['centerLowBits']
    if list(words)!=expected+expected:
        raise ValueError('Original singleton source box changed')
    for context in contexts:
        if (context['timeHighBits'],context['timeLowBits'])!=(family['context']['timeHighBits'],family['context']['timeLowBits']):
            raise ValueError('Original source time context changed')
    prior=json.loads(Path(case['priorCoverReceipt']['path']).read_text())
    command=prior['command']
    if prior['status']!='passed' or prior['exitCode'] or list(map(str,(Path(command[i]).resolve() for i in (2,3,4))))!=[
        str(Path(case[key]).resolve()) for key in ('program','setup','request')]:
        raise ValueError('Prior complete-cover execution does not bind these exact inputs')
    qualification=json.loads(Path(case['priorQualification']['path']).read_text())
    if qualification['status']!='passed' or not any(pin['sha256']==case['priorCoverReceipt']['sha256']
            for pin in qualification['receipts']):
        raise ValueError('Prior d10443f qualification receipt binding is invalid')
    return {'format':'control-v1-four-planes','bytes':len(data),'active':active,'raw':raw,
            'controlHex':data[:464].hex(),'contexts':contexts,'requestedPrecision':u32(416),
            'minimumPrecision':u32(60),'maximumPrecision':u32(56),
            'originalResources':{'arenaBytes':u64(48),'maximumWorkUnits':u64(424)},
            'originalInputsCopied':False,'bindingInputs':binding}


def cancel_probe(binary, fixtures, output):
    # Same isolated-process lifetime rule as the worker: cancellation kills and
    # reaps the sole child. This does not claim new public transport admission.
    command = [str(binary),'--cancel',str(fixtures)]
    start = time.monotonic()
    child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        selector = selectors.DefaultSelector(); selector.register(child.stdout, selectors.EVENT_READ)
        if not selector.select(10):
            raise RuntimeError('Cancellation probe did not reach the real identity attempt')
        ready = child.stdout.readline().strip()
        if ready != 'source-identity-attempt-ready':
            raise RuntimeError('Unexpected readiness: '+ready)
        child.terminate()
        stdout, stderr = child.communicate(timeout=5)
        if child.returncode != -signal.SIGTERM:
            raise RuntimeError('Cancellation was not the terminal process outcome')
        if '"identities"' in stdout:
            raise RuntimeError('Cancelled attempt published a completed proof')
        save(output, {'command':command,'ready':ready,'returncode':child.returncode,
            'elapsedSeconds':time.monotonic()-start,'reaped':True,'publishedProof':False,
            'scope':'isolated internal verifier process; no public identity transport exists',
            'stdout':stdout,'stderr':stderr})
    finally:
        if child.poll() is None:
            child.kill(); child.communicate(timeout=5)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output-dir',required=True,type=Path)
    parser.add_argument('--actual-manifest',type=Path)
    parser.add_argument('--coefficient-bits',required=True,type=int)
    parser.add_argument('--arena-bytes',required=True,type=int)
    parser.add_argument('--workspace-bytes',required=True,type=int)
    parser.add_argument('--host-memory-bytes',required=True,type=int)
    parser.add_argument('--maximum-atoms',required=True,type=int)
    parser.add_argument('--maximum-nodes',required=True,type=int)
    parser.add_argument('--work',required=True,type=int)
    parser.add_argument('--steps',required=True,type=int)
    parser.add_argument('--case-timeout',required=True,type=float)
    parser.add_argument('--sanitize',action='store_true')
    native_build.arguments(parser);args=parser.parse_args()
    if min(args.coefficient_bits,args.arena_bytes,args.workspace_bytes,args.host_memory_bytes,args.maximum_atoms,
           args.maximum_nodes,args.work,args.steps,args.case_timeout)<=0:
        parser.error('All resource ceilings must be explicit and positive')
    if max(args.maximum_atoms,args.maximum_nodes)>=2**32-1:
        parser.error('Node/atom capacities must fit their admitted UInt32 representation')
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=False)
    selected=native_build.resolve(args)
    sources=sorted(p for p in (ROOT/'Sources/OSDIPrecisionWorker').rglob('*.c') if 'WorkerProtocol' not in p.parts and p.name != 'WorkerEntry.c')
    headers=sorted((ROOT/'Sources/OSDIPrecisionWorker').rglob('*.h'))
    headers += [ROOT/'Sources/COSDI/include/OSDICompilerPrecision.h',ROOT/'Sources/COSDI/include/OSDIPrecisionProtocol.h']
    source,fixture_loader,fixture_generator=source_proof_files(ROOT)
    sources.append(source)
    numeric_inputs=[value[key] for value in selected['numeric'].values() for key in ('header','archive')]
    pins=[native_build.identity(p) for p in sources+headers+numeric_inputs+[fixture_loader,Path(__file__),fixture_generator]]
    binary=output/'constitutive-identity-tests'
    command=[selected['compiler'],'-isysroot',selected['sdk'],'-std=c11','-O2','-g','-Wall','-Wextra','-Werror',
             '-fno-fast-math','-ffp-contract=off']
    if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    for directory in [ROOT/'Sources/COSDI/include',
                      selected['numeric']['mpfr']['include'],selected['numeric']['gmp']['include']]:
        command+=['-I',directory]
    command+=sources+[selected['numeric']['mpfr']['archive'],selected['numeric']['gmp']['archive'],'-o',binary]
    run(command,output/'build',60)
    native_build.unchanged(pins+selected['identities'])
    fixtures=output/'fixtures';emit(fixtures)
    run([binary,fixtures],output/'synthetic',30)
    cancel_probe(binary,fixtures,output/'cancellation.json')
    records=[]
    if args.actual_manifest:
        manifest=args.actual_manifest.resolve(strict=True)
        cases=json.loads(manifest.read_text())['cases']
        if {case['id'] for case in cases}!={'bulk','cmg','img','soi'} or len(cases)!=4:
            raise ValueError('Actual qualification must explicitly retain all four model families')
        for case in cases:
            name=case['id'];program=Path(case['program']).resolve(strict=True)
            setup=Path(case['setup']).resolve(strict=True);request=Path(case['request']).resolve(strict=True)
            identities=[native_build.identity(p) for p in (program,setup,request)]
            for key,pin in zip(('programSHA256','setupSHA256','requestSHA256'),identities):
                if case.get(key)!=pin['sha256']:
                    raise ValueError('Actual case requires exact independently selected input pin: '+key)
            capture=read_capture(case,identities[0]);precision=capture['requestedPrecision']
            command=[binary,name,program,setup,request,args.arena_bytes,args.coefficient_bits,
                     args.work,args.steps,precision,args.maximum_atoms,args.maximum_nodes,
                     args.workspace_bytes,args.host_memory_bytes]
            # argv[1] is a label; all model/source bytes and original box stay exact.
            run(command,output/name,args.case_timeout)
            result=json.loads((output/(name+'.stdout')).read_text())
            native_build.unchanged(identities)
            native_build.unchanged(capture['bindingInputs'])
            records.append({'id':name,'inputs':identities,'originalCapture':capture,
                'experimentalResourceOverrides':{'arenaBytes':args.arena_bytes,'maximumWorkUnits':args.work,
                    'maximumSteps':args.steps,'hostManagedPayloadBytes':args.host_memory_bytes,
                    'componentFixedWorkspaceBytes':args.workspace_bytes},'result':result})
    native_build.unchanged(pins+selected['identities'])
    save(output/'receipt.json',{'schema':'osdi.constitutive-identities-verification.v1',
        'sources':pins,'buildSelection':selected['selection'],'binary':native_build.identity(binary),
        'limits':{key:getattr(args,key) for key in ('coefficient_bits','arena_bytes','workspace_bytes','host_memory_bytes',
             'maximum_atoms','maximum_nodes','work','steps','case_timeout')},
        'synthetic':'passed','cancelledInternalProcessReaped':True,'actualCases':records,
        'publicTransportQualified':False,'rootFlowHistoryAuthority':False})
    if any(case['result']['status']!=0 for case in records):
        raise RuntimeError('Actual source-identity admission refused; exact statuses are retained')


if __name__=='__main__':main()
