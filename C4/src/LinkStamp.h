#pragma once
#include <string>
#include <vector>

/// What a program's last link read and left, kept beside its intermediates, so a build whose link would read the same
/// files, unchanged, with the same command line, into a program and .pdb nothing has touched since, skips the link: a
/// source saved without a change, or an edit that leaves every object as it was. A file counts by its size and time.
class LinkStamp
{
public:
	LinkStamp(std::string path, const std::vector<std::string>& inputs, const std::vector<std::string>& arguments,
		std::vector<std::string> outputs);

	/// The last link read these inputs and its outputs are as it left them.
	bool Current() const;
	/// After a link that succeeded, with its outputs in place.
	void Record() const;
	/// Before a link: one that fails or stops leaves no stamp.
	void Forget() const;

private:
	std::string Contents() const;

	std::string m_Path;
	std::string m_Inputs; // a hash of the inputs' states and the arguments
	std::vector<std::string> m_Outputs;
};
