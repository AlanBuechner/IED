#include "OverloadResolutionTest.h"

namespace Testing
{
	// Shared (interned) primitive per kind - mirrors production where each primitive is a singleton
	// in the TypeTable, so "exact match" means pointer identity. Returning the SAME instance for a
	// given kind is what makes the resolver's exact-match path (p == a) behave as it does in the real
	// compiler; fresh instances per call would wrongly force the conversion path.
	static const Scripting::Type* SharedPrim(Scripting::TypeKind kind)
	{
		static Core::Vector<Scripting::PrimitiveType*> table(64, nullptr);
		const int32 idx = (int32)kind;
		if (!table[idx])
			table[idx] = new Scripting::PrimitiveType(kind);
		return table[idx];
	}

	// Build a function type returning int32 with the given parameter kinds.
	static const Scripting::FunctionType* MakeFn(const Core::Vector<Scripting::TypeKind>& paramKinds)
	{
		const auto* ret = SharedPrim(Scripting::TypeKind::Int32);
		Core::Vector<Scripting::FunctionType::ParameterInfo> params;
		for (uint32 i = 0; i < paramKinds.Count(); ++i) {
			Scripting::FunctionType::ParameterInfo p;
			p.mName = "a";
			p.m_Type = SharedPrim(paramKinds[i]);
			params.Push(p);
		}
		return new Scripting::FunctionType(ret, params);
	}

	static Core::Vector<const Scripting::Type*> MakeArgs(const Core::Vector<Scripting::TypeKind>& kinds)
	{
		Core::Vector<const Scripting::Type*> args;
		for (uint32 i = 0; i < kinds.Count(); ++i)
			args.Push(SharedPrim(kinds[i]));
		return args;
	}

