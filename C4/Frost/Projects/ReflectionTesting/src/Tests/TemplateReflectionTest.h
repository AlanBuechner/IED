#pragma once

#include "UnitTesting/TestBase.h"
#include "Reflection.h"
#include "StaticReflection/ReflectionData.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <list>

namespace Testing
{
	//---------------------------------------------------------------------------
	// Test fixture: simple child struct used as a template argument below.
	//---------------------------------------------------------------------------
	CLASS(GROUP = TemplateFixtures) TemplateTestChild : public Reflect::Reflected
	{
	public:
		REFLECTED_BODY(Testing::TemplateTestChild);

	public:
		PROPERTY(set=SetName)
		Core::String m_Name;

		PROPERTY()
		float m_Score = 0.f;

	public:
		FUNCTION()
		void SetName(const Core::String& p_Name);
	};

	//---------------------------------------------------------------------------
	// Test fixture: exercises every container kind and edge case.
	// HeaderTool must extract template arguments, classify containers, and
	// compute property paths from this class.
	//---------------------------------------------------------------------------
	CLASS(GROUP = TemplateFixtures) TemplateTestAllContainers : public Reflect::Reflected
	{
	public:
		REFLECTED_BODY(Testing::TemplateTestAllContainers);

		// --- Non-template properties (backward compatible: empty args & kind) ---
		PROPERTY()
		int m_Id = 0;

		PROPERTY()
		float m_Value = 0.f;

		PROPERTY()
		bool m_Active = false;

		PROPERTY()
		Core::String m_Label;

		PROPERTY()
		Math::Vector3F m_Position;

		// --- Sequential containers ---
		PROPERTY(set=SetChildren)
		Core::Vector<TemplateTestChild> m_Children;

		PROPERTY()
		std::vector<int> m_Scores;

		PROPERTY()
		std::list<Core::String> m_Tags;

		// --- Associative containers ---
		PROPERTY()
		std::map<Core::String, float> m_Stats;

		PROPERTY()
		std::unordered_map<uint32, Core::String> m_LookupTable;

		// --- Nested template argument (Vector of Vector) ---
		PROPERTY()
		Core::Vector<Core::Vector<int>> m_Grid;

	public:
		FUNCTION(set=SetChildren)
		void SetChildren(const Core::Vector<TemplateTestChild>& p_Children);
	};

	//---------------------------------------------------------------------------
	// Test fixture: only container properties to verify root-level paths.
	//---------------------------------------------------------------------------
	CLASS(GROUP = TemplateFixtures) TemplateTestOnlyContainers : public Reflect::Reflected
	{
	public:
		REFLECTED_BODY(Testing::TemplateTestOnlyContainers);

	public:
		PROPERTY()
		Core::Vector<float> m_Floats;

		PROPERTY()
		std::map<int, int> m_IntMap;
	};

	//---------------------------------------------------------------------------
	// Test fixture: deeply nested template (3 levels) to stress-test extraction.
	//---------------------------------------------------------------------------
	CLASS(GROUP = TemplateFixtures) TemplateTestDeepNesting : public Reflect::Reflected
	{
	public:
		REFLECTED_BODY(Testing::TemplateTestDeepNesting);

	public:
		PROPERTY()
		Core::Vector<std::map<Core::String, float>> m_DeepData;
	};

	//---------------------------------------------------------------------------
	// Unit tests for HeaderTool template argument extraction, container
	// classification, and property path computation.
	//---------------------------------------------------------------------------
	CLASS(GROUP = Testing) TemplateReflectionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TemplateReflectionTest);

		/// Verify that a non-template property has empty template args and
		/// container kind in the generated .reflected YAML.
		TEST_FUNC(TemplateReflection.NonTemplate)
		void TestNonTemplateProperty();

		/// Verify Core::Vector<T> is classified as "sequential" with correct
		/// template argument extraction.
		TEST_FUNC(TemplateReflection.SequentialCoreVector)
		void TestSequentialCoreVector();

		/// Verify std::vector<T> and std::list<T> are classified as "sequential".
		TEST_FUNC(TemplateReflection.SequentialStdContainers)
		void TestSequentialStdContainers();

		/// Verify std::map<K,V> is classified as "associative" with two template
		/// arguments.
		TEST_FUNC(TemplateReflection.AssociativeMap)
		void TestAssociativeMap();

		/// Verify std::unordered_map<K,V> is classified as "associative".
		TEST_FUNC(TemplateReflection.AssociativeUnorderedMap)
		void TestAssociativeUnorderedMap();

		/// Verify nested template arguments (Vector<Vector<int>>) are extracted
		/// correctly.
		TEST_FUNC(TemplateReflection.NestedTemplateArgs)
		void TestNestedTemplateArgs();

		/// Verify deeply nested templates (3 levels) are handled.
		TEST_FUNC(TemplateReflection.DeepNesting)
		void TestDeepNesting();

		/// Verify property paths are computed correctly for root-level properties.
		TEST_FUNC(TemplateReflection.PropertyPaths)
		void TestPropertyPaths();

		/// Verify backward compatibility: loading a .reflected file without
		/// TemplateArgs/ContainerKind fields does not crash.
		TEST_FUNC(TemplateReflection.BackwardCompatibility)
		void TestBackwardCompatibility();
	};
}
