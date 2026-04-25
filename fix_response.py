with open('tftdisplay_secure.X/nbproject/configurations.xml', 'r', encoding='utf-8') as f:
    content = f.read()

# Find what the current value is
import re
matches = re.findall(r'additional-options-use-response-files[^/]*/>', content)
print('Current entries:')
for m in matches:
    print(' ', m)

# Patch it
old = 'additional-options-use-response-files" value="false"'
new = 'additional-options-use-response-files" value="true"'

count = content.count(old)
print(f'\nFound {count} occurrence(s) to replace.')

if count > 0:
    content = content.replace(old, new)
    with open('tftdisplay_secure.X/nbproject/configurations.xml', 'w', encoding='utf-8') as f:
        f.write(content)
    print('SUCCESS: Response files enabled.')
else:
    print('Nothing replaced - may already be true or different format.')
    # Also check if already true
    already = content.count('additional-options-use-response-files" value="true"')
    print(f'Already-true count: {already}')
