// A type, an extension method declared for it (defined in main.cf), and one defined here; and the C++ headers whose
// COM interfaces bind a `Render` method (IPicture).
extern "c++" include "picture.hpp" {
	include_dir("C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um");
	include_dir("C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared");
}

struct Box
{
	public int32 v;
}

int32 Draw() extends Box;
int32 Doubled() extends Box { return this.v * 2; }
