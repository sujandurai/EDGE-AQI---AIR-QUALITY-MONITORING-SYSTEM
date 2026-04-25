import os
import glob

# 1. Rename all .cc files to .cpp
src_dir = 'c:/Users/monis/mplabnew_secure/src'
cc_files = glob.glob(os.path.join(src_dir, 'tinyml', '**', '*.cc'), recursive=True)

renamed_count = 0
for f in cc_files:
    new_name = f[:-3] + '.cpp'
    os.rename(f, new_name)
    renamed_count += 1

print(f'Renamed {renamed_count} .cc files to .cpp on disk.')

# 2. Update configurations.xml to point to .cpp instead of .cc
xml_path = 'tftdisplay_secure.X/nbproject/configurations.xml'
with open(xml_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Replace any .cc</itemPath> with .cpp</itemPath>
new_content = content.replace('.cc</itemPath>', '.cpp</itemPath>')

if content != new_content:
    with open(xml_path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print('SUCCESS: Updated configurations.xml to use .cpp extensions.')
else:
    print('No .cc entries found in configurations.xml.')
