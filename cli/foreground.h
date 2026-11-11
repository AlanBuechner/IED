/* What lets the window ied hands a location to come to the front: ied runs from the program in front (a terminal, an
   engine's editor), so it may pass that on (user32). */
__declspec(dllimport) int __stdcall AllowSetForegroundWindow(unsigned long processId);
