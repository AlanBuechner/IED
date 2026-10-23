# invoice

`invoice <orders.csv>` prints each order's total, then how many orders there were, their sum and the average order.

## Input

A CSV file whose first line is the header `order,customer,tier,sku,quantity,unit_price`. Every other line is one
line of an order; the lines of an order share its id, customer and tier (`standard` or `gold`). Prices have two
decimals. `data/orders.csv` is an example.

## Pricing

1. A line costs its quantity times its unit price. Ordering 10 or more of a line takes 10% off that line.
2. An order's subtotal is the sum of its lines.
3. Gold customers get 5% off the subtotal.
4. Tax is 8% of the subtotal after the gold discount.
5. Every percentage is rounded half up to the cent.

The order's total is the subtotal, less the gold discount, plus the tax.

## Output

One line per order, in the order the orders first appear: `<order> <customer> <total>`. Then
`orders <count> total <sum of totals>` and `average <sum / count, rounded half up to the cent>`; with no orders the
sum and the average are `0.00`.
