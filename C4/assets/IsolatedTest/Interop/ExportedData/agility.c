/* Data the executable exports for a loader that finds it by name, as a D3D12 program exports the Agility SDK's
   version and path (docs/language/13 section 6). In C a file-scope const has external linkage. */
__declspec(dllexport) const unsigned int D3D12SDKVersion = 614;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
