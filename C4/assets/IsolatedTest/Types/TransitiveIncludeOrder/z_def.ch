// Defines the shared type. Sorts AFTER a_user.ch alphabetically, so under the pre-fix program model
// (which collected headers in unit-id order) a_user.ch was analyzed before Vec existed.
struct Vec { int32 x; int32 y; }
