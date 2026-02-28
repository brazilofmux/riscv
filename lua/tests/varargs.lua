-- select
print(select("#", 1, 2, 3))
print(select(2, "a", "b", "c"))

-- Varargs in table constructor
local function pack(...)
    return {...}
end
local t = pack(10, 20, 30)
print(t[1], t[2], t[3])

-- table.pack and table.unpack
local t2 = table.pack(4, 5, 6)
print(t2.n)
print(table.unpack(t2, 1, t2.n))
