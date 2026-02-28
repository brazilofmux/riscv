-- print and tostring
print(tostring(42))
print(tostring(3.14))
print(tostring(true))
print(tostring(nil))
print(tostring("hello"))

-- tonumber
print(tonumber("42"))
print(tonumber("3.14"))
print(tonumber("0xff"))
print(tonumber("10", 2))
print(tonumber("bad") == nil)

-- io.write (no newline)
io.write("ab")
io.write("cd")
io.write("\n")

-- type
print(type(42))
print(type("hello"))
print(type(true))
print(type(nil))
print(type({}))
print(type(print))
