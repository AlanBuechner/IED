package main

import (
	"maps"
	"slices"
	"strings"
)

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

// List is the transactions in file order, those of account only when it is not empty: "date account category amount memo".
func List(rows []Transaction, account string) string {
	var out strings.Builder
	for _, row := range rows {
		if account != "" && row.Account != account {
			continue
		}
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
