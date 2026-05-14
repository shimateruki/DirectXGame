import json, glob, os

found = False
for f in glob.glob('Resources/json/**/*.json', recursive=True):
    try:
        with open(f, 'r', encoding='utf-8') as file:
            data = json.load(file)
            if isinstance(data, dict) and 'objects' in data:
                for obj in data['objects']:
                    if 'color' in obj:
                        c = obj['color']
                        if c[0] > 2.0 or c[0] < -2.0 or c[1] > 2.0 or c[1] < -2.0:
                            name = obj.get("name", "unnamed")
                            print(f"Found extreme color in {f}: {name} -> {c}")
                            found = True
    except Exception as e:
        pass

if not found:
    print("No extreme colors found in JSON objects.")
