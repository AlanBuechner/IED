```go
package main

func main() {
	fmt.Println("<hello> & \"bye\" *not emphasis*")
}
```

````
```
Four backticks hold a line of three.
``` not a closing fence: it has text after it
````

``` python extra words after the language
print(1)
`````

```
```

  ```js
  indented fence lines are kept as they are
      ```
After the fence.

```c++ and more
int x = a < b ? 1 : 2;
```

```a`b` is no fence: its info string holds a backtick

> ```
> unclosed fence in a quote
>     keeps its indentation
ends with the quote

```
an unclosed fence runs to the end

# not a heading
- not a list item
