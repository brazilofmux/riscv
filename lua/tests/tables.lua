-- Table creation and access
local t = {10, 20, 30}
print(t[1])
print(t[2])
print(t[3])
print(#t)

-- Named fields
local p = {x = 1, y = 2}
print(p.x)
print(p.y)

-- Table insert/remove
local a = {}
table.insert(a, "first")
table.insert(a, "second")
table.insert(a, "third")
print(#a)
print(a[1])
table.remove(a, 1)
print(a[1])
print(#a)

-- Iteration with ipairs
local sum = 0
for i, v in ipairs({10, 20, 30, 40}) do
    sum = sum + v
end
print(sum)

-- Iteration with pairs
local keys = {}
local vals = {}
local d = {a=1, b=2, c=3}
for k, v in pairs(d) do
    keys[#keys+1] = k
    vals[#vals+1] = v
end
table.sort(keys)
print(table.concat(keys, ","))
