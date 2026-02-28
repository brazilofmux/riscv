-- String basics
print("hello" .. " " .. "world")
print(#"hello")
print(string.len("test"))
print(string.upper("hello"))
print(string.lower("WORLD"))
print(string.rep("ab", 3))
print(string.reverse("abcd"))
print(string.sub("hello", 2, 4))
print(string.byte("A"))
print(string.char(65))

-- String format
print(string.format("%d + %d = %d", 2, 3, 5))
print(string.format("%.2f", 3.14159))
print(string.format("%s is %d", "age", 42))
print(string.format("%05d", 42))
