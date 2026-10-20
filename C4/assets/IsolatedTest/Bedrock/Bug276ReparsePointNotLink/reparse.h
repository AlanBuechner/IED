#pragma once
// Makes an empty file at `path` that carries a reparse point of a third-party tag (not a link, as a cloud placeholder
// or a deduplicated file is not). 0, or the system's error code.
int MakeTaggedFile(const char* path);
