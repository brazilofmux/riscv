-- Basic function
local function add(a, b)
    return a + b
end
print(add(3, 4))

-- Recursion
local function factorial(n)
    if n <= 1 then return 1 end
    return n * factorial(n - 1)
end
print(factorial(10))

-- Closures
local function counter()
    local n = 0
    return function()
        n = n + 1
        return n
    end
end
local c = counter()
print(c())
print(c())
print(c())

-- Multiple return values
local function swap(a, b)
    return b, a
end
local x, y = swap(1, 2)
print(x, y)

-- Varargs
local function sum(...)
    local s = 0
    for _, v in ipairs({...}) do
        s = s + v
    end
    return s
end
print(sum(1, 2, 3, 4, 5))
