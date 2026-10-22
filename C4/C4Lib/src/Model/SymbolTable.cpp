#include "Model/SymbolTable.h"

namespace Scripting
{
	namespace
	{
		// The ScopeCursor of the lowering running on this thread, innermost first (each points at its outer one).
		thread_local ScopeCursor* t_Cursor = nullptr;
	}

	// --- Scope ---

	bool Scope::Register(const Core::String& name, const SymbolInfo& info, const FunctionRecord* fn)
	{
		if (m_Writes) m_Writes->Ask("a name in a permanent scope");
		if (!m_Symbols) m_Symbols = std::make_unique<Symbols>();
		auto it = m_Symbols->find(name);
		if (it != m_Symbols->end())
		{
			Core::Vector<std::unique_ptr<SymbolInfo>>& entries = it->second;
			for (auto& e : entries)
			{
				SymbolInfo& existing = *e;
				if (existing.kind == SymbolKind::Function && info.kind == SymbolKind::Function)
				{
					// Overload set grows (doc 08): one name, many signatures. A set with one member
					// behaves exactly like the old single-symbol case - nothing downstream branches.
					if (fn) existing.functions.Push(fn);
					return true;
				}
			}
			// Same-name non-function redeclaration: legal only when both are static (internal
			// linkage keeps them distinct symbols). Anything else is a conflict the builder reports.
			for (auto& e : entries)
				if (!e->isStatic || !info.isStatic) return false;
		}

		auto stored = std::make_unique<SymbolInfo>(info);
		stored->declaringScope = this;
		if (fn) stored->functions.Push(fn);
		(*m_Symbols)[name].Push(std::move(stored));
		return true;
	}

	// Lowering asks each block layer of a body for every name it resolves, and most layers hold a few names or none:
	// those are compared, not hashed.
	const Core::Vector<std::unique_ptr<SymbolInfo>>& Scope::Entries(const Core::String& name) const
	{
		static const Core::Vector<std::unique_ptr<SymbolInfo>> kEmpty;
		if (!m_Symbols) return kEmpty;
		if (m_Symbols->size() <= kComparedNames)
		{
			for (const auto& [held, entries] : *m_Symbols)
				if (held == name) return entries;
			return kEmpty;
		}
		auto it = m_Symbols->find(name);
		return it != m_Symbols->end() ? it->second : kEmpty;
	}

	const SymbolInfo* Scope::Find(const Core::String& name) const
	{
		if (m_Symbols)
			if (auto it = m_Symbols->find(name); it != m_Symbols->end() && !it->second.Empty()) return it->second[0].get();
		return m_Parent ? m_Parent->Find(name) : nullptr;
	}

	void Scope::SetFunctionVisibilityUnit(const Core::String& name, uint32 unit)
	{
		if (!m_Symbols) return;
		auto it = m_Symbols->find(name);
		if (it == m_Symbols->end()) return;
		for (auto& e : it->second)
			if (e->kind == SymbolKind::Function && e->definingUnit != unit) e->definingUnit = unit;
	}

	// From the back: the layer a guard pops is the last one pushed under its parent, which may hold many.
	std::unique_ptr<Scope> Scope::TakeChild(Scope* child)
	{
		for (uint32 i = m_Children.Count(); i-- > 0;)
		{
			if (m_Children[i].get() != child) continue;
			std::unique_ptr<Scope> taken = std::move(m_Children[i]);
			m_Children.Remove(m_Children.begin() + i);
			return taken;
		}
		return nullptr;
	}

	void Scope::Enumerate(Core::Vector<std::pair<Core::String, SymbolInfo*>>& out) const
	{
		if (!m_Symbols) return;
		for (const auto& [name, entries] : *m_Symbols)
			for (auto& e : entries) out.Push({ name, e.get() });
	}

	// --- ScopeGuard ---

	// --- ScopeCursor ---

	ScopeCursor::ScopeCursor(SymbolTable& table) : m_Table(table), m_Outer(t_Cursor), m_Position(table.Current())
	{
		t_Cursor = this;
	}

	ScopeCursor::~ScopeCursor()
	{
		t_Cursor = m_Outer;
		m_Table.Adopt(*this);
	}

	const std::vector<SymbolInfo>* ScopeCursor::KeepPack(std::vector<SymbolInfo> elements)
	{
		m_Packs->push_back(std::move(elements));
		return &m_Packs->back();
	}

