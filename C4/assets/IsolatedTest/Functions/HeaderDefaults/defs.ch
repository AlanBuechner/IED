// A parameter's default may be written on the declaration or the definition, and applies to every call (07 7.4,
// BUG-369); written on both, it is the same.
int64 OnDeclaration(int64 v, int64 factor = 2);
int64 OnDefinition(int64 v, int64 factor);
int64 OnBoth(int64 v, int64 factor = 4);
int64 InsideDefiningFile();

class Widget
{
	public int64 Size(int64 extra) const;
	public int64 base;
}
