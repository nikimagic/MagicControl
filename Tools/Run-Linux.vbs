Option Explicit

Dim files, shell, project, portable, executable, command
Set files = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
project = files.GetParentFolderName(files.GetParentFolderName(WScript.ScriptFullName))
portable = files.BuildPath(project, "MagicControl\linux")
executable = files.BuildPath(portable, "MagicControl")

If Not files.FileExists(executable) Then
    MsgBox "Linux MagicControl has not been built: " & executable, vbExclamation, "MagicControl"
    WScript.Quit 1
End If

command = Quote(shell.ExpandEnvironmentStrings("%SystemRoot%\System32\wsl.exe")) _
    & " --cd " & Quote(portable) & " --exec /bin/sh -c " _
    & Quote("exec ./MagicControl >>startup.log 2>&1")
WScript.Quit shell.Run(command, 0, True)

Function Quote(value)
    Quote = Chr(34) & value & Chr(34)
End Function
