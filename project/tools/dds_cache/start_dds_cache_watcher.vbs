Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
toolDir = fso.GetParentFolderName(WScript.ScriptFullName)
projectDir = fso.GetParentFolderName(fso.GetParentFolderName(toolDir))
command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File """ & toolDir & "\dds_cache_builder.ps1"" -Watch -Interval 5 -RequestOnly"
shell.CurrentDirectory = projectDir
shell.Run command, 0, False
