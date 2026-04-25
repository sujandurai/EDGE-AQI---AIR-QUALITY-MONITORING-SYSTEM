with open('tftdisplay_secure.X/nbproject/configurations.xml', 'r', encoding='utf-8') as f:
    content = f.read()

# Find the closing tag of the tinyml logical folder and add missing files after it
# tinyml_wrapper.cpp is already there - add the two missing files right before it

old = '<itemPath>../src/tinyml_wrapper.cpp</itemPath>'
new = '''<itemPath>../src/tinyml/tflite-model/tflite_learn_959498_7_compiled.cpp</itemPath><itemPath>../src/ei_porting.cpp</itemPath><itemPath>../src/tinyml_wrapper.cpp</itemPath>'''

if old in content:
    # Check if already patched
    if 'ei_porting.cpp' in content:
        print('Already patched.')
    else:
        content = content.replace(old, new)
        with open('tftdisplay_secure.X/nbproject/configurations.xml', 'w', encoding='utf-8') as f:
            f.write(content)
        print('SUCCESS: Added ei_porting.cpp and tflite_learn_959498_7_compiled.cpp to project.')
else:
    print('ERROR: Could not find insertion point. tinyml_wrapper.cpp entry not found.')
    # Show where tinyml files end
    idx = content.find('tinyml_wrapper')
    if idx >= 0:
        print('Found at index:', idx)
        print('Context:', repr(content[idx-50:idx+80]))
    else:
        print('tinyml_wrapper not found at all!')
