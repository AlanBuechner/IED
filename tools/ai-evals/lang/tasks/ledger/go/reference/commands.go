package main

import (
	"cmp"
	"fmt"
	"maps"
	"slices"
	"strings"
)

const tenthsPerWhole = 1000

// CategorySpending is the money spent in one category, as a positive amount.
type CategorySpending struct {
	Category string
	Cents    int64
}

// Filter keeps the transactions of account (every one when empty) in month (YYYY-MM; every one when empty).
func Filter(rows []Transaction, account, month string) []Transaction {
	var kept []Transaction
	for _, row := range rows {
		if (account == "" || row.Account == account) && strings.HasPrefix(row.Date, month) {
			kept = append(kept, row)
		}
	}
	return kept
}

// Balance is each account's balance, by account name: "checking 1234.56".
func Balance(rows []Transaction) string {
	balances := make(map[string]int64)
	for _, row := range rows {
		balances[row.Account] += row.Cents
	}
	var out strings.Builder
	for _, account := range slices.Sorted(maps.Keys(balances)) {
		out.WriteString(account)
		out.WriteByte(' ')
		out.WriteString(FormatMoney(balances[account]))
		out.WriteByte('\n')
	}
	return out.String()
}

// List is the transactions in file order: "date account category amount memo".
func List(rows []Transaction) string {
	var out strings.Builder
	for _, row := range rows {
		out.WriteString(row.Date)
		out.WriteByte(' ')
		out.WriteString(row.Account)
		out.WriteByte(' ')
		out.WriteString(row.Category)
		out.WriteByte(' ')
		out.WriteString(FormatMoney(row.Cents))
		out.WriteByte(' ')
		out.WriteString(row.Memo)
		out.WriteByte('\n')
	}
	return out.String()
}

// PercentTenths is a share of whole in tenths of a percent, rounded half up: 1 of 2000 is 1 (0.1%).
func PercentTenths(part, whole int64) int64 {
	return (part*2*tenthsPerWhole + whole) / (2 * whole)
}

func spendingByCategory(rows []Transaction) []CategorySpending {
	spent := make(map[string]int64)
	for _, row := range rows {
		if row.Cents < 0 {
			spent[row.Category] -= row.Cents
		}
	}
	ranked := make([]CategorySpending, 0, len(spent))
	for category, cents := range spent {
		ranked = append(ranked, CategorySpending{category, cents})
	}
	slices.SortFunc(ranked, func(a, b CategorySpending) int {
		return cmp.Or(cmp.Compare(b.Cents, a.Cents), strings.Compare(a.Category, b.Category))
	})
	return ranked
}

// Report is the spending per category, the most first: "groceries 218.75 6.4%", then "total 3428.05";
// "no spending" when nothing was spent.
func Report(rows []Transaction) string {
	ranked := spendingByCategory(rows)
	var total int64
	for _, each := range ranked {
		total += each.Cents
	}
	if total == 0 {
		return "no spending\n"
	}
	var out strings.Builder
	for _, each := range ranked {
		tenths := PercentTenths(each.Cents, total)
		fmt.Fprintf(&out, "%s %s %d.%d%%\n", each.Category, FormatMoney(each.Cents), tenths/10, tenths%10)
	}
	out.WriteString("total " + FormatMoney(total) + "\n")
	return out.String()
}