	// --- ScopeGuard ---

	ScopeGuard::ScopeGuard(SymbolTable* table, ScopeCursor* cursor, Scope* scope) : m_Table(table), m_Cursor(cursor), m_Scope(scope) {}

	ScopeGuard::~ScopeGuard()
	{
		if (!m_Scope) return;
		if (m_Cursor)
		{
			if (m_Cursor->m_Position == m_Scope) m_Cursor->m_Position = m_Scope->GetParent();
			return; // the cursor keeps the layer until it ends
		}
		if (m_Table->m_Current == m_Scope) m_Table->m_Current = m_Scope->GetParent();
		// A popped layer leaves the tree but stays alive: records made in it (a local function) keep naming it.
		if (m_Scope->GetParent()) m_Table->m_Retired.Push(m_Scope->GetParent()->TakeChild(m_Scope));
	}

	// --- SymbolTable ---

	SymbolTable::SymbolTable(const ModelWrites* writes) : m_Writes(writes)
	{
		m_Root = std::make_unique<Scope>(Scope::Kind::File, "program");
		m_Root->m_Writes = writes;
		m_Current = m_Root.get();
	}

	ScopeCursor* SymbolTable::ActiveCursor() const
	{
		return t_Cursor && &t_Cursor->m_Table == this ? t_Cursor : nullptr;
	}

	Scope* SymbolTable::Current() const
	{
		const ScopeCursor* cursor = ActiveCursor();
		return cursor ? cursor->m_Position : m_Current;
	}

	ScopeGuard SymbolTable::PushScope(Scope::Kind kind, const Core::String& name)
	{
		auto scope = std::make_unique<Scope>(kind, name);
		Scope* raw = scope.get();
		if (ScopeCursor* cursor = ActiveCursor())
		{
			raw->m_Parent = cursor->m_Position;
			cursor->m_Layers.Push(std::move(scope));
			cursor->m_Position = raw;
			return ScopeGuard(this, cursor, raw);
		}
		raw->m_Parent = m_Current;
		m_Current->m_Children.Push(std::move(scope));
		m_Current = raw; // the newest layer is the lowering position until its guard pops
		return ScopeGuard(this, nullptr, raw);
	}

	void SymbolTable::Adopt(ScopeCursor& cursor)
	{
		const std::lock_guard<std::mutex> adopting(m_Adopting);
		for (auto& layer : cursor.m_Layers) m_Retired.Push(std::move(layer));
		m_RetiredPacks.Push(std::move(cursor.m_Packs));
	}

	Scope* SymbolTable::CreateScope(Scope::Kind kind, const Core::String& name, Scope* parent)
	{
		Scope* base = parent ? parent : Current();
		auto scope = std::make_unique<Scope>(kind, name);
		Scope* raw = scope.get();
		raw->m_Parent = base;
		// A block made while a lowering runs (a template's binding, a pack element's) holds names, never storage, and
		// no walk of the tree looks into blocks: it is the lowering's, kept as long as the model like its layers.
		if (ScopeCursor* cursor = ActiveCursor(); cursor && kind == Scope::Kind::Block)
		{
			cursor->m_Layers.Push(std::move(scope));
			return raw;
		}
		if (m_Writes) m_Writes->Ask("a class or namespace scope");
		raw->m_Writes = m_Writes;
		base->m_Children.Push(std::move(scope));
		return raw; // permanent: owned by its parent, not the lowering position
	}

	Core::String FunctionSymbol(const Scope* scope, const Core::String& name, const FunctionType& signature)
	{
		return CanonicalSymbolName(scope, name) + "_" + signature.OverloadSuffix();
	}

	Core::String MethodSymbol(const Scope* typeScope, const ObjectType& owner, const Core::String& name,
		const FunctionType& signature, bool isConst)
	{
		const Core::String symbol = typeScope ? FunctionSymbol(typeScope, name, signature)
			: owner.GetName() + "_" + name + "_" + signature.OverloadSuffix();
		return isConst ? symbol + "_const" : symbol;
	}

