#pragma once
struct __declspec(uuid("905db94b-a00c-4140-9df5-2b64ca9ea357")) IFoo {
    virtual int Ping() = 0;
};
struct NoUuid { int x; };
