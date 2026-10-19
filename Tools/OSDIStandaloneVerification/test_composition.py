"""Small composition regressions; native behavior remains owned by the real lanes."""
import importlib.util
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent/'OSDIPrecisionVerification'
sys.path.insert(0, str(TOOLS))
import constitutive_fixtures as identities


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load('standalone_runner', HERE/'run.py')
consumer = load('public_consumer_runner', HERE.parent/'OSDIConsumerVerification/verify_consumer.py')
typed_fixture = load('typed_simulation_real_fixture', TOOLS/'simulation_real_fixtures.py')
consumer_fixture = load('public_precision_fixture', HERE.parent/'OSDIConsumerVerification/precision_fixture.py')
source_checker = load('source_identity_checker', TOOLS/'verify_constitutive_identities.py')


class CompositionTests(unittest.TestCase):
    def test_standalone_uses_default_or_explicit_swift_without_path_name_requirement(self):
        swift = Path(shutil.which('swift')).absolute()
        version = subprocess.check_output([str(swift), '--version'], text=True, timeout=15)
        for explicit in (False, True):
            with self.subTest(explicit=explicit), tempfile.TemporaryDirectory() as directory:
                arguments = ['run.py', '--output', str(Path(directory)/'run')]
                if explicit:
                    arguments += ['--swift', str(swift)]
                with patch.object(sys, 'argv', arguments), \
                        patch.object(runner, 'execute', return_value=version) as execute, \
                        patch.object(runner, 'select_sdk', side_effect=RuntimeError('tool admission passed')):
                    with self.assertRaisesRegex(RuntimeError, 'tool admission passed'):
                        runner.main()
                self.assertEqual(execute.call_args.args[0], [swift, '--version'])

    def test_consumer_default_swift_selection_and_explicit_override(self):
        swift = Path(shutil.which('swift')).absolute()
        for explicit in (False, True):
            with self.subTest(explicit=explicit), tempfile.TemporaryDirectory() as directory:
                arguments = ['verify_consumer.py', '--source-url', 'https://example.invalid/source.git',
                    '--source-revision', '0'*40, '--foundation-url', 'https://example.invalid/foundation.git',
                    '--foundation-revision', '1'*40, '--scratch', str(Path(directory)/'consumer'),
                    '--clang', '/usr/bin/clang']
                if explicit:
                    arguments += ['--swift', str(swift)]
                with patch.object(sys, 'argv', arguments), \
                        patch.object(consumer, 'verify', side_effect=RuntimeError('selected tools inspected')) as verify:
                    with self.assertRaisesRegex(RuntimeError, 'selected tools inspected'):
                        consumer.main()
                self.assertEqual(verify.call_args.args[0].swift, swift)

    def test_consumer_version_gate_uses_version_output_and_rejects_other_releases(self):
        for version, accepted in (
                ('Apple Swift version 6.4 (swift-6.4-RELEASE)', True),
                ('Swift version 6.4.0 (swift-6.4.0-RELEASE)', True),
                ('Swift version 6.4.1 (swift-6.4.1-RELEASE)', False),
                ('Swift version 6.4 (DEVELOPMENT-SNAPSHOT)', False)):
            with self.subTest(version=version):
                execution = Mock()
                execution.run.return_value = version
                execution.checkout.side_effect = RuntimeError('version admission passed')
                with self.assertRaisesRegex(RuntimeError,
                        'version admission passed' if accepted else 'Swift 6.4.0 release'):
                    consumer.verify(SimpleNamespace(swift=Path('/usr/bin/swift'),
                        source_url='https://example.invalid/source.git', source_revision='0'*40), execution)
                self.assertEqual(execution.checkout.call_count, 1 if accepted else 0)

    def test_source_checker_paths_use_exact_tracked_directory_case(self):
        root = Path('/synthetic/source')
        expected = ('ConstitutiveIdentityTests.c', 'DecisionCoverTests.c', 'constitutive_fixtures.py')
        paths = source_checker.source_proof_files(root)
        self.assertEqual(tuple(path.relative_to(root).parts for path in paths),
                         tuple(('Tools', 'OSDIPrecisionVerification', name) for name in expected))

    def test_default_identity_source_bytes_stay_qualified(self):
        expected = {
            'conservation': 'fc5c72b3afc6a460657a62c87a9e3b6956897598afe7b06ce6f95862119d8ca6',
            'branch': '87c7b9df0adc70ff1b7f9ad1e6c4f531a8d0d37240fdaf85afa229c7e14fff0f',
            'growth': 'ec6313d30742928c7262d19e5def8112660aeb4a0eac86d047409fc76c8f096e',
            'independent_atoms': '439cf2f2fe44e8cdfb2a8b03205d29e858cdda11a99b1f63d8ef176cfd23701a'}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            identities.emit(root/'default')
            identities.emit(root/'public', public_probes=True)
            a, b = [json.loads((root/name/'manifest.json').read_text())['fixtures'] for name in ('default', 'public')]
            self.assertNotIn('probe-distinct', {entry['id'] for entry in a})
            self.assertEqual({entry['id'] for entry in b}-{entry['id'] for entry in a}, {'probe-distinct'})
            for entry in a:
                name = entry['id']
                for suffix in ('program', 'setup'):
                    self.assertEqual((root/'default'/f'{name}.{suffix}').read_bytes(), (root/'public'/f'{name}.{suffix}').read_bytes())
                if name in expected:
                    self.assertEqual(entry['programSHA256'], expected[name])
            source = (root/'public/probe-distinct.program').read_bytes()
            offset, count, stride = struct.unpack_from('<QII', source, 72+2*16)
            probes = [struct.unpack_from('<IIIIII', source, offset+i*stride) for i in range(count)][-6:]
            self.assertEqual([(value[3], value[4]) for value in probes],
                             [(0, 0xffffffff), (0, 0xffffffff), (0xffffffff, 0), (0, 1), (1, 0), (2, 2)])
            self.assertTrue(all(value[0] == 2 and value[2] == 10 for value in probes))

    def test_public_snapshot_keeps_semantic_profile_separate_from_content_digest(self):
        data = consumer_fixture.program()
        self.assertNotEqual(data[40:72], bytes(32))
        self.assertEqual(data[40:72].hex(), consumer_fixture.SEMANTIC_PROFILE.hex())
        self.assertNotEqual(hashlib.sha256(data).digest(), data[40:72])

    def test_public_export_has_exact_linear_roots_and_no_state(self):
        data = consumer_fixture.program()
        self.assertEqual(struct.unpack_from('<8sIIQIIII', data), (b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0))
        def section(index, fmt):
            offset, count, stride = struct.unpack_from('<QII', data, 72+16*index)
            return [struct.unpack_from(fmt, data, offset+i*stride) for i in range(count)]
        self.assertEqual(section(2, '<IIIIII'), [(2, 0, 10, 0, 0xffffffff, 0)])
        self.assertEqual(section(7, '<IIIIII'), [(2, 1, 0, 0, 0xffffffff, 0), (2, 2, 1, 0, 0xffffffff, 0),
                                               (2, 3, 2, 0, 0, 0), (2, 4, 1, 0, 0, 0)])
        constants = section(1, '<IIQ')
        self.assertEqual(constants, [(1, 1, 0), (2, 1, 0x3ff0000000000000)])
        self.assertEqual(section(5, '<IIIIII'), [])

    def test_all_precision_selectors_wait_for_independent_parity(self):
        for forged in (False, True):
            with self.subTest(forged=forged), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                environment = {'OSDI_PRECISION_WORKER': '/model/worker', 'OSDI_PRECISION_NUMERIC_PROFILE_SHA256': 'model-profile'}
                initial = dict(environment)
                calls = []
                numeric = {'sdk': '/explicit/sdk', 'worker': {'path': '/new/worker', 'sha256': 'a'*64},
                           'numericProfile': {'path': '/new/profile', 'sha256': 'b'*64}}
                legacy = {'mode': 'caller-qualified-artifact', 'worker': {'path': '/old/worker', 'sha256': 'c'*64},
                          'numericProfileSHA256': 'd'*64}
                def execute(argv, output, label, timeout, env, commands):
                    calls.append((list(map(str, argv)), label))
                    if label == 'identity-source-check':
                        proof = root/'identity-proof'
                        (proof/'fixtures').mkdir(parents=True)
                        for suffix in ('program', 'setup'):
                            value = (root/'identities'/f'probe-distinct.{suffix}').read_bytes()
                            if forged and suffix == 'setup':
                                value = value[:-1]+bytes([value[-1]^1])
                            (proof/'fixtures'/f'probe-distinct.{suffix}').write_bytes(value)
                        (proof/'receipt.json').write_text('{"synthetic":"passed"}\n')
                    elif label in ('simulation-real-library', 'raw-given-library'):
                        name = 'cold.dylib' if label == 'simulation-real-library' else 'raw-given.dylib'
                        (root/'simulation-real'/name).write_bytes(b'cold-library')
                    else:
                        selected = root/label.removesuffix('-inputs')
                        if selected.name == 'simulation-real':
                            with patch.object(sys, 'argv', ['simulation_real_fixtures.py', str(selected)]):
                                typed_fixture.main()
                        elif selected.name == 'identities':
                            identities.emit(selected, public_probes=True)
                        else:
                            selected.mkdir()
                            (selected/'manifest.json').write_text('{}\n')
                with patch.object(runner, 'execute', execute), patch.object(runner, 'prepare_legacy_worker', return_value=legacy):
                    if forged:
                        with self.assertRaisesRegex(ValueError, 'differs.*setup'):
                            runner.prepare_precision_inputs(root, Path('/explicit/clang'), True, environment, [], numeric)
                        self.assertEqual(environment, initial)
                    else:
                        result = runner.prepare_precision_inputs(root, Path('/explicit/clang'), True, environment, [], numeric)
                        self.assertEqual(set(environment), (set(runner.GENERATED_FIXTURES)-{'OSDI_STANDALONE_FIXTURES'}) | set(runner.TYPED_WORKER_BINDINGS) | {'OSDI_SIMULATION_REAL_QUALIFICATION'} | set(initial))
                        self.assertEqual({key: environment[key] for key in initial}, initial)
                        self.assertEqual(environment['OSDI_SIMULATION_REAL_WORKER'], '/new/worker')
                        self.assertEqual(environment['OSDI_SIMULATION_REAL_NUMERIC_PROFILE_SHA256'], 'b'*64)
                        self.assertEqual(environment['OSDI_PRECISION_OLD_WORKER'], '/old/worker')
                        self.assertEqual(environment['OSDI_SIMULATION_REAL_QUALIFICATION'], '1')
                        self.assertTrue(result['typedSimulationReal']['required'])
                        self.assertEqual(result['typedSimulationReal']['library']['sha256'], hashlib.sha256(b'cold-library').hexdigest())
                        self.assertFalse(result['actualModelsSelected'])
                        self.assertEqual(set(result['publicProbeExactCParity']), {'program', 'setup'})
                    command = next(argv for argv, label in calls if label == 'identity-source-check')
                    self.assertIn('--sanitize', command)
                    self.assertNotIn('--actual-manifest', command)
                    self.assertEqual({label for _, label in calls}, {'ranges-inputs', 'charts-inputs', 'covers-inputs', 'identities-inputs', 'simulation-real-inputs', 'identity-source-check'} | (set() if forged else {'simulation-real-library', 'raw-given-library'}))
                    if not forged:
                        cold = next(argv for argv, label in calls if label == 'simulation-real-library')
                        self.assertIn(str(runner.ROOT/'Tools/OSDIConsumerVerification/precision_fixture.c'), cold)
                        self.assertEqual(cold[cold.index('-isysroot')+1], '/explicit/sdk')


    def test_model_precision_uses_current_worker_while_only_legacy_preserves_receipt_pair(self):
        numeric = {'worker': {'path': '/fresh/protocol-five-worker'}, 'numericProfile': {'sha256': 'fresh-profile'}}
        original = {'OSDI_PRECISION_WORKER': '/historical/protocol-one-worker',
                    'OSDI_PRECISION_NUMERIC_PROFILE_SHA256': 'historical-profile',
                    'OSDI_SIMULATION_REAL_WORKER': '/separate/typed-worker'}
        for legacy in (False, True):
            environment = dict(original)
            runner.bind_precision_worker(environment, numeric, legacy)
            self.assertEqual(environment['OSDI_PRECISION_WORKER'], original['OSDI_PRECISION_WORKER'] if legacy else '/fresh/protocol-five-worker')
            self.assertEqual(environment['OSDI_PRECISION_NUMERIC_PROFILE_SHA256'], 'historical-profile' if legacy else 'fresh-profile')
            self.assertEqual(environment['OSDI_SIMULATION_REAL_WORKER'], '/separate/typed-worker')

    def test_sdk_selection_pins_explicit_or_xcrun_root_without_invalid_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);sdk=root/'MacOSX.sdk';sdk.mkdir();settings=sdk/'SDKSettings.json';settings.write_text('{"CanonicalName":"macosx"}')
            for explicit in (False, True):
                environment={'SDKROOT':str(sdk)} if explicit else {}
                with patch.object(runner, 'execute', return_value=str(sdk)+'\n') as execute:
                    selected=runner.select_sdk(root,environment,[])
                    self.assertEqual(execute.call_count,0 if explicit else 1)
                self.assertEqual(environment['SDKROOT'],str(sdk.resolve()))
                self.assertEqual(selected['settings']['sha256'],hashlib.sha256(settings.read_bytes()).hexdigest())
                self.assertEqual(selected['selection'],'caller' if explicit else 'xcrun-macosx')
            with patch.object(runner, 'execute', side_effect=AssertionError('Invalid explicit SDK fell back')):
                with self.assertRaisesRegex(RuntimeError,'unavailable'):
                    runner.select_sdk(root,{'SDKROOT':str(root/'absent')},[])
                empty=root/'empty';empty.mkdir()
                with self.assertRaisesRegex(RuntimeError,'settings identity'):
                    runner.select_sdk(root,{'SDKROOT':str(empty)},[])

    def test_raw_given_fixtures_keep_own_scope_separate_from_merged_presence(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'fixtures'
            with patch.object(sys, 'argv', ['simulation_real_fixtures.py', str(output)]):typed_fixture.main()
            manifest=json.loads((output/'manifest.json').read_text())
            entries={entry['id']:entry for entry in manifest['fixtures']}
            absent=Path(entries['raw-given-model-only']['setupPath']).read_bytes()
            explicit=Path(entries['raw-given-instance-same']['setupPath']).read_bytes()
            offset,count,stride=struct.unpack_from('<QII',absent,32)
            self.assertEqual((count,stride),(2,16))
            self.assertEqual(struct.unpack_from('<IIQ',absent,offset),(0,1,typed_fixture.real(1)))
            self.assertEqual(struct.unpack_from('<IIQ',absent,offset+stride),(1,0,0))
            self.assertEqual(struct.unpack_from('<IIQ',explicit,offset+stride),(1,1,typed_fixture.real(1)))
            program=Path(entries['raw-given-model-only']['programPath']).read_bytes()
            at,count,stride=struct.unpack_from('<QII',program,72+2*16)
            inputs=[struct.unpack_from('<IIIIII',program,at+i*stride)for i in range(count)]
            self.assertEqual({q[0]for q in inputs if q[2]==15},{0,1,2})
            self.assertTrue(any(q[2]==2 and q[3]==1 for q in inputs))
            self.assertTrue(all(q[4]==0 for q in inputs if q[2]==15))
            self.assertEqual(entries['raw-given-model-only']['expectedF'],3)
            self.assertEqual(entries['raw-given-instance-same']['expectedF'],5)
            self.assertNotEqual(program[40:72],typed_fixture.PROFILE)

    def test_ordinary_selection_clears_typed_inputs_without_changing_model_bindings(self):
        environment = {key: 'inherited' for key in runner.QUALIFICATION_SELECTORS + runner.GENERATED_FIXTURES + runner.TYPED_WORKER_BINDINGS}
        environment.update(OSDI_PRECISION_WORKER='/model/worker', OSDI_PRECISION_NUMERIC_PROFILE_SHA256='model-profile', PATH='/usr/bin')
        runner.clear_qualification_inputs(environment)
        self.assertEqual(environment, {'OSDI_PRECISION_WORKER': '/model/worker',
            'OSDI_PRECISION_NUMERIC_PROFILE_SHA256': 'model-profile', 'PATH': '/usr/bin'})

    def test_explicit_old_worker_pair_is_offline_and_half_pair_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            worker = root/'old-worker';worker.write_bytes(b'qualified-old-artifact');worker.chmod(0o700)
            inputs = {'OSDI_PRECISION_OLD_WORKER': str(worker), 'OSDI_PRECISION_OLD_NUMERIC_PROFILE_SHA256': 'e'*64}
            with patch.object(runner, 'execute', side_effect=AssertionError('Offline selection attempted a process')):
                result = runner.prepare_legacy_worker(root, Path('/clang'), {}, inputs, {}, [])
                self.assertEqual(result['mode'], 'caller-qualified-artifact')
                self.assertEqual(result['worker']['sha256'], hashlib.sha256(worker.read_bytes()).hexdigest())
                with self.assertRaisesRegex(RuntimeError, 'together'):
                    runner.prepare_legacy_worker(root, Path('/clang'), {}, {'OSDI_PRECISION_OLD_WORKER': str(worker)}, {}, [])
                inputs['OSDI_PRECISION_OLD_NUMERIC_PROFILE_SHA256'] = 'bad-profile'
                with self.assertRaisesRegex(RuntimeError, 'Invalid qualified'):
                    runner.prepare_legacy_worker(root, Path('/clang'), {}, inputs, {}, [])

    def test_public_old_worker_requires_exact_revision_and_matching_numeric_inputs(self):
        for bad_revision, bad_numeric in [(False, False), (True, False), (False, True)]:
            with self.subTest(bad_revision=bad_revision, bad_numeric=bad_numeric), tempfile.TemporaryDirectory() as directory:
                root = Path(directory);calls=[]
                numeric = {'sdk': '/sdk', 'compiler': {'sha256': 'compiler'}, 'buildSelection': {
                    'sdkSettings': {'sha256': 'sdk'}, 'numeric': [
                        {'name': 'mpfr', 'header': {'sha256': 'header'}, 'archive': {'sha256': 'archive'}}]}}
                def execute(argv, output, label, timeout, environment, commands):
                    calls.append((list(map(str, argv)), label, environment))
                    if label == 'legacy-source-revision':return 'wrong' if bad_revision else runner.LEGACY_REVISION+'\n'
                    if label == 'legacy-numeric-build':
                        old = json.loads(json.dumps(numeric));old['worker']={'path': '/old/worker', 'sha256': 'a'*64}
                        old['numericProfile']={'sha256': 'b'*64}
                        if bad_numeric:old['buildSelection']['numeric'][0]['archive']['sha256']='different'
                        (root/'legacy-numeric').mkdir()
                        (root/'legacy-numeric/receipt.json').write_text(json.dumps(old))
                    return ''
                with patch.object(runner, 'execute', execute):
                    if bad_revision or bad_numeric:
                        with self.assertRaisesRegex(RuntimeError, 'qualified commit' if bad_revision else 'inputs differ'):
                            runner.prepare_legacy_worker(root, Path('/clang'), numeric, None, {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'url.bad.insteadOf'}, [])
                    else:
                        result=runner.prepare_legacy_worker(root, Path('/clang'), numeric, None, {}, [])
                        self.assertEqual(result['mode'], 'public-immutable-source')
                        self.assertEqual(result['revision'], runner.LEGACY_REVISION)
                fetch, label, environment = calls[0]
                self.assertEqual(label, 'legacy-source-fetch')
                self.assertEqual(fetch[-2], 'https://github.com/1amageek/swift-osdi.git')
                self.assertEqual(fetch[fetch.index('--branch')+1], '1.1.0')
                self.assertEqual(environment['GIT_CONFIG_GLOBAL'], '/dev/null')
                self.assertNotIn('GIT_CONFIG_COUNT', environment)
                self.assertNotIn('GIT_CONFIG_KEY_0', environment)
                if bad_revision:self.assertEqual(len(calls), 2)


if __name__ == '__main__':
    unittest.main()
