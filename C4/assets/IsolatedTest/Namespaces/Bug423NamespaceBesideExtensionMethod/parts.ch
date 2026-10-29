// A type, an extension method declared for it (defined in main.cf), and one defined here; and the C++ headers whose
// COM interfaces bind a `Render` method (IPicture).
extern "c++" include "picture.hpp"

struct Box
{
	public int32 v;
}

int32 Draw() extends Box;
int32 Doubled() extends Box { return this.v * 2; }
