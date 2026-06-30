# imgui-node-editor integration note

Source: https://github.com/thedmd/imgui-node-editor
License: MIT / public-domain style dual license. Keep LICENSE with the imported files.

This folder intentionally contains only the files needed to compile the node editor core with the existing Dear ImGui integration.
Examples, docs, CI files, and bundled external assets were not imported.

Imported source files:
- crude_json.cpp
- imgui_canvas.cpp
- imgui_node_editor.cpp
- imgui_node_editor_api.cpp

Imported headers / inline files:
- crude_json.h
- imgui_bezier_math.h / imgui_bezier_math.inl
- imgui_canvas.h
- imgui_extra_math.h / imgui_extra_math.inl
- imgui_node_editor.h
- imgui_node_editor_internal.h / imgui_node_editor_internal.inl

Engine rule:
- Use this library only as the ImGui-side node canvas/view layer.
- Keep CG2's graph data model, save format, and runtime execution independent from this external UI library.