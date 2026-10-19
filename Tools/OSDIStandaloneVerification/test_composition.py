"""Small composition regressions; native behavior remains owned by the real lanes."""
import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

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
consumer_fixture = load('public_precision_fixture', HERE.parent/'OSDIConsumerVerification/precision_fixture.py')
source_checker = load('source_identity_checker', TOOLS/'verify_constitutive_identities.py')


class CompositionTests(unittest.TestCase):
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
                environment, calls = {}, []
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
                    else:
                        selected = root/label.removesuffix('-inputs')
                        if selected.name == 'identities':
                            identities.emit(selected, public_probes=True)
                        else:
                            selected.mkdir()
                            (selected/'manifest.json').write_text('{}\n')
                with patch.object(runner, 'execute', execute):
                    if forged:
                        with self.assertRaisesRegex(ValueError, 'differs.*setup'):
                            runner.prepare_precision_inputs(root, Path('/explicit/clang'), True, environment, [])
                        self.assertEqual(environment, {})
                    else:
                        result = runner.prepare_precision_inputs(root, Path('/explicit/clang'), True, environment, [])
                        self.assertEqual(set(environment), set(runner.GENERATED_FIXTURES)-{'OSDI_STANDALONE_FIXTURES'})
                        self.assertFalse(result['actualModelsSelected'])
                        self.assertEqual(set(result['publicProbeExactCParity']), {'program', 'setup'})
                    command = calls[-1][0]
                    self.assertIn('--sanitize', command)
                    self.assertNotIn('--actual-manifest', command)
                    self.assertEqual({label for _, label in calls}, {'ranges-inputs', 'charts-inputs', 'covers-inputs', 'identities-inputs', 'identity-source-check'})


if __name__ == '__main__':
    unittest.main()
