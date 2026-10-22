include "report.ch"

namespace JsonAgg
{
	// The report as JSON text: money as numbers of dollars, a customer's favorite sku null when it has none.
	Core::String ReportJson(const Report& report);
}
