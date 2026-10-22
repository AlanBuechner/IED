package main

import (
	"fmt"
	"strings"
)

const (
	centsPerUnit = 100
	centDigits   = 2
)

func parseDigits(text string) (int64, bool) {
	if text == "" {
		return 0, false
	}
	var value int64
	for i := 0; i < len(text); i++ {
		c := text[i]
		if c < '0' || c > '9' {
			return 0, false
		}
		value = value*10 + int64(c-'0')
	}
	return value, true
}

func parseFraction(decimals string) (int64, bool) {
	if len(decimals) != centDigits {
		return 0, false
	}
	return parseDigits(decimals)
}

// ParseCents reads an amount such as "12.34", "-0.50" or "7" as cents; false for anything else.
func ParseCents(text string) (int64, bool) {
	rest, negative := strings.CutPrefix(text, "-")
	whole, decimals, hasDot := strings.Cut(rest, ".")
	units, ok := parseDigits(whole)
	if !ok {
		return 0, false
	}
	var fraction int64
	if hasDot {
		if fraction, ok = parseFraction(decimals); !ok {
			return 0, false
		}
	}
	cents := units*centsPerUnit + fraction
	if negative {
		cents = -cents
	}
	return cents, true
}

// FormatMoney writes cents as an amount with two decimals: -1234 is "-12.34".
func FormatMoney(cents int64) string {
	sign := ""
	if cents < 0 {
		sign = "-"
		cents = -cents
	}
	return fmt.Sprintf("%s%d.%02d", sign, cents/centsPerUnit, cents%centsPerUnit)
}
