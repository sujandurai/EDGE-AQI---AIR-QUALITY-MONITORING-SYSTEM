import glob
import os

xml_path = 'tftdisplay_secure.X/nbproject/configurations.xml'

with open(xml_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Find all .cc files
src_dir = 'c:/Users/monis/mplabnew_secure/src'
cc_files = glob.glob(os.path.join(src_dir, 'tinyml', '**', '*.cc'), recursive=True)

# Filter out testing and irrelevant porting dirs
ignored = ['testing/', 'porting/arduino/', 'porting/mbed/', 'porting/espressif/',
           'porting/posix/', 'porting/silabs/', 'porting/stm32-cubeai/', 'porting/ti/',
           'porting/zephyr/', 'porting/sony/', 'porting/himax/', 'porting/brickml/',
           'porting/mingw32/', 'porting/android/', 'porting/ambiq/', 'porting/ceva-npn/',
           'porting/raspberry/', 'porting/renesas-ra/', 'porting/particle/',
           'porting/seeed-vision-ai/', 'porting/infineon-psoc62/', 'porting/synaptics/',
           'porting/clib/', 'porting/iar/', 'porting/himax-we2/']

valid = []
for f in cc_files:
    fn = f.replace('\\', '/')
    skip = False
    for ig in ignored:
        if ig in fn:
            skip = True
            break
    if skip:
        continue
    # Already in project?
    basename = os.path.basename(fn)
    if basename in content:
        continue
    rel = '../src/' + fn.split('/src/')[1]
    valid.append(rel)

print(f'Found {len(valid)} new .cc files to add')

# Insert them right before ei_porting.cpp entry
marker = '<itemPath>../src/ei_porting.cpp</itemPath>'
if marker in content:
    new_entries = ''.join(f'<itemPath>{v}</itemPath>' for v in valid)
    content = content.replace(marker, new_entries + marker)
    with open(xml_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f'SUCCESS: Added {len(valid)} .cc files to project.')
else:
    print('ERROR: Could not find marker in XML')
