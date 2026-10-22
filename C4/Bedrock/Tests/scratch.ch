// The test run's own scratch folder, named for its process, so runs at the same time (two checkouts, two sessions)
// never remove or overwrite each other's files.

include "bedrock.ch"

FS::Path TestScratch() {
	Core::StringBuilder name;
	Core::Format(&name, "c4_bedrock_{0}", (int64)Platform::ProcessId());
	FS::Path folder = FS::TempDirectory() / name.View();
	FS::MakeDirectories(folder.CStr());
	return folder;
}