	void OverloadResolutionTest::TestSingleCandidateExactMatch()
	{
		const auto* sig = MakeFn({ Scripting::TypeKind::Int32 });
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ sig, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == sig);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestPicksExactOverConverted()
	{
		const auto* exact = MakeFn({ Scripting::TypeKind::Int32 });   // int32 arg -> cost 0
		const auto* conv  = MakeFn({ Scripting::TypeKind::Float32 });  // int32 arg -> cost 1 (numeric)
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ conv, nullptr, 1 });
		cands.Push({ exact, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == exact); // cost 0 beats cost 1
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestFewerTotalConversionsWins()
	{
		// (int32,float) vs (float,float) for args (int32,float): first costs 0, second costs 1.
		const auto* best  = MakeFn({ Scripting::TypeKind::Int32, Scripting::TypeKind::Float32 });
		const auto* worse = MakeFn({ Scripting::TypeKind::Float32, Scripting::TypeKind::Float32 });
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ worse, nullptr, 2 });
		cands.Push({ best, nullptr, 2 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32, Scripting::TypeKind::Float32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == best);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestDefaultedTailViaMinArity()
	{
		// Candidate has 2 params but minArity 1 (second is defaulted): a 1-arg call is viable.
		const auto* sig = MakeFn({ Scripting::TypeKind::Int32, Scripting::TypeKind::Int32 });
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ sig, nullptr, 1 }); // minArity 1 -> Add(5) is viable
		auto args = MakeArgs({ Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == sig);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestTooManyArgsForCandidateSkipsIt()
	{
		const auto* sig = MakeFn({ Scripting::TypeKind::Int32 }); // 1 param, minArity 1
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ sig, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32, Scripting::TypeKind::Int32 }); // 2 > 1 -> not viable

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::NoMatch);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestAmbiguousWhenTied()
	{
		// Two DISTINCT signatures that tie: (int32,float) and (float,int32) both cost 1 for
		// args (int32,int32). A human must break this with a cast, so it is reported as ambiguous.
		const auto* s1 = MakeFn({ Scripting::TypeKind::Int32, Scripting::TypeKind::Float32 });
		const auto* s2 = MakeFn({ Scripting::TypeKind::Float32, Scripting::TypeKind::Int32 });
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ s1, nullptr, 2 });
		cands.Push({ s2, nullptr, 2 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32, Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Ambiguous);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestNoMatchWhenNothingViable()
	{
		// Candidate takes ZERO params (minArity 0); supplying one int32 arg exceeds its param count,
		// so it is not viable and nothing else is -> NoMatch.
		const auto* sig = MakeFn({}); // no parameters
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ sig, nullptr, 0 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 }); // 1 arg > 0 params -> not viable

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::NoMatch);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestEmptyCandidateSetIsNoMatch()
	{
		Core::Vector<Scripting::OverloadCandidate> cands;
		Core::Vector<const Scripting::Type*> args;
		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::NoMatch);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestReferenceParamBindsByExactReferent()
	{
		const auto* int32 = SharedPrim(Scripting::TypeKind::Int32);
		static Scripting::ReferenceType ref(int32);   // int32&
		const auto* f32 = SharedPrim(Scripting::TypeKind::Float32);
		static Scripting::ReferenceType fref(f32);    // float& cannot bind an int32 lvalue

		auto mk = [&](const Scripting::Type* paramType) {
			Core::Vector<Scripting::FunctionType::ParameterInfo> params;
			Scripting::FunctionType::ParameterInfo p; p.mName = "a"; p.m_Type = paramType;
			params.Push(p);
			return new Scripting::FunctionType(SharedPrim(Scripting::TypeKind::Int32), params);
		};
		const auto* refSig  = mk(&ref);   // int32& binds the int32 lvalue -> cost 0
		const auto* badRef  = mk(&fref);  // float& vs int32 arg -> not viable

		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ badRef, nullptr, 1 });
		cands.Push({ refSig, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == refSig);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestEnumConvertsToUnderlyingInt()
	{
		const auto* int32 = SharedPrim(Scripting::TypeKind::Int32);
		static Scripting::EnumType color("Color", int32);
		auto mk = [&](const Scripting::Type* paramType) {
			Core::Vector<Scripting::FunctionType::ParameterInfo> params;
			Scripting::FunctionType::ParameterInfo p; p.mName = "a"; p.m_Type = paramType;
			params.Push(p);
			return new Scripting::FunctionType(SharedPrim(Scripting::TypeKind::Int32), params);
		};
		const auto* intSig  = mk(int32);   // takes int32
		const auto* enumSig = mk(&color);  // takes Color (enum over int32)

		// Arg is an int32: exact for the int param (cost 0), converts to the enum param (cost 1).
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ enumSig, nullptr, 1 });
		cands.Push({ intSig, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == intSig); // exact beats enum conversion
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestAliasUnwrapsForMatching()
	{
		const auto* int32 = SharedPrim(Scripting::TypeKind::Int32);
		static Scripting::AliasType myInt("MyInt", int32); // alias of int32
		Core::Vector<Scripting::FunctionType::ParameterInfo> params;
		Scripting::FunctionType::ParameterInfo p; p.mName = "a"; p.m_Type = &myInt; // param is the ALIAS
		params.Push(p);
		const auto* sig = new Scripting::FunctionType(SharedPrim(Scripting::TypeKind::Int32), params);

		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ sig, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 }); // underlying type; alias must unwrap to match (cost 0)

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == sig);
		TEST_PASSED();
	}

	void OverloadResolutionTest::TestBug415SameTypesDifferentNamesIsOneFunction()
	{
		auto mk = [](const char* name) {
			Core::Vector<Scripting::FunctionType::ParameterInfo> params;
			Scripting::FunctionType::ParameterInfo p; p.mName = name; p.m_Type = SharedPrim(Scripting::TypeKind::Int32);
			params.Push(p);
			return new Scripting::FunctionType(SharedPrim(Scripting::TypeKind::Void), params);
		};
		const auto* derived = mk("value");   // Middle<Leaf>::Init(int32 value), made with the instance
		const auto* base = mk("");           // Base::Init(int32), the interned one
		Core::Vector<Scripting::OverloadCandidate> cands;
		cands.Push({ derived, nullptr, 1 });
		cands.Push({ base, nullptr, 1 });
		auto args = MakeArgs({ Scripting::TypeKind::Int32 });

		auto r = Scripting::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == Scripting::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == derived); // the first, most derived

		// Different parameter types still tie as two functions.
		Core::Vector<Scripting::OverloadCandidate> distinct;
		distinct.Push({ MakeFn({ Scripting::TypeKind::Int32, Scripting::TypeKind::Float32 }), nullptr, 2 });
		distinct.Push({ MakeFn({ Scripting::TypeKind::Float32, Scripting::TypeKind::Int32 }), nullptr, 2 });
		auto r2 = Scripting::OverloadResolver::Resolve(distinct, MakeArgs({ Scripting::TypeKind::Int32, Scripting::TypeKind::Int32 }));
		TEST_ASSERT(r2.status == Scripting::OverloadResolution::Status::Ambiguous);
		TEST_PASSED();
	}
} // namespace Testing
