package main

import "testing"

func TestParsesALine(t *testing.T) {
	line, err := ParseLine("1001,alice,gold,widget,3,2.50")
	if err != nil {
		t.Fatal(err)
	}
	if line.Order != "1001" || line.Customer != "alice" || line.Tier != TierGold {
		t.Fatalf("order fields: %+v", line)
	}
	if line.Sku != "widget" || line.Quantity != 3 || line.UnitCents != 250 {
		t.Fatalf("line fields: %+v", line)
	}
}

func TestRejectsBadLines(t *testing.T) {
	cases := []struct{ text, why string }{
		{"1001,alice,platinum,widget,3,2.50", "bad tier"},
		{"1001,alice,gold,widget,0,2.50", "bad quantity"},
		{"1001,alice,gold,widget,3,2.5", "bad price"},
	}
	for _, c := range cases {
		if _, err := ParseLine(c.text); err == nil || err.Error() != c.why {
			t.Errorf("%q: got %v, want %q", c.text, err, c.why)
		}
	}
}

func TestNamesTheBadLine(t *testing.T) {
	_, err := ParseOrders("order,customer,tier,sku,quantity,unit_price\n1,a,gold,x,1,1.00\n2,b,gold\n")
	if err == nil || err.Error() != "line 3: expected 6 fields" {
		t.Fatalf("got %v", err)
	}
}
