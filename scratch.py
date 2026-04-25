import xml.etree.ElementTree as ET
import glob
import os

xml_path = 'c:/Users/monis/mplabnew_secure/tftdisplay_secure.X/nbproject/configurations.xml'

ET.register_namespace('', 'http://www.netbeans.org/ns/project/1')

tree = ET.parse(xml_path)
root = tree.getroot()

# Helper macro to remove namespace prefixes if any, though MPLAB doesn't usually use strict ns on elements
def local_name(tag):
    return tag.split('}')[-1]

source_folder = None
for lf in root.iter():
    if local_name(lf.tag) == 'logicalFolder' and lf.get('name') == 'SourceFiles':
        source_folder = lf
        break

if source_folder is None:
    for lf in root.iter():
        if local_name(lf.tag) == 'logicalFolder' and lf.get('name') == 'root':
            source_folder = lf
            break

src_dir = 'c:/Users/monis/mplabnew_secure/src'
tinyml_files = []
for ext in ('*.c', '*.cpp', '*.cc'):
    tinyml_files.extend(glob.glob(os.path.join(src_dir, 'tinyml', '**', ext), recursive=True))

ignored_ports = [
    'porting/arduino', 'porting/mbed', 'porting/espressif', 'porting/posix',
    'porting/silabs', 'porting/stm32-cubeai', 'porting/ti', 'porting/zephyr',
    'porting/sony', 'porting/ndp120', 'porting/himax', 'porting/freescale',
    'porting/renesas', 'porting/nuttx', 'porting/winnxt', 'porting/synaptics', 'porting/nordic', 'porting/brickml'
]

valid_files = []
for f in tinyml_files:
    f_norm = f.replace('\\', '/')
    if '/tinyml/edge-impulse-sdk/tensorflow/lite/micro/testing/' in f_norm: continue
    
    skip = False
    for p in ignored_ports:
        if p in f_norm:
            skip = True
    if skip: continue
    valid_files.append(f_norm)

# Remove old tinyml folder if exists
for lf in list(source_folder):
    if local_name(lf.tag) == 'logicalFolder' and lf.get('name') == 'tinyml':
        source_folder.remove(lf)

tinyml_lf = ET.SubElement(source_folder, 'logicalFolder')
tinyml_lf.set('name', 'tinyml')
tinyml_lf.set('displayName', 'tinyml')
tinyml_lf.set('projectFiles', 'true')

for f in valid_files:
    rel = '../src/' + f.split('/src/')[1]
    item = ET.SubElement(tinyml_lf, 'itemPath')
    item.text = rel

# Also ensure wrapper is added
wrapper_added = False
for item in source_folder.findall('*'):
    if local_name(item.tag) == 'itemPath' and item.text == '../src/tinyml_wrapper.cpp':
        wrapper_added = True

if not wrapper_added:
    tinyml_lf_wrapper = ET.SubElement(source_folder, 'itemPath')
    tinyml_lf_wrapper.text = '../src/tinyml_wrapper.cpp'

extra_includes = ';../src/tinyml;../src/tinyml/edge-impulse-sdk;../src/tinyml/model-parameters;../src/tinyml/tflite-model;../src'

for conf in root.iter():
    if local_name(conf.tag) == 'conf':
        for c32cpp in conf.findall('.//C32CPP'):
            for prop in c32cpp.findall('*'):
                if local_name(prop.tag) == 'property' and prop.get('key') == 'extra-include-directories':
                    val = prop.get('value', '')
                    if '../src/tinyml' not in val:
                        prop.set('value', val + extra_includes)
        
        for c32 in conf.findall('.//C32'):
            for prop in c32.findall('*'):
                if local_name(prop.tag) == 'property' and prop.get('key') == 'extra-include-directories':
                    val = prop.get('value', '')
                    if '../src/tinyml' not in val:
                        prop.set('value', val + extra_includes)

# Remove the default namespace for writing back exactly like MPLAB expects
with open(xml_path, 'r', encoding='utf-8') as f:
    text = f.read()

tree.write(xml_path, encoding='UTF-8', xml_declaration=True)

# Post-process to remove ns0: elements introduced by ElementTree namespace handling
with open(xml_path, 'r', encoding='utf-8') as f:
    text = f.read()

text = text.replace('ns0:', '').replace(':ns0', '')

with open(xml_path, 'w', encoding='utf-8') as f:
    f.write(text)

print(f"Successfully patched {xml_path} with {len(valid_files)} Edge Impulse files.")
