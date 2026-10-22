// The control pipe's instance (common/userpipe.cpp): its descriptor admits the current user alone, and it is never an
// instance of a pipe another process made first under its name.
#include "testing.h"

#include "userpipe.h"

#include <windows.h>
#include <aclapi.h>

#include <string>
#include <vector>

using namespace fb;

namespace
{
	std::vector<BYTE> CurrentUser() {
		HANDLE token = nullptr;
		OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		std::vector<BYTE> user(size);
		GetTokenInformation(token, TokenUser, user.data(), size, &size);
		CloseHandle(token);
		return user;
	}
}

TEST(UserPipeAdmitsOnlyItsUser) {
	const std::wstring name = L"\\\\.\\pipe\\flashbang-test-" + std::to_wstring(GetCurrentProcessId());
	HANDLE pipe = (HANDLE)NewUserPipe(name.c_str(), 4096);
	CHECK(pipe != nullptr);
	if (!pipe) return;
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	CHECK(GetSecurityInfo(pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
	CHECK(dacl && dacl->AceCount == 1);
	ACCESS_ALLOWED_ACE* ace = nullptr;
	if (dacl && GetAce(dacl, 0, (void**)&ace)) {
		const std::vector<BYTE> user = CurrentUser();
		CHECK(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
		CHECK(!user.empty() && EqualSid((PSID)&ace->SidStart, ((TOKEN_USER*)user.data())->User.Sid));
	}
	LocalFree(descriptor);
	CloseHandle(pipe);
}

TEST(UserPipeNeverJoinsAnotherPipe) {
	// A pipe made first under the name, open to any number of instances: the control pipe must not become one of them.
	const std::wstring name = L"\\\\.\\pipe\\flashbang-squat-" + std::to_wstring(GetCurrentProcessId());
	HANDLE squatter = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
	CHECK(squatter != INVALID_HANDLE_VALUE);
	CHECK(NewUserPipe(name.c_str(), 4096) == nullptr);
	CloseHandle(squatter);
}
