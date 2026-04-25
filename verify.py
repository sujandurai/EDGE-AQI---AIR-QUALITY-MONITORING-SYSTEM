with open('tftdisplay_secure.X/nbproject/configurations.xml', 'r', encoding='utf-8') as f:
    content = f.read()

checks = [
    ('Include paths patched', '../src/tinyml/edge-impulse-sdk'),
    ('Response files enabled', 'additional-options-use-response-files" value="true"'),
    ('tinyml_wrapper.cpp present', 'tinyml_wrapper.cpp'),
    ('Edge Impulse files present', 'arm_abs_f32'),
]

all_ok = True
for name, pattern in checks:
    status = 'OK' if pattern in content else 'MISSING'
    if status == 'MISSING':
        all_ok = False
    print(f'[{status}] {name}')

print()
print('All checks passed!' if all_ok else 'SOME CHECKS FAILED - re-run patches.')