	const FunctionRecord* SymbolTable::AddFunctionRecord(FunctionRecord record)
	{
		if (m_Writes) m_Writes->Ask("a function record");
		if (!record.isBuiltin && !record.isLinked && !record.mangledName.empty())
			for (uint32 n = 1; !m_ClaimedSymbols.insert(record.mangledName).second; ++n)
				record.mangledName += "_" + std::to_string(n);
		record.isHostDefined = m_HostSymbols && !HasBody(record) && !record.isLinked && m_HostSymbols->count(record.mangledName.c_str());
		auto rec = std::make_unique<FunctionRecord>(std::move(record));
		const FunctionRecord* raw = rec.get();
		m_Records.Push(std::move(rec));
		return raw;
	}

	void SymbolTable::SetIncludeClosure(uint32 unitId, const Core::Vector<uint32>& closure)
	{
		std::unordered_set<uint32> set(closure.begin(), closure.end());
		m_Closures[unitId] = std::move(set);
	}

	bool SymbolTable::IsVisible(uint32 definingUnit, uint32 askingUnit) const
	{
		if (askingUnit == 0) return true; // builtin / no-context queries see everything
		if (definingUnit == 0 || definingUnit == askingUnit) return true; // builtin / same file
		auto it = m_Closures.find(askingUnit);
		return it != m_Closures.end() && it->second.count(definingUnit) != 0;
	}

	bool SymbolTable::DeclarationVisible(const FunctionRecord& function, uint32 askingUnit) const
	{
		if (IsVisible(function.declUnit, askingUnit)) return true;
		for (uint32 unit : function.declUnits)
			if (IsVisible(unit, askingUnit)) return true;
		return false;
	}

	const SymbolInfo* SymbolTable::FindVisible(const Scope* from, const Core::String& name, uint32 askingUnit) const
	{
		if (!from) return nullptr;
		// Innermost layer first: the shadowing rule. Within a layer, the asking unit's own
		// declaration wins (file-scope statics), then any visible entry.
		for (const Scope* s = from; s; s = s->GetParent())
		{
			const auto& entries = s->Entries(name);
			if (entries.Empty()) continue;
			const SymbolInfo* fallback = nullptr;
			for (auto& e : entries)
			{
			bool visible = IsVisible(e->definingUnit, askingUnit);
		if (!visible && e->kind == SymbolKind::Function)
		{
			// A merged overload set can hold records from several units; the entry's single
				// definingUnit may have been re-pointed to a DEFINITION unit by body-attach. Visibility
				// must be judged by where each record was DECLARED (declUnit, stable), not its lowering
				// unit - otherwise a header-declared function whose body lives in another file becomes
				// invisible to units that include only the header. The set is visible if ANY signature's
				// declaration is visible to the asking unit.
				for (const FunctionRecord* rec : e->functions)
					if (rec && DeclarationVisible(*rec, askingUnit)) { visible = true; break; }
			}
				if (!visible) continue;
				if (e->definingUnit == askingUnit) return e.get(); // own declaration wins
				if (!fallback) fallback = e.get();
			}
			if (fallback) return fallback;
		}
		return nullptr;
	}

	void SymbolTable::CollectGlobalVariables(const std::function<void(const SymbolInfo&)>& visit) const
	{
		ForEachGlobalVariable([&](const SymbolInfo& info, const Scope&) { visit(info); });
	}

	void SymbolTable::ForEachScopedSymbol(const std::function<void(const Core::String&, SymbolInfo*, const Scope&)>& visit) const
	{
		std::function<void(const Scope&)> walk = [&](const Scope& s)
		{
			Core::Vector<std::pair<Core::String, SymbolInfo*>> symbols;
			s.Enumerate(symbols);
			for (const auto& [name, info] : symbols) visit(name, info, s);
			for (const auto& child : s.GetChildren()) walk(*child);
		};
		walk(*m_Root);
	}

	void SymbolTable::ForEachGlobalVariable(const std::function<void(const SymbolInfo&, const Scope&)>& visit) const
	{
		std::function<void(const Scope*)> walk = [&](const Scope* s)
		{
			if (!s) return;
			switch (s->GetKind())
			{
				case Scope::Kind::Function:
				case Scope::Kind::Block:
					return; // locals: no backing storage, never reflected
				default:
					break;
			}
			s->ForEachSymbol([&](const SymbolInfo& info) { if (info.kind == SymbolKind::Variable) visit(info, *s); });
			for (const auto& child : s->GetChildren())
				walk(child.get());
		};
		walk(m_Root.get());
	}

} // namespace Scripting
