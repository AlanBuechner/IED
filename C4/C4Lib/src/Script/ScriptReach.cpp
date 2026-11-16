#include "Script/ScriptReach.h"
#include "Script/TemplateNames.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		// The host's projects, then one per layer, lowest first: what each depends on, by index.
		class ProjectGraph
		{
		public:
			ProjectGraph(const HostInterface& host, const std::vector<const ScriptChain*>& layers)
			{
				for (uint32 p = 0; p < host.projects.size(); ++p)
				{
					m_IndexOf.emplace(host.projects[p].name, p);
					m_Depends.push_back(host.projects[p].dependencies);
				}
				for (const ScriptChain* layer : layers)
				{
					m_Depends.push_back(Indexes(layer->layer.depends));
					m_IndexOf.emplace(layer->layer.project, static_cast<uint32>(m_Depends.size() - 1));
				}
			}

			bool Has(const std::string& name) const { return m_IndexOf.count(name) != 0; }
			uint32 LayerProject(size_t layer, const HostInterface& host) const { return static_cast<uint32>(host.projects.size() + layer); }

			std::vector<uint32> Indexes(const std::vector<std::string>& names) const
			{
				std::vector<uint32> indexes;
				for (const std::string& name : names)
					if (auto it = m_IndexOf.find(name); it != m_IndexOf.end()) indexes.push_back(it->second);
				return indexes;
			}

			// `from` and everything it depends on, directly or not, into `reach`.
			void Reach(const std::vector<uint32>& from, std::vector<bool>& reach) const
			{
				std::vector<uint32> pending = from;
				while (!pending.empty())
				{
					const uint32 at = pending.back();
					pending.pop_back();
					if (at >= reach.size() || reach[at]) continue;
					reach[at] = true;
					pending.insert(pending.end(), m_Depends[at].begin(), m_Depends[at].end());
				}
			}

			size_t Count() const { return m_Depends.size(); }

		private:
			std::unordered_map<std::string, uint32> m_IndexOf;
			std::vector<std::vector<uint32>> m_Depends;
		};

		// One interface or layer file as the reach sees it.
		struct ReachUnit
		{
			uint32 id = 0;
			uint32 project = 0;
			bool library = false;
			const InterfaceFile* file = nullptr;
		};

		std::vector<ReachUnit> UnitsOf(const HostInterface& host, const std::vector<const ScriptChain*>& layers, const ProjectGraph& graph)
		{
			std::vector<ReachUnit> units;
			for (const InterfaceFile& file : host.files) units.push_back({ file.unitId, file.project, file.library, &file });
			for (size_t i = 0; i < layers.size(); ++i)
				for (const InterfaceFile& file : layers[i]->layer.files) units.push_back({ file.unitId, graph.LayerProject(i, host), false, &file });
			return units;
		}

		bool Reached(const ReachUnit& unit, const std::vector<bool>& reach) { return unit.library || (unit.project < reach.size() && reach[unit.project]); }

		// What the reached files declare: a file adding to one of these is read with its project's reach.
		std::unordered_set<std::string> DeclaredTemplates(const std::vector<ReachUnit>& units, const std::vector<bool>& reach)
		{
			std::unordered_set<std::string> declared;
			for (const ReachUnit& unit : units)
				if (Reached(unit, reach)) declared.insert(unit.file->templates.begin(), unit.file->templates.end());
			return declared;
		}

		bool AddsTo(const InterfaceFile& file, const std::unordered_set<std::string>& declared)
		{
			for (const std::string& addition : file.extendedTemplates)
				for (const std::string& target : TemplateAdditionTargets(addition))
					if (declared.count(target)) return true;
			return false;
		}

		// The units of each symbol two functions had, the host's and the layers'.
		std::vector<const std::vector<uint32>*> CollisionFamilies(const HostInterface& host, const std::vector<const ScriptChain*>& layers)
		{
			std::vector<const std::vector<uint32>*> families;
			for (const CollidedSymbol& symbol : host.collidedSymbols) families.push_back(&symbol.units);
			for (const ScriptChain* layer : layers)
				for (const CollidedSymbol& symbol : layer->layer.collidedSymbols) families.push_back(&symbol.units);
			return families;
		}

		// Reading one file of a template, or one unit of a collided symbol, reads the others' projects too: the files that add
		// to a template it declares, every unit whose function had that symbol. True when the reach grew.
		bool CloseOver(const ProjectGraph& graph, const std::vector<ReachUnit>& units, const std::unordered_map<uint32, size_t>& indexOf,
			const std::vector<const std::vector<uint32>*>& families, std::vector<bool>& reach)
		{
			bool grew = false;
			const std::unordered_set<std::string> declared = DeclaredTemplates(units, reach);
			for (const ReachUnit& unit : units)
				if (!Reached(unit, reach) && AddsTo(*unit.file, declared))
				{
					graph.Reach({ unit.project }, reach);
					grew = true;
				}
			for (const std::vector<uint32>* family : families)
			{
				bool any = false;
				for (uint32 id : *family)
					if (auto at = indexOf.find(id); at != indexOf.end() && Reached(units[at->second], reach)) any = true;
				if (!any) continue;
				for (uint32 id : *family)
					if (auto at = indexOf.find(id); at != indexOf.end() && !Reached(units[at->second], reach))
					{
						graph.Reach({ units[at->second].project }, reach);
						grew = true;
					}
			}
			return grew;
		}
	}

	std::vector<std::string> ProjectsBelow(const HostInterface& host, const std::vector<const ScriptChain*>& layers)
	{
		std::vector<std::string> below;
		if (host.rootProject < host.projects.size()) below.push_back(host.projects[host.rootProject].name);
		for (const ScriptChain* layer : layers) below.push_back(layer->layer.project);
		return below;
	}

	std::vector<std::string> ScriptDependencies(const HostInterface& host, const std::vector<const ScriptChain*>& layers,
		const std::vector<std::string>& allow, bool onlyAllowed)
	{
		const ProjectGraph graph(host, layers);
		std::vector<std::string> depends;
		for (const std::string& entry : allow)
		{
			if (!graph.Has(entry)) return ProjectsBelow(host, layers);
			if (std::find(depends.begin(), depends.end(), entry) == depends.end()) depends.push_back(entry);
		}
		if (depends.empty() && !onlyAllowed) return ProjectsBelow(host, layers);
		return depends;
	}

	std::vector<uint32> ReachedUnits(const HostInterface& host, const std::vector<const ScriptChain*>& layers, const std::vector<std::string>& depends)
	{
		const ProjectGraph graph(host, layers);
		std::vector<bool> reach(graph.Count(), false);
		graph.Reach(graph.Indexes(depends), reach);
		const std::vector<ReachUnit> units = UnitsOf(host, layers, graph);
		std::unordered_map<uint32, size_t> indexOf;
		for (size_t i = 0; i < units.size(); ++i) indexOf.emplace(units[i].id, i);
		const std::vector<const std::vector<uint32>*> families = CollisionFamilies(host, layers);
		while (CloseOver(graph, units, indexOf, families, reach)) {}
		std::vector<uint32> reached;
		for (const ReachUnit& unit : units)
			if (Reached(unit, reach)) reached.push_back(unit.id);
		std::sort(reached.begin(), reached.end());
		return reached;
	}
} // namespace Scripting
