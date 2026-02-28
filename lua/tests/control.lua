-- if/else
if true then print("yes") end
if false then print("no") else print("else") end

-- if/elseif/else
local x = 5
if x < 0 then
    print("negative")
elseif x == 0 then
    print("zero")
else
    print("positive")
end

-- while
local i = 1
local s = 0
while i <= 10 do
    s = s + i
    i = i + 1
end
print(s)

-- for (numeric)
local sum = 0
for i = 1, 100 do
    sum = sum + i
end
print(sum)

-- for (step)
local count = 0
for i = 10, 1, -1 do
    count = count + 1
end
print(count)

-- repeat/until
local n = 1
repeat
    n = n * 2
until n > 100
print(n)

-- do/end block
do
    local a = 42
    print(a)
end
