// Interfaces whose GUID is on a declaration apart from the definition, the way dxcapi.h declares them.
#pragma once
#define CROSS_PLATFORM_UUIDOF(interface, spec) struct __declspec(uuid(spec)) interface;

CROSS_PLATFORM_UUIDOF(IForwardMacro, "4605c4cb-2019-492a-ada4-65f20bb7d67f")
struct IForwardMacro
{
	virtual int Value() = 0;
};

struct __declspec(uuid("228b4687-5a6a-4730-900c-9702b2203f54")) IForwardWritten;
struct IForwardWritten
{
	virtual int Value() = 0;
};
