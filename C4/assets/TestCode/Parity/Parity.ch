// Parity - Phase 0 parity probe suite for docs/compiler/02-program-model.md.
// Each [OK] catalog row that is not already locked elsewhere gets a case here, so the rework has an
// executable oracle: every case must stay green through the Program Model rebuild. Cases are named
// after their catalog row (A5, B2, C1, ...) for direct traceability. The [BUG] rows (A1, A2, A8, A10,
// B1, B5, C5, G2, G6) are intentionally ABSENT - they flip to passing as the rework lands and become
// tests in the commit that fixes them.

void TestParity();
