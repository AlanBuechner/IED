#include "native.h"
#include <stdarg.h>

int64_t c_sum(int32_t count, ...)
{
	va_list ap;
	va_start(ap, count);
	int64_t sum = 0;
	for (int32_t i = 0; i < count; ++i) sum += va_arg(ap, int);
	va_end(ap);
	return sum;
}

double c_average(int32_t count, ...)
{
	va_list ap;
	va_start(ap, count);
	double sum = 0.0;
	for (int32_t i = 0; i < count; ++i) sum += va_arg(ap, double);
	va_end(ap);
	return count ? sum / count : 0.0;
}

typedef int64_t (*Summer)(int32_t count, ...);

int64_t c_calls_back(void* summer)
{
	Summer f = (Summer)summer;
	return f(4, 10, 20, 30, 40) + f(1, 5);
}
