#pragma once
#include <optional>
#include <string>

namespace Scripting
{
	/// The operating system a build's program runs on (D-86). What it decides - the object format, the calling
	/// convention, how exceptions unwind, the C library the program links against and which runtime files it gets - is
	/// its TargetAbi's (Target/TargetAbi.h). Every target is x86-64.
	enum class TargetSystem
	{
		Windows, // PE/COFF, the MSVC x64 ABI and C runtime, funclet exceptions
		Linux,   // ELF, the System V ABI, glibc (the sysroot in vendor/sysroots/linux-x64), landing-pad exceptions
	};

	/// The system the compiler itself runs on: what a build targets unless `--target` names another, and what a
	/// script (compiled into the running host) always targets.
	TargetSystem HostSystem();

	/// `windows-x64` or `linux-x64`: the name `--target` takes and Nuke's `{system}-{arch}` pair spells.
	const char* TargetName(TargetSystem system);
	/// The system `name` names (TargetName's names); nothing for another name.
	std::optional<TargetSystem> TargetFromName(const std::string& name);
	/// Every name TargetFromName reads, for a message.
	std::string TargetNames();
} // namespace Scripting
