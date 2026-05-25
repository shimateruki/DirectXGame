import json
import sys

title_file = r'c:\Users\k024g\Lesson\2026A\TD3\project\Resources\json\3Dobject\titleScene_object.json'
game_file = r'c:\Users\k024g\Lesson\2026A\TD3\project\Resources\json\3Dobject\bossStage_object.json'

try:
    with open(title_file, 'r', encoding='utf-8') as f:
        title_data = json.load(f)
        
    with open(game_file, 'r', encoding='utf-8') as f:
        game_data = json.load(f)

    names_to_copy = ["Battle_Field_Dome", "map_wall_02", "map_wall_03"]
    
    # Extract from game scene
    game_objects = {}
    for obj in game_data.get('objects', []):
        if 'name' in obj and obj['name'] in names_to_copy:
            game_objects[obj['name']] = obj
            print(f"Found {obj['name']} in game scene.")
            
    # Update title scene
    updated_count = 0
    for i, obj in enumerate(title_data.get('objects', [])):
        if 'name' in obj and obj['name'] in names_to_copy:
            if obj['name'] in game_objects:
                title_data['objects'][i] = game_objects[obj['name']]
                updated_count += 1
                print(f"Updated {obj['name']} in title scene.")

    if updated_count > 0:
        with open(title_file, 'w', encoding='utf-8') as f:
            json.dump(title_data, f, indent=4)
        print("Successfully saved updated JSON.")
    else:
        print("No objects updated.")
        
except Exception as e:
    print(f"Error: {e}")
